// vrconfig_helper - logging (loguru back-end)

#include "Log.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <system_error>

#include "loguru.hpp"
#include "XPLMUtilities.h"

#ifndef VRCH_LOG_TO_XPLANE_DEFAULT
#define VRCH_LOG_TO_XPLANE_DEFAULT 1
#endif

namespace hpvr::log {

namespace fs = std::filesystem;

namespace {

constexpr int  kKeptCopies    = 2;                 // <plugin>.log.1 .. .2
constexpr char kFileSinkId[]  = "vrconfig_helper_file";
constexpr char kXPlaneSinkId[] = "vrconfig_helper_xplane";

struct State {
    std::mutex     mutex;              // guards file, path, size
    std::FILE*     file = nullptr;
    fs::path       path;
    std::uintmax_t size = 0;
    std::string    pluginName = "vrconfig_helper";
    bool           active = false;
};

State& TheState()
{
    static State state;
    return state;
}

std::atomic<bool> gMirrorToXPlane{VRCH_LOG_TO_XPLANE_DEFAULT != 0};
#ifdef NDEBUG
std::atomic<int>  gLevel{static_cast<int>(Level::Info)};
#else
std::atomic<int>  gLevel{static_cast<int>(Level::Debug)};
#endif

const char* LevelTag(Level level)
{
    switch (level) {
        case Level::Error:   return "ERROR ";
        case Level::Warning: return "WARN ";
        case Level::Debug:   return "DEBUG ";
        default:             return "";
    }
}

void XPlaneLine(Level level, std::string_view message)
{
    const std::string line = std::format("{}: {}{}\n", TheState().pluginName, LevelTag(level), message);
    XPLMDebugString(line.c_str());
}

fs::path Numbered(const fs::path& base, int copyNumber)
{
    fs::path numbered = base;
    numbered += "." + std::to_string(copyNumber);
    return numbered;
}

// <plugin>.log.1 -> .2, <plugin>.log -> .1 (oldest copy is dropped)
void RotateFiles(const fs::path& base)
{
    std::error_code ec;
    fs::remove(Numbered(base, kKeptCopies), ec);
    for (int i = kKeptCopies - 1; i >= 1; --i) {
        if (fs::exists(Numbered(base, i), ec))
            fs::rename(Numbered(base, i), Numbered(base, i + 1), ec);
    }
    if (fs::exists(base, ec) && fs::file_size(base, ec) > 0)
        fs::rename(base, Numbered(base, 1), ec);
}

// Caller holds TheState().mutex
bool OpenFile()
{
    State& state = TheState();
    state.file = std::fopen(state.path.string().c_str(), "w");
    state.size = 0;
    return state.file != nullptr;
}

// --- loguru sinks -------------------------------------------------------------------

void FileSink(void*, const loguru::Message& message)
{
    State& state = TheState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.file)
        return;

    const std::string line = std::format("{}{}{}{}\n", message.preamble, message.indentation, message.prefix, message.message);
    std::fwrite(line.data(), 1, line.size(), state.file);
    std::fflush(state.file);                       // keep the log complete if X-Plane crashes
    state.size += line.size();

    if (state.size >= kMaxLogFileSize) {
        std::fclose(state.file);
        state.file = nullptr;
        RotateFiles(state.path);
        OpenFile();
    }
}

void FileSinkFlush(void*)
{
    State& state = TheState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.file)
        std::fflush(state.file);
}

void FileSinkClose(void*)
{
    State& state = TheState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.file) {
        std::fclose(state.file);
        state.file = nullptr;
    }
}

const char* VerbosityName(loguru::Verbosity verbosity)
{
    return verbosity == static_cast<loguru::Verbosity>(Level::Debug) ? "DBG" : nullptr;   // nullptr: loguru default
}

void XPlaneSink(void*, const loguru::Message& message)
{
    if (gMirrorToXPlane.load())
        XPlaneLine(static_cast<Level>(message.verbosity), message.message);
}

} // namespace

bool Init(std::string_view pluginName, const fs::path& directory)
{
    State& state = TheState();
    if (state.active)
        Shutdown();

    state.pluginName = std::string(pluginName);

    // loguru is used without loguru::init(): no signal handlers inside X-Plane,
    // no argv parsing, X-Plane's main thread keeps its name.
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    loguru::g_preamble_header  = false;
    loguru::g_preamble_date    = true;
    loguru::g_preamble_time    = true;
    loguru::g_preamble_uptime  = false;
    loguru::g_preamble_thread  = false;
    loguru::g_preamble_file    = true;
    loguru::g_preamble_verbose = true;
    loguru::g_preamble_pipe    = true;
    loguru::set_verbosity_to_name_callback(VerbosityName);

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        std::error_code ec;
        fs::create_directories(directory, ec);
        state.path = directory / (state.pluginName + ".log");
        RotateFiles(state.path);
        if (!OpenFile()) {
            XPlaneLine(Level::Error, std::format("[{}] cannot open log file {}", __func__, state.path.string()));
            return false;
        }
    }

    loguru::add_callback(kFileSinkId,   FileSink,   nullptr, loguru::Verbosity_MAX, FileSinkClose, FileSinkFlush);
    loguru::add_callback(kXPlaneSinkId, XPlaneSink, nullptr, loguru::Verbosity_MAX);
    state.active = true;
    return true;
}

void Shutdown()
{
    State& state = TheState();
    if (!state.active)
        return;
    loguru::remove_callback(kXPlaneSinkId);
    loguru::remove_callback(kFileSinkId);     // calls FileSinkClose
    state.active = false;
}

void SetMirrorToXPlane(bool enabled) { gMirrorToXPlane.store(enabled); }
bool GetMirrorToXPlane()             { return gMirrorToXPlane.load(); }

void  SetLevel(Level level)   { gLevel.store(static_cast<int>(level)); }
Level GetLevel()              { return static_cast<Level>(gLevel.load()); }
bool  IsEnabled(Level level)  { return static_cast<int>(level) <= gLevel.load(); }

fs::path GetLogFilePath()
{
    State& state = TheState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.path;
}

void Write(Level level, const char* file, unsigned line, std::string_view message)
{
    if (!TheState().active) {
        // Before Init() / after Shutdown(): don't lose the message.
        XPlaneLine(level, message);
        return;
    }
    const std::string text(message);
    loguru::log(static_cast<loguru::Verbosity>(level), file, line, "%s", text.c_str());
}

} // namespace hpvr::log

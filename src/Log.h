// vrconfig_helper - logging
//
// Thin layer over loguru:
//   * every message goes to "<plugin folder>/<plugin>.log"
//     (rotated: <plugin>.log -> .log.1 -> .log.2, at start-up and when the
//     file grows past info::kLogMaxFileSize; at most 2 old copies are kept)
//   * optionally mirrored to X-Plane's Log.txt (see SetMirrorToXPlane)
//   * messages are formatted with C++20 std::format
//
// Use the macros, they add the calling function automatically:
//     LOG_INFO("plugin started, version {}", version);   ->  "[XPluginStart] plugin started, version 0.1.0"
//     LOG_DEBUG("value={}", v);                         ->  "[MyFunc:42] value=7"
//
// __func__ is "operator()" inside a lambda, so lambdas must be named with the
// "lmbda_" prefix and log through the *_AS macros with that name:
//     auto lmbda_onClick = [&]() { LOG_INFO_AS("lmbda_onClick", "clicked {}", id); };

#pragma once

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace hpvr::log {

enum class Level : int {
    Error   = -2,
    Warning = -1,
    Info    =  0,
    Debug   =  1,
};

// Largest size of the active log file before it is rotated.

// Start logging into <directory>/<pluginName>.log. Safe to call more than once.
bool Init(std::string_view pluginName, const std::filesystem::path& directory);
// Flush and close the log file.
void Shutdown();

// Also write every message to X-Plane's Log.txt.
// Default: info::kLogToXPlaneDefault (SharedInfo.h).
void SetMirrorToXPlane(bool enabled);
bool GetMirrorToXPlane();

// Messages above this level are dropped (Debug in debug builds, Info otherwise).
void SetLevel(Level level);
Level GetLevel();
bool IsEnabled(Level level);

std::filesystem::path GetLogFilePath();

// Writes an already formatted message (no function name added).
void Write(Level level, const char* file, unsigned line, std::string_view message);

template <class... Args>
void Format(Level level, const char* file, unsigned line, const char* func,
            std::format_string<Args...> fmt, Args&&... args)
{
    if (!IsEnabled(level))
        return;
    const std::string body = std::format(fmt, std::forward<Args>(args)...);
    if (level == Level::Debug)
        Write(level, file, line, std::format("[{}:{}] {}", func, line, body));
    else
        Write(level, file, line, std::format("[{}] {}", func, body));
}

} // namespace hpvr::log

// --- Logging macros ----------------------------------------------------------------
#define VRCH_LOG_IMPL(level, func, fmt, ...) \
    ::hpvr::log::Format((level), __FILE__, __LINE__, (func), fmt __VA_OPT__(,) __VA_ARGS__)

#define LOG_ERROR(fmt, ...) VRCH_LOG_IMPL(::hpvr::log::Level::Error,   __func__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN(fmt, ...)  VRCH_LOG_IMPL(::hpvr::log::Level::Warning, __func__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO(fmt, ...)  VRCH_LOG_IMPL(::hpvr::log::Level::Info,    __func__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG(fmt, ...) VRCH_LOG_IMPL(::hpvr::log::Level::Debug,   __func__, fmt __VA_OPT__(,) __VA_ARGS__)

// For lambdas: pass the lambda's name ("lmbda_...") instead of __func__.
#define LOG_ERROR_AS(name, fmt, ...) VRCH_LOG_IMPL(::hpvr::log::Level::Error,   name, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN_AS(name, fmt, ...)  VRCH_LOG_IMPL(::hpvr::log::Level::Warning, name, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO_AS(name, fmt, ...)  VRCH_LOG_IMPL(::hpvr::log::Level::Info,    name, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG_AS(name, fmt, ...) VRCH_LOG_IMPL(::hpvr::log::Level::Debug,   name, fmt __VA_OPT__(,) __VA_ARGS__)

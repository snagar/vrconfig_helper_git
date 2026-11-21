// hpvr - small file helpers shared by the edit sessions

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace hpvr::util {

// Paths and UTF-8 text. X-Plane hands out UTF-8 paths, and ImGui and the log
// show UTF-8. On Windows, std::filesystem::path(std::string) and .string() use
// the local code page instead, so non-ASCII folder names would break: always
// go through these two. On Linux and macOS they change nothing.
inline std::string Utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

inline std::filesystem::path PathFromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

// A file's modification time (fs::last_write_time) as a system_clock time, for
// showing it as a date. std::chrono::clock_cast does this in C++20, but Apple's
// libc++ (Xcode 16) does not have it; this measures both clocks' "now" instead,
// which is exact to well under a second.
inline std::chrono::system_clock::time_point ToSystemTime(std::filesystem::file_time_type fileTime)
{
    using namespace std::chrono;
    return time_point_cast<system_clock::duration>(fileTime - std::filesystem::file_time_type::clock::now()
                                                   + system_clock::now());
}

// The whole file as bytes; empty when it cannot be read.
std::string ReadAll(const std::filesystem::path& path);

// Copies through a temporary file + rename, so `to` is never half written.
// On failure `error` says why.
bool CopyFileSafely(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error);

// Writes `text` through a temporary file + rename, so `path` is never half
// written (an existing file is replaced in one step). On failure `error` says why.
bool WriteFileSafely(const std::filesystem::path& path, const std::string& text, std::string& error);

} // namespace hpvr::util

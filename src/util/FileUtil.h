// hpvr - small file helpers shared by the edit sessions

#pragma once

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

// The whole file as bytes; empty when it cannot be read.
std::string ReadAll(const std::filesystem::path& path);

// Copies through a temporary file + rename, so `to` is never half written.
// On failure `error` says why.
bool CopyFileSafely(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error);

// Writes `text` through a temporary file + rename, so `path` is never half
// written (an existing file is replaced in one step). On failure `error` says why.
bool WriteFileSafely(const std::filesystem::path& path, const std::string& text, std::string& error);

} // namespace hpvr::util

// hpvr - small file helpers shared by the edit sessions

#pragma once

#include <filesystem>
#include <string>

namespace hpvr::util {

// The whole file as bytes; empty when it cannot be read.
std::string ReadAll(const std::filesystem::path& path);

// Copies through a temporary file + rename, so `to` is never half written.
// On failure `error` says why.
bool CopyFileSafely(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error);

// Writes `text` through a temporary file + rename, so `path` is never half
// written (an existing file is replaced in one step). On failure `error` says why.
bool WriteFileSafely(const std::filesystem::path& path, const std::string& text, std::string& error);

} // namespace hpvr::util

// hpvr - small file helpers shared by the edit sessions

#include "FileUtil.h"

#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace hpvr::util {

namespace fs = std::filesystem;

std::string ReadAll(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool CopyFileSafely(const fs::path& from, const fs::path& to, std::string& error)
{
    fs::path tmp = to;
    tmp += ".tmp";
    std::error_code ec;
    fs::copy_file(from, tmp, fs::copy_options::overwrite_existing, ec);
    if (!ec)
        fs::rename(tmp, to, ec);
    if (ec) {
        error = std::format("cannot copy {} to {}: {}", util::Utf8(from), util::Utf8(to), ec.message());
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool WriteFileSafely(const fs::path& path, const std::string& text, std::string& error)
{
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = std::format("cannot write {}", util::Utf8(tmp));
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            error = std::format("write failed: {}", util::Utf8(tmp));
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        error = std::format("cannot replace {}: {}", util::Utf8(path), ec.message());
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace hpvr::util

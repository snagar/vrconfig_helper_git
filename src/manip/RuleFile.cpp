// hpvr - the plugin's rule files (manip_types.ini, manip_actions.ini)

#include "RuleFile.h"

#include <format>
#include <functional>
#include <system_error>

#include "util/FileUtil.h"

namespace hpvr::manip {

namespace fs = std::filesystem;

RuleFile::RuleFile(fs::path folder, std::string name, int version)
    : mFolder(std::move(folder)), mName(std::move(name)), mVersion(version)
{
}

bool RuleFile::Read(std::string& text)
{
    std::error_code ec;
    mCustom = fs::exists(CustomPath(), ec);
    mPath   = mCustom ? CustomPath() : (fs::exists(DefaultPath(), ec) ? DefaultPath() : fs::path());
    text    = mPath.empty() ? std::string() : util::ReadAll(mPath);
    const std::size_t hash = std::hash<std::string>{}(util::Utf8(mPath) + '\n' + text);
    if (mHash && *mHash == hash)
        return false;
    mHash = hash;
    return true;
}

std::string RuleFile::UsedName() const
{
    return mPath.empty() ? mName : util::Utf8(mPath.filename());
}

std::string RuleFile::Notice(int fileVersion) const
{
    if (!Found())
        return std::format("{} is missing from the plugin folder - reinstall the plugin.", mName);
    if (fileVersion == mVersion)
        return {};
    if (mCustom)
        return std::format("{} was made from {} version {}; the plugin has version {} - compare your copy with "
                           "the new {} and update it.", UsedName(), mName, fileVersion, mVersion, mName);
    return std::format("{} is version {}, this plugin needs version {} - reinstall the plugin.", mName, fileVersion,
                       mVersion);
}

} // namespace hpvr::manip

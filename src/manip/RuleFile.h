// hpvr - the plugin's rule files (manip_types.ini, manip_actions.ini)
//
// Each rule file ships with the plugin and is replaced by every update, so it
// must not be edited. A user who wants other rules copies it to
// "custom_<name>" in the same folder and edits the copy: when the custom file
// exists it is used instead of the plugin's own (the whole file, not merged).
//
// Each file starts with "[file]  version = N". When the file used has another
// version than the plugin expects, Notice() says what to do (a custom file
// made from an older plugin file: compare it with the new one; the plugin's own
// file from another version: reinstall the plugin).
//
// No X-Plane calls here.

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace hpvr::manip {

class RuleFile {
public:
    // `name`: "manip_actions.ini"; `version`: the version this plugin expects.
    RuleFile(std::filesystem::path folder, std::string name, int version);

    // Reads custom_<name> when it exists, else <name>. True when the file used
    // or its content changed since the last Read (then `text` is the new content).
    bool Read(std::string& text);

    const std::string&           Name() const       { return mName; }
    const std::filesystem::path& Path() const       { return mPath; }       // the file used; empty = none found
    std::filesystem::path        DefaultPath() const { return mFolder / mName; }
    std::filesystem::path        CustomPath() const  { return mFolder / ("custom_" + mName); }
    bool Found() const  { return !mPath.empty(); }
    bool Custom() const { return mCustom; }
    int  ExpectedVersion() const { return mVersion; }

    // What the page should tell the user about the file (empty = nothing):
    // not found, or a version that does not match. `fileVersion`: as read (0 = none).
    std::string Notice(int fileVersion) const;
    // "custom_manip_actions.ini" / "manip_actions.ini" - for the page and the log.
    std::string UsedName() const;

private:
    std::filesystem::path      mFolder;
    std::string                mName;
    int                        mVersion = 0;
    std::filesystem::path      mPath;
    bool                       mCustom = false;
    std::optional<std::size_t> mHash;
};

} // namespace hpvr::manip

// hpvr - edit session of "VRConfig Manipulator Updater": the BEGIN_MANIP blocks
// of one vrconfig file (any aircraft, not only the loaded one)
//
// Rules (agreed design):
//  * Each block can be changed (new lines for BEGIN_MANIP..END_MANIP) or
//    removed. Changes are kept in memory until Save or Cancel - also while
//    another page is shown, the window is closed or another aircraft is picked.
//    Loading an aircraft discards them only when the file belongs to the loaded
//    aircraft (before or after the load).
//  * Nothing is written before Save. Save applies every change at once, then
//    reads the file again.
//  * Backup: the first Save of the file in this X-Plane run copies it to
//    "<file>.updater.bak" (its own backup, apart from the hotspot and Extract
//    ones). The first save in a later run replaces it. RestoreBackup puts it back.
//  * A block without END_MANIP cannot be changed.
//
// No X-Plane calls here, so the class can be tested on its own.

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "manip/ManipEditor.h"      // SaveResult
#include "vrconfig/VrConfigFile.h"

namespace hpvr::manip {

enum class UpdaterOpen {
    Opened,         // file read, no pending changes
    KeptChanges,    // same file with unsaved changes: nothing re-read
    NoFile,         // the aircraft has no vrconfig file
    ReadError,
};

class ManipUpdater {
public:
    using Clock = std::chrono::system_clock;

    // `forLoadedAircraft`: the file belongs to the aircraft X-Plane has loaded.
    UpdaterOpen Open(const std::filesystem::path& vrconfigPath, bool forLoadedAircraft);
    void Discard();

    bool IsOpen() const            { return mOpen; }
    bool ForLoadedAircraft() const { return mForLoadedAircraft; }
    const std::filesystem::path&  FilePath() const  { return mFile.FilePath(); }
    const vrconfig::VrConfigFile& File() const      { return mFile; }
    const std::string&            LastError() const { return mLastError; }

    // --- blocks (index = order in the file) ---
    std::size_t BlockCount() const { return mFile.Manips().size(); }
    const vrconfig::ManipRecord& Record(std::size_t block) const { return mFile.Manips()[block]; }
    bool CanEdit(std::size_t block) const;                      // has END_MANIP
    std::vector<std::string> SavedLines(std::size_t block) const;
    // The lines as they will be written (the saved ones when unchanged).
    std::vector<std::string> CurrentLines(std::size_t block) const;

    // --- changes ---
    // New lines for a block (exactly one block, checked by the caller). The
    // same lines as saved undo the change. `handEdited`: typed as free text.
    void SetBlock(std::size_t block, const std::vector<std::string>& lines, bool handEdited);
    void RemoveBlock(std::size_t block);
    void Revert(std::size_t block);

    bool IsChanged(std::size_t block) const   { return mPending.contains(block); }
    bool IsRemoved(std::size_t block) const;
    bool IsHandEdited(std::size_t block) const;
    bool SavedThisRun(std::size_t block) const;                 // green: written by a Save in this run

    std::size_t ChangeCount() const { return mPending.size(); }
    std::vector<std::string> ChangeLines() const;               // "Change: <name>", "Remove: <name>"
    void CancelChanges();

    SaveResult Save(bool overwriteOutsideChanges = false);
    std::optional<Clock::time_point> LastSaveTime() const { return mLastSave; }

    // --- backup ---
    std::filesystem::path BackupPath() const;                  // <file>.updater.bak
    std::filesystem::path RestorePath() const;                 // BackupPath(), or the name used up to v1.0
    bool HasBackup() const;
    bool BackupMadeThisRun() const;
    std::optional<Clock::time_point> BackupTime() const;
    bool RestoreBackup();

    // Name for people: the tooltip, else the last part of the first command / dataref.
    static std::string BlockName(const vrconfig::ManipRecord& record);

    // Test support: forget which files were backed up in this run.
    static void ResetRunState();

private:
    struct Pending {
        std::vector<std::string> lines;
        bool                     removed    = false;
        bool                     handEdited = false;
    };

    bool ChangedOnDisk() const;
    std::string Identity(const vrconfig::ManipRecord& record) const;

    vrconfig::VrConfigFile             mFile;
    std::string                        mLoadedText;
    std::map<std::size_t, Pending>     mPending;
    std::set<std::string>              mSavedIdentities;     // blocks written by a Save in this session
    bool                               mOpen = false;
    bool                               mForLoadedAircraft = false;
    std::optional<Clock::time_point>   mLastSave;
    std::string                        mLastError;
};

} // namespace hpvr::manip

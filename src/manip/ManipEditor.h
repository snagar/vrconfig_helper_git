// hpvr - edit session for the manipulator blocks of one vrconfig file
//
// Extraction is the first step of the VR setup: every manipulator found in the
// OBJ files gets a BEGIN_MANIP block with default settings. Fine tuning of each
// manipulator is a later screen.
//
// Rules (agreed design, same as the hotspot page):
//  * "The same manipulator" = same type and same commands / datarefs.
//  * Each extracted manipulator is a row with a tick: ticked = written on Save.
//    A new one starts ticked (appended at the end of the file on Save); one
//    already in the file starts unticked ("exists") - ticking it replaces its
//    block in place (the comment above it stays). "Override existing" ticks
//    all of those. A row the user unticked stays unticked when extracting
//    again, until the aircraft is loaded.
//  * Changes are kept in memory until Save or Cancel, also while another page
//    is shown or the window is closed. Loading an aircraft discards them.
//  * Backup: the first Save of an existing file in this X-Plane run copies it to
//    "<file>.extract.bak" (not the hotspot backup). The first save in a later run
//    replaces it. RestoreBackup puts it back. A new file has no backup.
//  * Default block content: knobs DEG_PER_ACTION (30 selector, 10 altitude,
//    else 5); switches empty; levers and yoke empty with a TODO comment.
//
// No X-Plane calls here, so the class can be tested on its own.

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "manip/ObjManips.h"
#include "vrconfig/VrConfigFile.h"

namespace hpvr::manip {

enum class OpenResult {
    Opened,         // file read, no pending changes
    KeptChanges,    // same file with unsaved changes: nothing re-read
    NewFile,        // the aircraft has no vrconfig file: one is started in memory (written on Save)
    ReadError,
};

enum class SaveResult {
    Saved,
    NothingToSave,
    ChangedOnDisk,  // the file was modified outside the plugin since it was read
    Failed,
};

// State of one extracted manipulator.
enum class RowStatus {
    Added,          // ticked, not in the file: appended on Save
    Overridden,     // ticked, in the file: its block is replaced on Save
    Exists,         // unticked, a block is in the file
    Saved,          // unticked, written by a Save in this session
    Skipped,        // unticked, not in the file: not written
};

struct ExtractSummary {
    std::size_t found = 0, added = 0, overridden = 0, exists = 0, saved = 0, skipped = 0;
};

class ManipEditor {
public:
    using Clock = std::chrono::system_clock;

    OpenResult Open(const std::filesystem::path& vrconfigPath);
    // Forget the file, the extraction and every unsaved change (aircraft loaded).
    void Discard();

    bool IsOpen() const     { return mOpen; }
    bool FileOnDisk() const { return mFileOnDisk; }
    const std::filesystem::path&  FilePath() const  { return mFile.FilePath(); }
    const vrconfig::VrConfigFile& File() const      { return mFile; }
    const std::string&            LastError() const { return mLastError; }

    // --- extraction ---
    // Replaces the rows by `manips`; ticked rows of the previous extraction that
    // are not in it stay (unsaved changes are never dropped silently).
    ExtractSummary Extract(const std::vector<ObjManip>& manips, bool overrideExisting);
    std::size_t     RowCount() const { return mRows.size(); }
    const ObjManip& Row(std::size_t row) const { return mRows[row].manip; }
    bool            Ticked(std::size_t row) const { return mRows[row].ticked; }
    void            SetTicked(std::size_t row, bool ticked);
    void            SetAllTicked(bool ticked);
    RowStatus       Status(std::size_t row) const;
    ExtractSummary  Summary() const;             // of the current rows

    // --- state ---
    std::size_t ChangeCount() const;             // ticked rows
    std::vector<std::string> ChangeLines() const;      // "Add: ALT dial", ...
    void CancelChanges();                              // unticks every row

    SaveResult Save(bool overwriteOutsideChanges = false);
    std::optional<Clock::time_point> LastSaveTime() const { return mLastSave; }

    // --- backup ---
    std::filesystem::path BackupPath() const;          // <file>.extract.bak
    std::filesystem::path RestorePath() const;                 // BackupPath(), or the name used up to v1.0
    bool HasBackup() const;
    bool BackupMadeThisRun() const;
    std::optional<Clock::time_point> BackupTime() const;
    bool RestoreBackup();

    // Lines written for one manipulator: BEGIN_MANIP, its body (indented with a
    // tab), END_MANIP. `withComment` adds the name line above.
    static std::vector<std::string> BlockLines(const ObjManip& manip, bool withComment);

    // Test support: forget which files were backed up in this run.
    static void ResetRunState();

private:
    struct RowEntry {
        ObjManip manip;
        bool     ticked = false;
    };

    void ReadKeys();
    bool InFile(const std::string& key) const { return mFileKeys.contains(key); }
    bool ChangedOnDisk() const;

    vrconfig::VrConfigFile            mFile;
    std::string                       mLoadedText;      // file content when read / saved
    std::set<std::string>             mFileKeys;        // manipulators in the file
    std::set<std::string>             mSavedKeys;       // written by Save in this session
    std::vector<RowEntry>                  mRows;            // last extraction (+ ticked rows kept)
    std::set<std::string>             mUnticked;        // new manipulators the user unticked
    bool                              mOpen = false;
    bool                              mFileOnDisk = false;
    std::optional<Clock::time_point>  mLastSave;
    std::string                       mLastError;
};

} // namespace hpvr::manip

// hpvr - edit session for the teleport spots of one vrconfig file
//
// Rules (agreed design):
//  * Changes (move, turn, rename, add, delete) are kept in memory until Save or
//    a cancel action, also while the window shows another page or is closed.
//    Loading or reloading an aircraft discards them (Discard).
//  * Save writes every change at once through VrConfigFile: updates, then
//    deletions, then new blocks; afterwards the file is read again.
//  * Backup: the first Save of an existing file in this X-Plane run copies it to
//    "<file>.bak". Later saves in the same run keep that backup; the first save
//    in a later run replaces it. A new file (created by the plugin) has no backup.
//  * RestoreBackup puts the .bak back (kept, can be restored again).
//  * Commas between the numbers of a hotspot (written by some tools) make it
//    read-only; FixCommas removes them on Save, like any other change.
//  * "Saved this run" (green): a spot that is not identical to any spot of
//    this run's backup.
//
// No X-Plane calls here, so the class can be tested on its own.

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "vrconfig/VrConfigFile.h"

namespace hpvr::hotspot {

using vrconfig::SpotKind;
using vrconfig::SpotRecord;
using vrconfig::TeleportSpot;
using vrconfig::Vec3;

enum class OpenResult {
    Opened,         // file read, no pending changes
    KeptChanges,    // same file with unsaved changes: nothing re-read
    NoFile,         // the aircraft has no vrconfig file (StartNewFile can create one)
    ReadError,
};

enum class SaveResult {
    Saved,
    NothingToSave,
    ChangedOnDisk,  // the file was modified outside the plugin since it was read
    Failed,
};

// One editable value of a hotspot (Advanced popup).
enum class SpotField {
    X,          // right
    Y,          // up
    Z,          // aft
    Heading,    // PRESET_PSI  0..360
    Pitch,      // PRESET_THE  -90..90
    Roll,       // PRESET_PHI  -180..180 (sideways tilt)
};

inline constexpr SpotField kAllSpotFields[] = {
    SpotField::X, SpotField::Y, SpotField::Z, SpotField::Heading, SpotField::Pitch, SpotField::Roll,
};

const char* SpotFieldLabel(SpotField field);        // "X (right)", "Pitch (THE)", ...
int         SpotFieldDecimals(SpotField field);     // 4 for meters, 1 for degrees
double      GetSpotField(const TeleportSpot& spot, SpotField field);

struct SpotChanges {
    std::size_t              index = 0;
    std::string              name;
    std::vector<std::string> what;   // "new", "delete", "position", "heading", "pitch/roll", "renamed to ..."
};

// Request for a new spot.
struct NewSpotRequest {
    std::string                name;        // empty: next free "Hotspot N"
    SpotKind                   kind = SpotKind::Sitting;
    // Start position: either a head position, or a copy of an existing spot.
    std::optional<Vec3>        position;
    double                     heading = 0.0;
    std::optional<std::size_t> copyOf;      // used when position is empty
    std::optional<std::size_t> boxTemplate; // spot whose teleport box shape to reuse (same kind preferred)
};

class SpotEditor {
public:
    using Clock = std::chrono::system_clock;

    OpenResult Open(const std::filesystem::path& vrconfigPath);
    // The aircraft has no vrconfig file: start one in memory (written on Save).
    void StartNewFile(const std::filesystem::path& vrconfigPath);
    // Forget the file and every unsaved change (aircraft loaded / refresh).
    void Discard();

    bool IsOpen() const     { return mOpen; }
    bool FileOnDisk() const { return mFileOnDisk; }
    const std::filesystem::path&  FilePath() const  { return mFile.FilePath(); }
    const vrconfig::VrConfigFile& File() const      { return mFile; }
    const std::string&            LastError() const { return mLastError; }

    // --- spots (index = position in the list shown to the user) ---
    std::size_t         SpotCount() const { return mSlots.size(); }
    bool                IsNew(std::size_t spotIndex) const     { return !mSlots[spotIndex].fileIndex.has_value(); }
    bool                IsDeleted(std::size_t spotIndex) const { return mSlots[spotIndex].deleted; }
    bool                IsEditable(std::size_t spotIndex) const;           // editable and not deleted
    const SpotRecord*   Record(std::size_t spotIndex) const;               // nullptr for a new spot
    const TeleportSpot* SavedSpot(std::size_t spotIndex) const;            // nullptr for a new spot
    const TeleportSpot& Edited(std::size_t spotIndex) const { return mSlots[spotIndex].spot; }

    // --- editing (ignored for spots that are not editable) ---
    void Move(std::size_t spotIndex, const Vec3& delta);                   // position and box together
    // Stores the position exactly as given (the spot type does not change it);
    // pitch and roll are set to 0.
    void PlaceAt(std::size_t spotIndex, const Vec3& position, double heading);
    void Turn(std::size_t spotIndex, double degrees);                      // heading, wraps 0..360
    void SetHeading(std::size_t spotIndex, double degrees);                // heading, wraps 0..360
    // Sets one value: X/Y/Z move the teleport box along, heading and roll wrap,
    // pitch is limited to -90..90.
    void SetField(std::size_t spotIndex, SpotField field, double value);
    void ChangeField(std::size_t spotIndex, SpotField field, double delta);
    void SetValues(std::size_t spotIndex, const TeleportSpot& spot);       // typed values; box follows position
    void Rename(std::size_t spotIndex, const std::string& name);

    // A spot whose only problem is commas between its numbers: FixCommas makes it
    // editable now; the commas are removed from the file on Save (an unsaved change).
    bool CommasFixable(std::size_t spotIndex) const;
    bool CommasFixed(std::size_t spotIndex) const { return spotIndex < mSlots.size() && mSlots[spotIndex].commasFixed; }
    void FixCommas(std::size_t spotIndex);

    // Adds a new spot (pending until Save). Returns its index.
    std::size_t AddSpot(const NewSpotRequest& request);
    // Marks for deletion; a new (unsaved) spot is removed at once.
    // Returns true when the slot was removed from the list (indexes shift).
    bool Delete(std::size_t spotIndex);
    void UndoDelete(std::size_t spotIndex);

    // Undo every change of one spot. Returns true when the slot was removed (new spot).
    bool Revert(std::size_t spotIndex);
    void RevertAll();

    std::string NextFreeName() const;                              // "Hotspot N"
    bool NameInUse(const std::string& name, std::optional<std::size_t> except = std::nullopt) const;

    // --- state ---
    bool IsChanged(std::size_t spotIndex) const;
    std::size_t ChangedSpotCount() const;
    std::size_t ChangeCount() const;
    std::vector<SpotChanges> Changes() const;

    bool SavedThisRun(std::size_t spotIndex) const;                        // green
    std::optional<TeleportSpot> BackupSpot(std::size_t spotIndex) const;   // same-name spot in this run's backup

    SaveResult Save(bool overwriteOutsideChanges = false);
    std::optional<Clock::time_point> LastSaveTime() const { return mLastSave; }

    // --- backup ---
    std::filesystem::path BackupPath() const;
    bool HasBackup() const;
    bool BackupMadeThisRun() const;
    std::optional<Clock::time_point> BackupTime() const;
    bool RestoreBackup();

    // Test support: forget which files were backed up in this run.
    static void ResetRunState();

private:
    struct Slot {
        std::optional<std::size_t> fileIndex;   // empty = new spot
        TeleportSpot               spot;        // edited values
        bool                       deleted = false;
        bool                       commasFixed = false;
    };

    void RebuildSlots();
    bool ChangedOnDisk() const;
    void LoadBackupView();

    vrconfig::VrConfigFile            mFile;
    std::vector<Slot>                 mSlots;
    std::string                       mLoadedText;      // file content when read / saved
    vrconfig::VrConfigFile            mBackupView;      // this run's backup, for "saved this run"
    bool                              mBackupViewLoaded = false;
    bool                              mOpen = false;
    bool                              mFileOnDisk = false;
    std::optional<Clock::time_point>  mLastSave;
    std::string                       mLastError;
};

// "HH:MM" in local time.
std::string ClockTime(SpotEditor::Clock::time_point when);

} // namespace hpvr::hotspot

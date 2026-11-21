// hpvr - edit session for the manipulator blocks of one vrconfig file

#include "ManipEditor.h"

#include <algorithm>
#include <format>
#include <map>
#include <system_error>

#include "Log.h"
#include "util/FileUtil.h"

namespace hpvr::manip {

namespace fs = std::filesystem;
using util::CopyFileSafely;
using util::ReadAll;

namespace {

// Files backed up in this X-Plane run (plugin lifetime); separate from the hotspot backups.
std::set<std::string>& BackedUpThisRun()
{
    static std::set<std::string> files;
    return files;
}

constexpr const char* kIndent = "\t";        // as in Laminar's vrconfig files

} // namespace

void ManipEditor::ResetRunState()
{
    BackedUpThisRun().clear();
}

// --- open / discard -------------------------------------------------------------------

OpenResult ManipEditor::Open(const fs::path& vrconfigPath)
{
    if (mOpen && mFile.FilePath() == vrconfigPath && ChangeCount() > 0) {
        LOG_INFO("keeping {} unsaved manipulator changes of {}", ChangeCount(), vrconfigPath.filename().string());
        return OpenResult::KeptChanges;
    }

    const bool samePath = mOpen && mFile.FilePath() == vrconfigPath;
    std::vector<RowEntry> rows = samePath ? mRows : std::vector<RowEntry>{};
    std::set<std::string> savedKeys = samePath ? mSavedKeys : std::set<std::string>{};
    std::set<std::string> unticked  = samePath ? mUnticked : std::set<std::string>{};
    const auto lastSave = samePath ? mLastSave : std::nullopt;
    Discard();
    mRows      = std::move(rows);           // the report stays while the same aircraft is shown
    mSavedKeys = std::move(savedKeys);
    mUnticked  = std::move(unticked);
    mLastSave  = lastSave;

    std::error_code ec;
    if (vrconfigPath.empty() || !fs::exists(vrconfigPath, ec)) {
        mFile.CreateNew(vrconfigPath);
        mFileOnDisk = false;
        mOpen = true;
        ReadKeys();
        LOG_INFO("no vrconfig file yet: {} (created on Save)", vrconfigPath.string());
        return OpenResult::NewFile;
    }
    if (!mFile.Load(vrconfigPath)) {
        mLastError = mFile.LastError();
        return OpenResult::ReadError;
    }
    mFileOnDisk = true;
    mLoadedText = mFile.Text();
    mOpen = true;
    ReadKeys();
    return OpenResult::Opened;
}

void ManipEditor::Discard()
{
    if (mOpen && ChangeCount() > 0)
        LOG_INFO("{} unsaved manipulator changes of {} discarded", ChangeCount(),
                 mFile.FilePath().filename().string());
    mFile = vrconfig::VrConfigFile{};
    mLoadedText.clear();
    mFileKeys.clear();
    mSavedKeys.clear();
    mRows.clear();
    mUnticked.clear();
    mOpen = false;
    mFileOnDisk = false;
    mLastSave.reset();
    mLastError.clear();
}

void ManipEditor::ReadKeys()
{
    mFileKeys.clear();
    for (const vrconfig::ManipRecord& rec : mFile.Manips())
        mFileKeys.insert(ManipKey(rec.type, rec.refs));
}

// --- extraction -----------------------------------------------------------------------

ExtractSummary ManipEditor::Extract(const std::vector<ObjManip>& manips, bool overrideExisting)
{
    std::map<std::string, bool> previous;                  // key -> ticked
    for (const RowEntry& row : mRows)
        previous[row.manip.Key()] = row.ticked;

    std::vector<RowEntry> rows;
    std::set<std::string> keys;
    for (const ObjManip& manip : manips) {
        const std::string key = manip.Key();
        if (!keys.insert(key).second)
            continue;
        RowEntry row{manip, false};
        if (InFile(key)) {
            const auto before = previous.find(key);
            row.ticked = overrideExisting || (before != previous.end() && before->second);
        } else {
            row.ticked = !mUnticked.contains(key);
        }
        rows.push_back(std::move(row));
    }
    // Ticked rows of the previous extraction that are not in this one stay.
    for (const RowEntry& row : mRows) {
        if (row.ticked && !keys.contains(row.manip.Key()))
            rows.push_back(row);
    }
    mRows = std::move(rows);

    const ExtractSummary summary = Summary();
    LOG_INFO("extracted {} manipulators: {} added, {} overridden, {} exist, {} saved before, {} skipped (override {})",
             summary.found, summary.added, summary.overridden, summary.exists, summary.saved, summary.skipped,
             overrideExisting ? "on" : "off");
    return summary;
}

void ManipEditor::SetTicked(std::size_t row, bool ticked)
{
    if (row >= mRows.size() || mRows[row].ticked == ticked)
        return;
    mRows[row].ticked = ticked;
    const std::string key = mRows[row].manip.Key();
    if (!InFile(key)) {
        if (ticked)
            mUnticked.erase(key);
        else
            mUnticked.insert(key);                         // remembered for the next extraction
    }
    LOG_INFO("{} '{}' ({})", ticked ? "tick" : "untick", mRows[row].manip.DisplayName(), key);
}

void ManipEditor::SetAllTicked(bool ticked)
{
    for (std::size_t row = 0; row < mRows.size(); ++row) {
        if (mRows[row].ticked == ticked)
            continue;
        mRows[row].ticked = ticked;
        const std::string key = mRows[row].manip.Key();
        if (!InFile(key)) {
            if (ticked)
                mUnticked.erase(key);
            else
                mUnticked.insert(key);
        }
    }
    LOG_INFO("{} all {} manipulators", ticked ? "tick" : "untick", mRows.size());
}

RowStatus ManipEditor::Status(std::size_t row) const
{
    const std::string key = mRows[row].manip.Key();
    if (mRows[row].ticked)
        return InFile(key) ? RowStatus::Overridden : RowStatus::Added;
    if (mSavedKeys.contains(key))
        return RowStatus::Saved;
    if (InFile(key))
        return RowStatus::Exists;
    return RowStatus::Skipped;
}

ExtractSummary ManipEditor::Summary() const
{
    ExtractSummary summary;
    summary.found = mRows.size();
    for (std::size_t row = 0; row < mRows.size(); ++row) {
        switch (Status(row)) {
            case RowStatus::Added:      ++summary.added;      break;
            case RowStatus::Overridden: ++summary.overridden; break;
            case RowStatus::Exists:     ++summary.exists;     break;
            case RowStatus::Saved:      ++summary.saved;      break;
            case RowStatus::Skipped:    ++summary.skipped;    break;
        }
    }
    return summary;
}

std::size_t ManipEditor::ChangeCount() const
{
    return static_cast<std::size_t>(std::count_if(mRows.begin(), mRows.end(), [](const RowEntry& row) { return row.ticked; }));
}

std::vector<std::string> ManipEditor::ChangeLines() const
{
    std::vector<std::string> lines;
    for (const RowEntry& row : mRows) {
        if (row.ticked)
            lines.push_back(std::format("{}: {}", InFile(row.manip.Key()) ? "Override" : "Add", row.manip.DisplayName()));
    }
    return lines;
}

void ManipEditor::CancelChanges()
{
    const std::size_t changes = ChangeCount();
    for (RowEntry& row : mRows) {
        if (row.ticked && !InFile(row.manip.Key()))
            mUnticked.insert(row.manip.Key());
        row.ticked = false;
    }
    if (changes > 0)
        LOG_INFO("{} unsaved manipulator changes cancelled", changes);
}

std::vector<std::string> ManipEditor::BlockLines(const ObjManip& manip, bool withComment)
{
    std::vector<std::string> lines;
    if (withComment) {
        lines.push_back(std::format("# {} - {}, extracted by VRConfig Helper", manip.DisplayName(),
                                    manip.objFile.filename().string()));
    }
    lines.push_back("BEGIN_MANIP " + manip.Key());
    for (const std::string& line : manip.body)
        lines.push_back(kIndent + line);
    lines.push_back("END_MANIP");
    return lines;
}

// --- save -------------------------------------------------------------------------------

bool ManipEditor::ChangedOnDisk() const
{
    std::error_code ec;
    if (!mFileOnDisk)
        return fs::exists(mFile.FilePath(), ec);        // created by someone else meanwhile
    return ReadAll(mFile.FilePath()) != mLoadedText;
}

SaveResult ManipEditor::Save(bool overwriteOutsideChanges)
{
    mLastError.clear();
    if (!mOpen || ChangeCount() == 0)
        return SaveResult::NothingToSave;

    if (!overwriteOutsideChanges && ChangedOnDisk()) {
        mLastError = "the file was changed outside VRConfig Helper since it was read";
        LOG_WARN("{}: {}", mFile.FilePath().string(), mLastError);
        return SaveResult::ChangedOnDisk;
    }
    if (overwriteOutsideChanges && mFileOnDisk && !mFile.Load(mFile.FilePath())) {
        mLastError = mFile.LastError();                // build on the file as it is now
        return SaveResult::Failed;
    }

    // Backup once per X-Plane run: the file exactly as it is on disk now.
    const std::string backupKey = BackupPath().string();
    if (mFileOnDisk && !BackedUpThisRun().contains(backupKey)) {
        if (!CopyFileSafely(mFile.FilePath(), BackupPath(), mLastError)) {
            LOG_ERROR("backup failed, nothing saved: {}", mLastError);
            return SaveResult::Failed;
        }
        BackedUpThisRun().insert(backupKey);
        LOG_INFO("backup written: {}", BackupPath().string());
    }

    // New blocks go to the end of the file sorted by group (first reference's path),
    // with "# --- <group> ---" before each group; changed blocks stay where they are.
    std::vector<const RowEntry*> appended;
    for (const RowEntry& pending : mRows) {
        if (!pending.ticked)
            continue;
        const std::string key = pending.manip.Key();
        std::optional<std::size_t> existing;
        if (InFile(key)) {
            const auto& records = mFile.Manips();
            for (std::size_t i = 0; i < records.size(); ++i) {
                if (ManipKey(records[i].type, records[i].refs) == key &&
                    records[i].endIdx != vrconfig::ManipRecord::kNoLine) {
                    existing = i;
                    break;
                }
            }
        }
        if (existing)
            mFile.ReplaceManip(*existing, BlockLines(pending.manip, false));
        else
            appended.push_back(&pending);
    }
    std::stable_sort(appended.begin(), appended.end(), [](const RowEntry* first, const RowEntry* second) {
        return ManipGroup(first->manip.refs) < ManipGroup(second->manip.refs);
    });
    std::optional<std::string> lastGroup;
    for (const RowEntry* pending : appended) {
        std::vector<std::string> lines = BlockLines(pending->manip, true);
        const std::string group = ManipGroup(pending->manip.refs);
        if (!lastGroup || *lastGroup != group) {
            if (!group.empty())
                lines.insert(lines.begin(), GroupComment(group));
            lastGroup = group;
        }
        mFile.AppendBlock(lines);
    }

    if (!mFile.Save()) {
        mLastError = mFile.LastError();
        LOG_ERROR("{}", mLastError);
        if (mFileOnDisk)
            mFile.Load(mFile.FilePath());              // drop the half-applied edits
        else
            mFile.CreateNew(mFile.FilePath());
        return SaveResult::Failed;
    }

    for (RowEntry& row : mRows) {
        if (!row.ticked)
            continue;
        LOG_INFO("saved {} '{}': {}", InFile(row.manip.Key()) ? "override" : "new block", row.manip.DisplayName(),
                 row.manip.Key());
        mSavedKeys.insert(row.manip.Key());
        row.ticked = false;
    }

    // Read the file again: line numbers and keys are fresh.
    const fs::path path = mFile.FilePath();
    if (!mFile.Load(path)) {
        mLastError = mFile.LastError();
        return SaveResult::Failed;
    }
    mFileOnDisk = true;
    mLoadedText = mFile.Text();
    ReadKeys();
    mLastSave = Clock::now();
    return SaveResult::Saved;
}

// --- backup -----------------------------------------------------------------------------

fs::path ManipEditor::BackupPath() const
{
    fs::path backup = mFile.FilePath();
    backup += ".manip.bak";
    return backup;
}

bool ManipEditor::HasBackup() const
{
    std::error_code ec;
    return mOpen && mFileOnDisk && fs::exists(BackupPath(), ec);
}

bool ManipEditor::BackupMadeThisRun() const
{
    return mOpen && BackedUpThisRun().contains(BackupPath().string());
}

std::optional<ManipEditor::Clock::time_point> ManipEditor::BackupTime() const
{
    std::error_code ec;
    if (!HasBackup())
        return std::nullopt;
    const auto fileTime = fs::last_write_time(BackupPath(), ec);
    if (ec)
        return std::nullopt;
    return std::chrono::clock_cast<Clock>(fileTime);
}

bool ManipEditor::RestoreBackup()
{
    if (!HasBackup()) {
        mLastError = "there is no backup to restore";
        return false;
    }
    const fs::path file = mFile.FilePath();
    if (!CopyFileSafely(BackupPath(), file, mLastError)) {
        LOG_ERROR("restore failed: {}", mLastError);
        return false;
    }
    LOG_INFO("restored {} from its manipulator backup", file.string());
    if (ChangeCount() > 0)
        LOG_INFO("{} unsaved manipulator changes dropped by the restore", ChangeCount());
    std::vector<RowEntry> rows = mRows;
    Discard();                                         // re-read, keep the report (unticked)
    const OpenResult opened = Open(file);
    for (RowEntry& row : rows)
        row.ticked = false;
    mRows = std::move(rows);
    return opened == OpenResult::Opened;
}

} // namespace hpvr::manip

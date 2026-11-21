// hpvr - edit session of "VRConfig Manipulator Updater"

#include "ManipUpdater.h"

#include <format>
#include <system_error>

#include "Log.h"
#include "manip/ManipBlock.h"
#include "util/FileUtil.h"

namespace hpvr::manip {

namespace fs = std::filesystem;

namespace {

// Files backed up in this X-Plane run (plugin lifetime); separate from the other backups.
std::set<std::string>& BackedUpThisRun()
{
    static std::set<std::string> files;
    return files;
}

} // namespace

void ManipUpdater::ResetRunState()
{
    BackedUpThisRun().clear();
}

std::string ManipUpdater::BlockName(const vrconfig::ManipRecord& record)
{
    if (!record.tooltip.empty())
        return record.tooltip;
    if (record.refs.empty())
        return record.type;
    const std::string& ref = record.refs.front();
    const std::size_t slash = ref.rfind('/');
    return slash == std::string::npos ? ref : ref.substr(slash + 1);
}

std::string ManipUpdater::Identity(const vrconfig::ManipRecord& record) const
{
    std::string identity = record.type;
    for (const std::string& ref : record.refs)
        identity += " " + ref;
    if (!record.tooltip.empty())
        identity += " | " + record.tooltip;
    return identity;
}

// --- open / discard ---------------------------------------------------------------------------

UpdaterOpen ManipUpdater::Open(const fs::path& vrconfigPath, bool forLoadedAircraft)
{
    if (mOpen && mFile.FilePath() == vrconfigPath && ChangeCount() > 0) {
        mForLoadedAircraft = forLoadedAircraft;
        LOG_INFO("keeping {} unsaved manipulator updates of {}", ChangeCount(), vrconfigPath.filename().string());
        return UpdaterOpen::KeptChanges;
    }
    const bool samePath = mOpen && mFile.FilePath() == vrconfigPath;
    std::set<std::string> saved = samePath ? mSavedIdentities : std::set<std::string>{};
    const auto lastSave = samePath ? mLastSave : std::nullopt;
    Discard();
    mSavedIdentities = std::move(saved);
    mLastSave = lastSave;
    mForLoadedAircraft = forLoadedAircraft;

    std::error_code ec;
    if (vrconfigPath.empty() || !fs::exists(vrconfigPath, ec)) {
        mLastError = std::format("no vrconfig file: {}", vrconfigPath.string());
        LOG_INFO("{}", mLastError);
        return UpdaterOpen::NoFile;
    }
    if (!mFile.Load(vrconfigPath)) {
        mLastError = mFile.LastError();
        return UpdaterOpen::ReadError;
    }
    mLoadedText = mFile.Text();
    mOpen = true;
    LOG_INFO("{}: {} manipulator blocks", vrconfigPath.filename().string(), mFile.Manips().size());
    return UpdaterOpen::Opened;
}

void ManipUpdater::Discard()
{
    if (mOpen && ChangeCount() > 0)
        LOG_INFO("{} unsaved manipulator updates of {} discarded", ChangeCount(), mFile.FilePath().filename().string());
    mFile = vrconfig::VrConfigFile{};
    mLoadedText.clear();
    mPending.clear();
    mSavedIdentities.clear();
    mOpen = false;
    mForLoadedAircraft = false;
    mLastSave.reset();
    mLastError.clear();
}

// --- blocks -------------------------------------------------------------------------------------

bool ManipUpdater::CanEdit(std::size_t block) const
{
    return block < BlockCount() && Record(block).endIdx != vrconfig::ManipRecord::kNoLine;
}

std::vector<std::string> ManipUpdater::SavedLines(std::size_t block) const
{
    return mFile.ManipLines(block);
}

std::vector<std::string> ManipUpdater::CurrentLines(std::size_t block) const
{
    if (const auto found = mPending.find(block); found != mPending.end() && !found->second.removed)
        return found->second.lines;
    return SavedLines(block);
}

void ManipUpdater::SetBlock(std::size_t block, const std::vector<std::string>& lines, bool handEdited)
{
    if (!CanEdit(block))
        return;
    if (lines == SavedLines(block)) {
        if (mPending.erase(block) > 0)
            LOG_INFO("'{}' back as saved", BlockName(Record(block)));
        return;
    }
    mPending[block] = Pending{lines, false, handEdited};
    LOG_INFO("'{}' (line {}) changed{}", BlockName(Record(block)), Record(block).lineNumber,
             handEdited ? " by hand" : "");
}

void ManipUpdater::RemoveBlock(std::size_t block)
{
    if (!CanEdit(block))
        return;
    mPending[block] = Pending{{}, true, false};
    LOG_INFO("'{}' (line {}) marked for removal", BlockName(Record(block)), Record(block).lineNumber);
}

void ManipUpdater::Revert(std::size_t block)
{
    if (mPending.erase(block) > 0 && block < BlockCount())
        LOG_INFO("'{}' reverted", BlockName(Record(block)));
}

bool ManipUpdater::IsRemoved(std::size_t block) const
{
    const auto found = mPending.find(block);
    return found != mPending.end() && found->second.removed;
}

bool ManipUpdater::IsHandEdited(std::size_t block) const
{
    const auto found = mPending.find(block);
    return found != mPending.end() && found->second.handEdited;
}

bool ManipUpdater::SavedThisRun(std::size_t block) const
{
    return block < BlockCount() && !IsChanged(block) && mSavedIdentities.contains(Identity(Record(block)));
}

std::vector<std::string> ManipUpdater::ChangeLines() const
{
    std::vector<std::string> lines;
    for (const auto& [block, pending] : mPending) {
        lines.push_back(std::format("{}: {} (line {})", pending.removed ? "Remove" : "Change",
                                    BlockName(Record(block)), Record(block).lineNumber));
    }
    return lines;
}

void ManipUpdater::CancelChanges()
{
    if (!mPending.empty())
        LOG_INFO("{} unsaved manipulator updates cancelled", mPending.size());
    mPending.clear();
}

// --- save ------------------------------------------------------------------------------------------

bool ManipUpdater::ChangedOnDisk() const
{
    return util::ReadAll(mFile.FilePath()) != mLoadedText;
}

SaveResult ManipUpdater::Save(bool overwriteOutsideChanges)
{
    mLastError.clear();
    if (!mOpen || mPending.empty())
        return SaveResult::NothingToSave;
    if (!overwriteOutsideChanges && ChangedOnDisk()) {
        mLastError = "the file was changed outside VRConfig Helper since it was read";
        LOG_WARN("{}: {}", mFile.FilePath().string(), mLastError);
        return SaveResult::ChangedOnDisk;
    }

    // Save anyway: the changes are applied to the file as it is now, matched by
    // the block's identity (its index may have moved).
    std::map<std::size_t, Pending> pending = mPending;
    if (overwriteOutsideChanges) {
        std::vector<std::pair<std::string, Pending>> byIdentity;
        for (const auto& [block, change] : mPending)
            byIdentity.emplace_back(Identity(Record(block)), change);
        if (!mFile.Load(mFile.FilePath())) {
            mLastError = mFile.LastError();
            return SaveResult::Failed;
        }
        pending.clear();
        std::size_t lost = 0;
        for (const auto& [identity, change] : byIdentity) {
            bool found = false;
            for (std::size_t block = 0; block < BlockCount() && !found; ++block) {
                if (Identity(Record(block)) == identity && CanEdit(block) && !pending.contains(block)) {
                    pending[block] = change;
                    found = true;
                }
            }
            lost += found ? 0 : 1;
        }
        if (lost > 0)
            LOG_WARN("{} changed blocks are no longer in the file - not saved", lost);
    }

    const std::string backupKey = BackupPath().string();
    if (!BackedUpThisRun().contains(backupKey)) {
        if (!util::CopyFileSafely(mFile.FilePath(), BackupPath(), mLastError)) {
            LOG_ERROR("backup failed, nothing saved: {}", mLastError);
            return SaveResult::Failed;
        }
        BackedUpThisRun().insert(backupKey);
        LOG_INFO("backup written: {}", BackupPath().string());
    }

    // Last block first, so the indexes of the earlier ones stay valid.
    std::vector<std::string> savedIdentities;
    for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
        const auto& [block, change] = *it;
        const std::string name = BlockName(Record(block));
        bool done = false;
        if (change.removed) {
            done = mFile.RemoveManip(block);
        } else {
            std::string error;
            if (const auto parsed = ParseBlock(change.lines, error)) {
                savedIdentities.push_back(parsed->Identity());
                done = mFile.ReplaceManip(block, change.lines);
            } else {
                mLastError = error;
            }
        }
        if (!done) {
            if (mLastError.empty())
                mLastError = std::format("cannot write '{}'", name);
            LOG_ERROR("{}", mLastError);
            mFile.Load(mFile.FilePath());                        // drop the half-applied edits
            return SaveResult::Failed;
        }
        LOG_INFO("saved {}'{}'", change.removed ? "the removal of " : "", name);
    }
    if (!mFile.Save()) {
        mLastError = mFile.LastError();
        mFile.Load(mFile.FilePath());
        return SaveResult::Failed;
    }

    const fs::path path = mFile.FilePath();
    if (!mFile.Load(path)) {
        mLastError = mFile.LastError();
        return SaveResult::Failed;
    }
    mLoadedText = mFile.Text();
    mPending.clear();
    mSavedIdentities.insert(savedIdentities.begin(), savedIdentities.end());
    mLastSave = Clock::now();
    return SaveResult::Saved;
}

// --- backup ----------------------------------------------------------------------------------------

fs::path ManipUpdater::BackupPath() const
{
    fs::path backup = mFile.FilePath();
    backup += ".manip.upd.bak";
    return backup;
}

bool ManipUpdater::HasBackup() const
{
    std::error_code ec;
    return mOpen && fs::exists(BackupPath(), ec);
}

bool ManipUpdater::BackupMadeThisRun() const
{
    return mOpen && BackedUpThisRun().contains(BackupPath().string());
}

std::optional<ManipUpdater::Clock::time_point> ManipUpdater::BackupTime() const
{
    std::error_code ec;
    if (!HasBackup())
        return std::nullopt;
    const auto fileTime = fs::last_write_time(BackupPath(), ec);
    if (ec)
        return std::nullopt;
    return std::chrono::clock_cast<Clock>(fileTime);
}

bool ManipUpdater::RestoreBackup()
{
    if (!HasBackup()) {
        mLastError = "there is no backup to restore";
        return false;
    }
    const fs::path file = mFile.FilePath();
    if (!util::CopyFileSafely(BackupPath(), file, mLastError)) {
        LOG_ERROR("restore failed: {}", mLastError);
        return false;
    }
    LOG_INFO("restored {} from its updater backup", file.string());
    if (ChangeCount() > 0)
        LOG_INFO("{} unsaved manipulator updates dropped by the restore", ChangeCount());
    const bool forLoaded = mForLoadedAircraft;
    Discard();
    return Open(file, forLoaded) == UpdaterOpen::Opened;
}

} // namespace hpvr::manip

// hpvr - edit session for the teleport spots of one vrconfig file

#include "SpotEditor.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

#include "Log.h"
#include "util/FileUtil.h"

namespace hpvr::hotspot {

namespace fs = std::filesystem;
using util::CopyFileSafely;
using util::ReadAll;

namespace {

// Teleport box used when no other spot can serve as a model: offsets from the
// spot position (the head), in meters. Chosen for this plugin: a 0.6 x 0.6 m
// area 0.45..0.75 m below the eyes. The spot type (SITTING / STANDING) is
// metadata and does not change it.
constexpr Vec3 kDefaultBoxMin {-0.30, -0.75, -0.30};
constexpr Vec3 kDefaultBoxMax { 0.30, -0.45,  0.30};

// Files backed up in this X-Plane run (plugin lifetime).
std::set<std::string>& BackedUpThisRun()
{
    static std::set<std::string> files;
    return files;
}

double WrapHeading(double deg)
{
    deg = std::fmod(deg, 360.0);
    if (deg < 0.0)
        deg += 360.0;
    if (deg >= 359.99995)
        deg = 0.0;
    return deg;
}

double WrapRoll(double deg)
{
    deg = std::fmod(deg + 180.0, 360.0);
    if (deg < 0.0)
        deg += 360.0;
    return deg - 180.0;                      // -180 .. <180
}

double ClampPitch(double deg)
{
    return std::clamp(deg, -90.0, 90.0);
}

Vec3 Add(const Vec3& first, const Vec3& second)
{
    return {first.x + second.x, first.y + second.y, first.z + second.z};
}
Vec3 Sub(const Vec3& first, const Vec3& second)
{
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

std::string CleanName(const std::string& name)
{
    std::string clean;
    for (char character : name)       // must stay on one line
        clean += (character == '\r' || character == '\n' || character == '\t') ? ' ' : character;
    const auto first = clean.find_first_not_of(' ');
    if (first == std::string::npos)
        return {};
    const auto last = clean.find_last_not_of(' ');
    return clean.substr(first, last - first + 1);
}

} // namespace

const char* SpotFieldLabel(SpotField field)
{
    switch (field) {
        case SpotField::X:       return "X (right)";
        case SpotField::Y:       return "Y (up)";
        case SpotField::Z:       return "Z (aft)";
        case SpotField::Heading: return "Heading (PSI)";
        case SpotField::Pitch:   return "Pitch (THE)";
        case SpotField::Roll:    return "Roll / tilt (PHI)";
    }
    return "";
}

int SpotFieldDecimals(SpotField field)
{
    return (field == SpotField::X || field == SpotField::Y || field == SpotField::Z) ? 4 : 1;
}

double GetSpotField(const TeleportSpot& spot, SpotField field)
{
    switch (field) {
        case SpotField::X:       return spot.position.x;
        case SpotField::Y:       return spot.position.y;
        case SpotField::Z:       return spot.position.z;
        case SpotField::Heading: return spot.heading;
        case SpotField::Pitch:   return spot.pitch;
        case SpotField::Roll:    return spot.roll;
    }
    return 0.0;
}

std::string ClockTime(SpotEditor::Clock::time_point when)
{
    const std::time_t asTimeT = SpotEditor::Clock::to_time_t(when);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &asTimeT);
#else
    localtime_r(&asTimeT, &local);
#endif
    return std::format("{:02}:{:02}", local.tm_hour, local.tm_min);
}

void SpotEditor::ResetRunState()
{
    BackedUpThisRun().clear();
}

// --- open / discard -------------------------------------------------------------------

OpenResult SpotEditor::Open(const fs::path& vrconfigPath)
{
    if (mOpen && mFile.FilePath() == vrconfigPath && ChangeCount() > 0) {
        LOG_INFO("keeping {} unsaved changes of {}", ChangeCount(), vrconfigPath.filename().string());
        return OpenResult::KeptChanges;
    }

    Discard();
    std::error_code ec;
    if (vrconfigPath.empty() || !fs::exists(vrconfigPath, ec)) {
        mLastError = std::format("no vrconfig file: {}", vrconfigPath.string());
        LOG_INFO("{}", mLastError);
        return OpenResult::NoFile;
    }
    if (!mFile.Load(vrconfigPath)) {
        mLastError = mFile.LastError();
        return OpenResult::ReadError;
    }

    mFileOnDisk = true;
    mLoadedText = mFile.Text();
    RebuildSlots();
    mOpen = true;
    LoadBackupView();
    return OpenResult::Opened;
}

void SpotEditor::StartNewFile(const fs::path& vrconfigPath)
{
    Discard();
    mFile.CreateNew(vrconfigPath);
    mFileOnDisk = false;
    mLoadedText.clear();
    mSlots.clear();
    mOpen = true;
}

void SpotEditor::Discard()
{
    if (mOpen && ChangeCount() > 0)
        LOG_INFO("{} unsaved changes in {} spots of {} discarded", ChangeCount(),
                 ChangedSpotCount(), mFile.FilePath().filename().string());
    mFile = vrconfig::VrConfigFile{};
    mSlots.clear();
    mLoadedText.clear();
    mBackupView = vrconfig::VrConfigFile{};
    mBackupViewLoaded = false;
    mOpen = false;
    mFileOnDisk = false;
    mLastSave.reset();
    mLastError.clear();
}

void SpotEditor::RebuildSlots()
{
    mSlots.clear();
    for (std::size_t i = 0; i < mFile.Spots().size(); ++i)
        mSlots.push_back(Slot{i, mFile.Spots()[i].spot, false, false});
}

// --- spot access ----------------------------------------------------------------------

const SpotRecord* SpotEditor::Record(std::size_t spotIndex) const
{
    if (spotIndex >= mSlots.size() || !mSlots[spotIndex].fileIndex)
        return nullptr;
    return &mFile.Spots()[*mSlots[spotIndex].fileIndex];
}

const TeleportSpot* SpotEditor::SavedSpot(std::size_t spotIndex) const
{
    const SpotRecord* rec = Record(spotIndex);
    return rec ? &rec->spot : nullptr;
}

bool SpotEditor::IsEditable(std::size_t spotIndex) const
{
    if (spotIndex >= mSlots.size() || mSlots[spotIndex].deleted)
        return false;
    const SpotRecord* rec = Record(spotIndex);
    return rec == nullptr || rec->editable || mSlots[spotIndex].commasFixed;   // new spots are always editable
}

bool SpotEditor::CommasFixable(std::size_t spotIndex) const
{
    if (spotIndex >= mSlots.size() || mSlots[spotIndex].deleted || mSlots[spotIndex].commasFixed)
        return false;
    const SpotRecord* rec = Record(spotIndex);
    return rec != nullptr && rec->commasOnly;
}

void SpotEditor::FixCommas(std::size_t spotIndex)
{
    if (!CommasFixable(spotIndex))
        return;
    mSlots[spotIndex].commasFixed = true;
    mSlots[spotIndex].spot = Record(spotIndex)->spot;         // the values read as if the commas were spaces
    LOG_INFO("'{}': commas are removed on Save", mSlots[spotIndex].spot.name);
}

// --- editing --------------------------------------------------------------------------

void SpotEditor::Move(std::size_t spotIndex, const Vec3& delta)
{
    if (!IsEditable(spotIndex))
        return;
    TeleportSpot& spot = mSlots[spotIndex].spot;
    spot.position = Add(spot.position, delta);
    spot.boxMin   = Add(spot.boxMin, delta);
    spot.boxMax   = Add(spot.boxMax, delta);
}

void SpotEditor::PlaceAt(std::size_t spotIndex, const Vec3& position, double heading)
{
    if (!IsEditable(spotIndex))
        return;
    Move(spotIndex, Sub(position, mSlots[spotIndex].spot.position));   // the box keeps its offset
    mSlots[spotIndex].spot.position = position;                // exact value, no rounding drift
    mSlots[spotIndex].spot.heading  = WrapHeading(heading);
    mSlots[spotIndex].spot.pitch    = 0.0;
    mSlots[spotIndex].spot.roll     = 0.0;
}

void SpotEditor::Turn(std::size_t spotIndex, double degrees)
{
    if (!IsEditable(spotIndex))
        return;
    mSlots[spotIndex].spot.heading = WrapHeading(mSlots[spotIndex].spot.heading + degrees);
}

void SpotEditor::SetHeading(std::size_t spotIndex, double degrees)
{
    if (!IsEditable(spotIndex))
        return;
    mSlots[spotIndex].spot.heading = WrapHeading(degrees);
}

void SpotEditor::SetField(std::size_t spotIndex, SpotField field, double value)
{
    if (!IsEditable(spotIndex))
        return;
    TeleportSpot& spot = mSlots[spotIndex].spot;
    switch (field) {
        case SpotField::X:       Move(spotIndex, {value - spot.position.x, 0.0, 0.0}); spot.position.x = value; break;
        case SpotField::Y:       Move(spotIndex, {0.0, value - spot.position.y, 0.0}); spot.position.y = value; break;
        case SpotField::Z:       Move(spotIndex, {0.0, 0.0, value - spot.position.z}); spot.position.z = value; break;
        case SpotField::Heading: spot.heading = WrapHeading(value); break;
        case SpotField::Pitch:   spot.pitch   = ClampPitch(value);  break;
        case SpotField::Roll:    spot.roll    = WrapRoll(value);    break;
    }
}

void SpotEditor::ChangeField(std::size_t spotIndex, SpotField field, double delta)
{
    if (IsEditable(spotIndex))
        SetField(spotIndex, field, GetSpotField(mSlots[spotIndex].spot, field) + delta);
}

void SpotEditor::SetValues(std::size_t spotIndex, const TeleportSpot& spot)
{
    if (!IsEditable(spotIndex))
        return;
    Move(spotIndex, Sub(spot.position, mSlots[spotIndex].spot.position));
    mSlots[spotIndex].spot.position = spot.position;
    mSlots[spotIndex].spot.heading  = WrapHeading(spot.heading);
    mSlots[spotIndex].spot.pitch    = ClampPitch(spot.pitch);
    mSlots[spotIndex].spot.roll     = WrapRoll(spot.roll);
}

void SpotEditor::Rename(std::size_t spotIndex, const std::string& name)
{
    if (!IsEditable(spotIndex))
        return;
    const std::string clean = CleanName(name);
    mSlots[spotIndex].spot.name = clean.empty() ? NextFreeName() : clean;
}

std::string SpotEditor::NextFreeName() const
{
    for (std::size_t number = mSlots.size() + 1;; ++number) {
        const std::string name = std::format("Hotspot {}", number);
        if (!NameInUse(name))
            return name;
    }
}

bool SpotEditor::NameInUse(const std::string& name, std::optional<std::size_t> except) const
{
    for (std::size_t i = 0; i < mSlots.size(); ++i) {
        if (except && *except == i)
            continue;
        if (!mSlots[i].deleted && mSlots[i].spot.name == name)
            return true;
    }
    return false;
}

std::size_t SpotEditor::AddSpot(const NewSpotRequest& request)
{
    TeleportSpot spot;
    spot.kind = request.kind;

    if (request.position) {
        spot.position = *request.position;
        spot.heading  = WrapHeading(request.heading);

        // Teleport box: same shape as an existing spot - the template, then any
        // spot of the same kind, then any spot - else the default.
        std::optional<std::size_t> model;
        auto lmbda_usable = [&](std::size_t spotIndex, bool sameKind) {
            return spotIndex < mSlots.size() && IsEditable(spotIndex) &&
                   (!sameKind || mSlots[spotIndex].spot.kind == request.kind);
        };
        if (request.boxTemplate && lmbda_usable(*request.boxTemplate, true))
            model = request.boxTemplate;
        for (int pass = 0; pass < 2 && !model; ++pass) {
            for (std::size_t i = 0; !model && i < mSlots.size(); ++i) {
                if (lmbda_usable(i, pass == 0))
                    model = i;
            }
        }
        if (model) {
            const TeleportSpot& modelSpot = mSlots[*model].spot;
            spot.boxMin = Add(spot.position, Sub(modelSpot.boxMin, modelSpot.position));
            spot.boxMax = Add(spot.position, Sub(modelSpot.boxMax, modelSpot.position));
        } else {
            spot.boxMin = Add(spot.position, kDefaultBoxMin);
            spot.boxMax = Add(spot.position, kDefaultBoxMax);
        }
    } else if (request.copyOf && *request.copyOf < mSlots.size()) {
        const TeleportSpot& src = mSlots[*request.copyOf].spot;
        spot.position = src.position;
        spot.boxMin   = src.boxMin;
        spot.boxMax   = src.boxMax;
        spot.heading  = src.heading;               // pitch and roll stay 0 (neutral)
    }

    const std::string clean = CleanName(request.name);
    spot.name = clean.empty() ? NextFreeName() : clean;

    mSlots.push_back(Slot{std::nullopt, spot, false, false});
    LOG_INFO("new spot '{}' ({}) at x={:.4f} y={:.4f} z={:.4f}", spot.name,
             vrconfig::KindKeyword(spot.kind), spot.position.x, spot.position.y, spot.position.z);
    return mSlots.size() - 1;
}

bool SpotEditor::Delete(std::size_t spotIndex)
{
    if (spotIndex >= mSlots.size())
        return false;
    if (IsNew(spotIndex)) {
        LOG_INFO("new spot '{}' removed", mSlots[spotIndex].spot.name);
        mSlots.erase(mSlots.begin() + static_cast<std::ptrdiff_t>(spotIndex));
        return true;
    }
    if (Record(spotIndex)->endIdx == SpotRecord::kNoLine)
        return false;                                  // block without END: cannot be cut out safely
    mSlots[spotIndex].deleted = true;
    LOG_INFO("'{}' marked for deletion", SavedSpot(spotIndex)->name);
    return false;
}

void SpotEditor::UndoDelete(std::size_t spotIndex)
{
    if (spotIndex < mSlots.size())
        mSlots[spotIndex].deleted = false;
}

bool SpotEditor::Revert(std::size_t spotIndex)
{
    if (spotIndex >= mSlots.size())
        return false;
    if (IsNew(spotIndex)) {
        mSlots.erase(mSlots.begin() + static_cast<std::ptrdiff_t>(spotIndex));
        return true;
    }
    mSlots[spotIndex].deleted     = false;
    mSlots[spotIndex].commasFixed = false;
    mSlots[spotIndex].spot        = *SavedSpot(spotIndex);
    return false;
}

void SpotEditor::RevertAll()
{
    LOG_INFO("{} unsaved changes in {} spots cancelled", ChangeCount(), ChangedSpotCount());
    RebuildSlots();
}

// --- state ------------------------------------------------------------------------------

bool SpotEditor::IsChanged(std::size_t spotIndex) const
{
    if (spotIndex >= mSlots.size())
        return false;
    if (IsNew(spotIndex) || mSlots[spotIndex].deleted || mSlots[spotIndex].commasFixed)
        return true;
    return !vrconfig::SameSpot(mSlots[spotIndex].spot, *SavedSpot(spotIndex));
}

std::size_t SpotEditor::ChangedSpotCount() const
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < mSlots.size(); ++i)
        count += IsChanged(i) ? 1 : 0;
    return count;
}

std::vector<SpotChanges> SpotEditor::Changes() const
{
    std::vector<SpotChanges> out;
    for (std::size_t i = 0; i < mSlots.size(); ++i) {
        if (!IsChanged(i))
            continue;
        SpotChanges change;
        change.index = i;
        if (IsNew(i)) {
            change.name = mSlots[i].spot.name;
            change.what.emplace_back("new");
        } else if (mSlots[i].deleted) {
            change.name = SavedSpot(i)->name;
            change.what.emplace_back("delete");
        } else {
            const TeleportSpot& now = mSlots[i].spot;
            const TeleportSpot& was = *SavedSpot(i);
            change.name = was.name;
            if (mSlots[i].commasFixed)             change.what.emplace_back("commas removed");
            if (!vrconfig::SamePosition(now, was)) change.what.emplace_back("position");
            if (!vrconfig::SameHeading(now, was))  change.what.emplace_back("heading");
            if (!vrconfig::SameTilt(now, was))     change.what.emplace_back("pitch/roll");
            if (!vrconfig::SameName(now, was))     change.what.emplace_back(std::format("renamed to \"{}\"", now.name));
        }
        out.push_back(std::move(change));
    }
    return out;
}

std::size_t SpotEditor::ChangeCount() const
{
    std::size_t count = 0;
    for (const SpotChanges& change : Changes())
        count += change.what.size();
    return count;
}

bool SpotEditor::SavedThisRun(std::size_t spotIndex) const
{
    if (!mBackupViewLoaded || spotIndex >= mSlots.size() || IsNew(spotIndex))
        return false;
    const TeleportSpot& saved = *SavedSpot(spotIndex);
    for (const SpotRecord& backupRecord : mBackupView.Spots()) {
        if (vrconfig::SameSpot(saved, backupRecord.spot))
            return false;
    }
    return true;
}

std::optional<TeleportSpot> SpotEditor::BackupSpot(std::size_t spotIndex) const
{
    if (!mBackupViewLoaded || spotIndex >= mSlots.size() || IsNew(spotIndex))
        return std::nullopt;
    const std::string& name = SavedSpot(spotIndex)->name;
    for (const SpotRecord& backupRecord : mBackupView.Spots()) {
        if (backupRecord.spot.name == name)
            return backupRecord.spot;
    }
    return std::nullopt;
}

// --- save -------------------------------------------------------------------------------

bool SpotEditor::ChangedOnDisk() const
{
    std::error_code ec;
    if (!mFileOnDisk)
        return fs::exists(mFile.FilePath(), ec);        // created by someone else meanwhile
    return ReadAll(mFile.FilePath()) != mLoadedText;
}

SaveResult SpotEditor::Save(bool overwriteOutsideChanges)
{
    mLastError.clear();
    if (!mOpen || ChangeCount() == 0)
        return SaveResult::NothingToSave;

    if (!overwriteOutsideChanges && ChangedOnDisk()) {
        mLastError = "the file was changed outside VRConfig Helper since it was read";
        LOG_WARN("{}: {}", mFile.FilePath().string(), mLastError);
        return SaveResult::ChangedOnDisk;
    }

    // Backup once per X-Plane run: the file exactly as it is on disk now.
    const std::string key = mFile.FilePath().string();
    if (mFileOnDisk && !BackedUpThisRun().contains(key)) {
        if (!CopyFileSafely(mFile.FilePath(), BackupPath(), mLastError)) {
            LOG_ERROR("backup failed, nothing saved: {}", mLastError);
            return SaveResult::Failed;
        }
        BackedUpThisRun().insert(key);
        LOG_INFO("backup written: {}", BackupPath().string());
    }

    const std::vector<SpotChanges> changes = Changes();
    auto lmbda_fail = [&](const std::string& why) {
        mLastError = why;
        LOG_ERROR("{}", why);
        if (mFileOnDisk)
            mFile.Load(mFile.FilePath());              // drop half-applied line edits
        else
            mFile.CreateNew(mFile.FilePath());
        return SaveResult::Failed;
    };

    // 0. commas removed (same line count; the spot becomes editable in the file)
    for (std::size_t i = 0; i < mSlots.size(); ++i) {
        if (mSlots[i].commasFixed && !mSlots[i].deleted && mSlots[i].fileIndex &&
            !mFile.FixCommas(*mSlots[i].fileIndex))
            return lmbda_fail(std::format("cannot remove the commas of '{}'", SavedSpot(i)->name));
    }
    // 1. updates (same line count)
    for (std::size_t i = 0; i < mSlots.size(); ++i) {
        if (!IsNew(i) && !mSlots[i].deleted && IsChanged(i) &&
            !mFile.UpdateSpot(*mSlots[i].fileIndex, mSlots[i].spot))
            return lmbda_fail(std::format("cannot update '{}'", SavedSpot(i)->name));
    }
    // 2. deletions, last block first so earlier indexes stay valid
    std::vector<std::size_t> removed;
    for (const Slot& slot : mSlots) {
        if (slot.deleted && slot.fileIndex)
            removed.push_back(*slot.fileIndex);
    }
    std::sort(removed.rbegin(), removed.rend());
    for (std::size_t idx : removed) {
        if (!mFile.RemoveSpot(idx))
            return lmbda_fail(std::format("cannot delete spot {}", idx + 1));
    }
    // 3. new blocks, in the order they were added
    for (const Slot& slot : mSlots) {
        if (!slot.fileIndex)
            mFile.AppendSpot(slot.spot);
    }

    if (!mFile.Save())
        return lmbda_fail(mFile.LastError());

    for (const SpotChanges& change : changes) {
        std::string what;
        for (const std::string& whatChanged : change.what)
            what += (what.empty() ? "" : ", ") + whatChanged;
        LOG_INFO("saved '{}': {}", change.name, what);
    }

    // Read the file again: indexes, line numbers and saved values are fresh.
    const fs::path path = mFile.FilePath();
    if (!mFile.Load(path)) {
        mLastError = mFile.LastError();
        return SaveResult::Failed;
    }
    mFileOnDisk = true;
    mLoadedText = mFile.Text();
    RebuildSlots();
    mLastSave = Clock::now();
    LoadBackupView();
    return SaveResult::Saved;
}

// --- backup -----------------------------------------------------------------------------

fs::path SpotEditor::BackupPath() const
{
    fs::path backup = mFile.FilePath();
    backup += ".bak";
    return backup;
}

bool SpotEditor::HasBackup() const
{
    std::error_code ec;
    return mOpen && mFileOnDisk && fs::exists(BackupPath(), ec);
}

bool SpotEditor::BackupMadeThisRun() const
{
    return mOpen && BackedUpThisRun().contains(mFile.FilePath().string());
}

std::optional<SpotEditor::Clock::time_point> SpotEditor::BackupTime() const
{
    std::error_code ec;
    if (!HasBackup())
        return std::nullopt;
    const auto ft = fs::last_write_time(BackupPath(), ec);
    if (ec)
        return std::nullopt;
    return std::chrono::clock_cast<Clock>(ft);
}

bool SpotEditor::RestoreBackup()
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
    LOG_INFO("restored {} from its backup", file.string());
    if (ChangeCount() > 0)
        LOG_INFO("{} unsaved changes dropped by the restore", ChangeCount());
    mOpen = false;                                     // Open() must re-read, not keep changes
    return Open(file) == OpenResult::Opened;
}

void SpotEditor::LoadBackupView()
{
    mBackupView = vrconfig::VrConfigFile{};
    mBackupViewLoaded = BackupMadeThisRun() && mBackupView.Load(BackupPath());
}

} // namespace hpvr::hotspot

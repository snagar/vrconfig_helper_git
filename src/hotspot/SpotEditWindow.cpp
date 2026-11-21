// hotspot_vr - "Hotspot" screen

#include "SpotEditWindow.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

#include "imgui.h"
#include "IconsFontAwesome5.h"

#include "Log.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "util/FileUtil.h"

namespace hotspot_vr::hotspot {

namespace theme = ui::theme;

namespace {

constexpr double kStepMeters  = 0.01;   // one tap = 1 cm
constexpr double kStepDegrees = 1.0;    // one tap = 1 degree
constexpr float  kArrowW      = 92.0f;
constexpr float  kArrowH      = 40.0f;

const char* kExplainText =
    "- Your changes are kept until you press Save or Cancel changes, even if you close this window.\n"
    "- Save writes all changes to the aircraft's vrconfig file.\n"
    "- The first time you save an aircraft's hotspots in this X-Plane run, a backup of the original "
    "file is made. Later saves don't change it.\n"
    "- Restore backup brings back the file as it was before your first save in this run. In the next "
    "X-Plane run, your first save replaces the backup.\n"
    "- Cancel changes discards everything not yet saved; you will be asked to confirm.\n"
    "- Loading or reloading an aircraft discards changes that haven't been saved.\n"
    "- X-Plane reads the vrconfig file when the aircraft loads: reload the aircraft to try your new hotspots.";

std::string Plural(std::size_t n, const char* one, const char* many)
{
    return std::format("{} {}", n, n == 1 ? one : many);
}

void CenterNextPopup()
{
    const ImVec2 center(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() * 0.5f,
                        ImGui::GetWindowPos().y + ImGui::GetWindowHeight() * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetWindowWidth() * 0.8f, 0.0f),
                                        ImVec2(ImGui::GetWindowWidth() - 30.0f, ImGui::GetWindowHeight() - 30.0f));
}

void WrappedText(const std::string& text, const ImVec4* color = nullptr)
{
    if (color)
        ImGui::PushStyleColor(ImGuiCol_Text, *color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    if (color)
        ImGui::PopStyleColor();
}

} // namespace

SpotEditWindow::SpotEditWindow(SpotEditor& editor)
    : ImgWindow(WindowGeometry::Centered(kWidth, kHeight, xplm_WindowDecorationRoundRectangle),
                xplm_WindowDecorationRoundRectangle,
                xplm_WindowLayerFloatingWindows),
      mEditor(editor)
{
    SetWindowTitle("Hotspot VR - Hotspots");
    SetWindowResizingLimits(kWidth, 420, kWidth * 2, kHeight * 2);
    theme::ApplyBaseStyle();
}

ImGuiWindowFlags_ SpotEditWindow::beforeBegin()
{
    return ImGuiWindowFlags_None;
}

void SpotEditWindow::Reload()
{
    mRenaming = false;
    mError.clear();
    mAircraft = xp::GetUserAircraft();
    if (!mAircraft) {
        mEditor.Discard();
        mOpenResult = OpenResult::NoFile;
        return;
    }
    mOpenResult = mEditor.Open(mAircraft->vrconfigFile);
    if (mSelected >= mEditor.SpotCount())
        mSelected = 0;
    // start on the first editable spot
    if (mEditor.SpotCount() > 0 && !mEditor.Saved(mSelected).editable) {
        for (std::size_t i = 0; i < mEditor.SpotCount(); ++i) {
            if (mEditor.Saved(i).editable) { mSelected = i; break; }
        }
    }
    LOG_DEBUG("reloaded: {} spots, selected {}", mEditor.SpotCount(), mSelected);
}

void SpotEditWindow::SetNotice(const std::string& text)
{
    mNotice = text;
}

// --- frame ----------------------------------------------------------------------------

void SpotEditWindow::buildInterface()
{
    DrawHeader();
    ImGui::Separator();

    if (!mEditor.IsOpen() || mEditor.SpotCount() == 0) {
        DrawNotAvailable();
    } else {
        DrawSpotPicker();
        ImGui::Separator();
        DrawValueTable();
        DrawMoveControls();
        ImGui::Separator();
        DrawStatusAndActions();
    }
    DrawPopups();
}

void SpotEditWindow::DrawHeader()
{
    const char* explain = ICON_FA_INFO_CIRCLE " Explain";
    const float explainW = ImGui::CalcTextSize(explain).x + ImGui::GetStyle().FramePadding.x * 2.0f;

    ImGui::TextUnformatted(mAircraft ? mAircraft->displayName.c_str() : "No aircraft");
    ImGui::SameLine(ImGui::GetContentRegionMax().x - explainW);
    if (ui::Button(explain))
        mAskExplain = true;

    if (mAircraft) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDisabled);
        ImGui::TextUnformatted(util::Utf8(mAircraft->vrconfigFile.filename()).c_str());
        ImGui::PopStyleColor();
    }
}

void SpotEditWindow::DrawNotAvailable()
{
    ImGui::Spacing();
    if (!mAircraft) {
        WrappedText("X-Plane did not report a user aircraft.", &theme::kErrorText);
    } else if (mOpenResult == OpenResult::NoFile) {
        WrappedText(std::format("This aircraft has no vrconfig file.\nExpected: {}",
                                util::Utf8(mAircraft->vrconfigFile)));
        ImGui::SameLine();
        ui::HelpMarker("X-Plane reads teleport hotspots from <aircraft>_vrconfig.txt, "
                       "in the same folder as the .acf file.");
    } else if (mOpenResult == OpenResult::ReadError) {
        WrappedText(std::format(ICON_FA_EXCLAMATION_TRIANGLE " Cannot read the vrconfig file: {}",
                                mEditor.LastError()), &theme::kErrorText);
    } else {
        WrappedText("No teleport hotspots (BEGIN_TELEPORT_HOTSPOT) in this file.");
    }
    ImGui::Spacing();
    if (ui::Button(ICON_FA_SYNC " Read again"))
        Reload();
}

void SpotEditWindow::DrawSpotPicker()
{
    const std::size_t count = mEditor.SpotCount();
    mSelected = std::min(mSelected, count - 1);

    auto lmbda_marker = [this](std::size_t i, ImVec4& color) -> const char* {
        if (!mEditor.Saved(i).editable) { color = theme::kErrorText; return ICON_FA_EXCLAMATION_TRIANGLE " "; }
        if (mEditor.IsChanged(i))       { color = theme::kChangedText; return ICON_FA_CIRCLE " "; }
        if (mEditor.SavedThisRun(i))    { color = theme::kSavedText;   return ICON_FA_CHECK " "; }
        color = theme::kText;
        return "";
    };
    auto lmbda_label = [this](std::size_t i) {
        const std::string& n = mEditor.Edited(i).name;
        return n.empty() ? std::format("Hotspot {}", i + 1) : n;
    };

    const SpotRecord& rec = mEditor.Saved(mSelected);
    const char* kind = vrconfig::KindKeyword(mEditor.Edited(mSelected).kind);
    const float kindW = ImGui::CalcTextSize(kind).x + ImGui::CalcTextSize("(?)").x + 24.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Hotspot:");
    ImGui::SameLine();

    ImVec4 previewColor;
    const std::string preview = std::string(lmbda_marker(mSelected, previewColor)) + lmbda_label(mSelected);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - kindW);
    ImGui::PushStyleColor(ImGuiCol_Text, previewColor);
    const bool open = ImGui::BeginCombo("##spot", preview.c_str(), ImGuiComboFlags_HeightLarge);
    ImGui::PopStyleColor();
    if (open) {
        for (std::size_t i = 0; i < count; ++i) {
            ImVec4 color;
            const std::string item = std::format("{}{}##{}", lmbda_marker(i, color), lmbda_label(i), i);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            const ImGuiSelectableFlags flags = mEditor.Saved(i).editable ? 0 : ImGuiSelectableFlags_Disabled;
            if (ImGui::Selectable(item.c_str(), i == mSelected, flags)) {
                mSelected  = i;
                mRenaming  = false;
                LOG_DEBUG_AS("lmbda_selectSpot", "selected '{}'", lmbda_label(i));
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(kind);
    ImGui::SameLine();
    ui::HelpMarker("SITTING: the position is where the user's head will be.\n"
                   "STANDING: the position is where the user's feet will be.");

    if (!rec.editable) {
        WrappedText(std::format(ICON_FA_EXCLAMATION_TRIANGLE " Line {}: {}", rec.lineNumber, rec.problem),
                    &theme::kErrorText);
        return;
    }

    // Rename
    if (!mRenaming) {
        if (ui::Button(ICON_FA_PEN " Rename")) {
            mRenaming = true;
            mFocusRename = true;
            std::snprintf(mRenameText.data(), mRenameText.size(), "%s", mEditor.Edited(mSelected).name.c_str());
        }
    } else {
        const float buttonsW = ImGui::CalcTextSize("OK").x + ImGui::CalcTextSize("Cancel").x + 48.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonsW);
        if (mFocusRename) {
            ImGui::SetKeyboardFocusHere();
            mFocusRename = false;
        }
        const bool enter = ImGui::InputText("##rename", mRenameText.data(), mRenameText.size(),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ui::Button("OK") || enter) {
            mEditor.Rename(mSelected, mRenameText.data());
            mRenaming = false;
            mNotice.clear();
        }
        ImGui::SameLine();
        if (ui::Button("Cancel"))
            mRenaming = false;
    }
}

void SpotEditWindow::DrawValueTable()
{
    const SpotRecord& rec = mEditor.Saved(mSelected);
    if (!rec.editable)
        return;

    const TeleportSpot& saved  = rec.spot;
    TeleportSpot        edited = mEditor.Edited(mSelected);
    const std::optional<TeleportSpot> backup = mEditor.BackupSpot(mSelected);
    const bool showTilt = std::fabs(saved.pitch) > 1e-4 || std::fabs(saved.roll) > 1e-4 ||
                          std::fabs(edited.pitch) > 1e-4 || std::fabs(edited.roll) > 1e-4;

    ImGui::TextUnformatted("Position (meters) and heading (degrees)");
    ImGui::SameLine();
    ui::HelpMarker("Saved: what is in the file now. New: your edit.\n"
                   ICON_FA_CIRCLE " (orange) marks an unsaved change.\n"
                   "Green saved values were changed by a save in this X-Plane run.\n"
                   "X is right, Y is up, Z is toward the tail.");

    bool edit = false;
    auto lmbda_row = [&](const char* label, double savedValue, double& newValue,
                         std::optional<double> backupValue, const char* id, int decimals) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);

        ImGui::TableSetColumnIndex(1);
        const bool greenSaved = backupValue && std::fabs(*backupValue - savedValue) > 0.00005;
        ImGui::PushStyleColor(ImGuiCol_Text, greenSaved ? theme::kSavedText : theme::kText);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(std::format("{:.{}f}", savedValue, decimals).c_str());
        ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(2);
        const bool changed = std::fabs(newValue - savedValue) > 0.00005;
        ImGui::PushStyleColor(ImGuiCol_Text, changed ? theme::kChangedText : theme::kText);
        ImGui::SetNextItemWidth(-1.0f);
        const std::string printfFmt = std::format("%.{}f", decimals);
        if (ImGui::InputDouble(id, &newValue, 0.0, 0.0, printfFmt.c_str()))
            edit = true;
        ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(3);
        if (changed) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kChangedText);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(ICON_FA_CIRCLE);
            ImGui::PopStyleColor();
        }
    };

    const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##values", 4, flags)) {
        ImGui::TableSetupColumn("",      ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Saved", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("New",   ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("",      ImGuiTableColumnFlags_WidthFixed, 18.0f);
        ImGui::TableHeadersRow();

        auto lmbda_backup = [&](double TeleportSpot::*field) -> std::optional<double> {
            return backup ? std::optional<double>((*backup).*field) : std::nullopt;
        };
        auto lmbda_backupPos = [&](double vrconfig::Vec3::*axis) -> std::optional<double> {
            return backup ? std::optional<double>(backup->position.*axis) : std::nullopt;
        };

        lmbda_row("X (right)", saved.position.x, edited.position.x, lmbda_backupPos(&vrconfig::Vec3::x), "##x", 4);
        lmbda_row("Y (up)",    saved.position.y, edited.position.y, lmbda_backupPos(&vrconfig::Vec3::y), "##y", 4);
        lmbda_row("Z (aft)",   saved.position.z, edited.position.z, lmbda_backupPos(&vrconfig::Vec3::z), "##z", 4);
        lmbda_row("Heading",   saved.heading,    edited.heading,    lmbda_backup(&TeleportSpot::heading), "##psi", 1);
        if (showTilt) {
            lmbda_row("Pitch", saved.pitch, edited.pitch, lmbda_backup(&TeleportSpot::pitch), "##the", 1);
            lmbda_row("Roll",  saved.roll,  edited.roll,  lmbda_backup(&TeleportSpot::roll),  "##phi", 1);
        }
        ImGui::EndTable();
    }

    if (edit) {
        mEditor.SetValues(mSelected, edited);
        mNotice.clear();
    }
}

void SpotEditWindow::DrawMoveControls()
{
    const std::size_t i = mSelected;
    if (!mEditor.Saved(i).editable)
        return;

    ImGui::Spacing();
    const bool outside = xp::IsTeleportedOutside();
    if (ui::Button(ICON_FA_CROSSHAIRS "  Use my current head position",
                   ImVec2(ImGui::GetContentRegionAvail().x - 30.0f, 44.0f), !outside))
        UseHeadPosition();
    ImGui::SameLine();
    ui::HelpMarker(outside
        ? "Not available: you are teleported outside the aircraft. Teleport back inside first."
        : "Sit or stand where you want this hotspot, look in the direction it should face, "
          "then press. The position and heading are copied; pitch and roll are set to 0.\n"
          "STANDING hotspots keep their height (the floor), only the position across "
          "the cabin and the heading change.");

    ImGui::Spacing();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float startX  = ImGui::GetCursorPosX();
    const float upDownX = startX + 3.0f * (kArrowW + spacing) + 24.0f;

    // Row 1: Forward / Up
    ImGui::SetCursorPosX(startX + kArrowW + spacing);
    const float fwd = ui::HoldButton(ICON_FA_ARROW_UP " Forward", ImVec2(kArrowW, kArrowH));
    ImGui::SameLine(upDownX);
    const float up = ui::HoldButton(ICON_FA_CARET_UP " Up", ImVec2(kArrowW, kArrowH));
    ImGui::SameLine();
    ui::HelpMarker("Tap to move 1 cm. Hold to keep moving - the longer you hold, the faster it moves.\n"
                   "Directions are relative to the aircraft. The teleport box moves with the hotspot.");

    // Row 2: Left / Back / Right / Down
    ImGui::SetCursorPosX(startX);
    const float left = ui::HoldButton(ICON_FA_ARROW_LEFT " Left", ImVec2(kArrowW, kArrowH));
    ImGui::SameLine();
    const float back = ui::HoldButton(ICON_FA_ARROW_DOWN " Back", ImVec2(kArrowW, kArrowH));
    ImGui::SameLine();
    const float right = ui::HoldButton(ICON_FA_ARROW_RIGHT " Right", ImVec2(kArrowW, kArrowH));
    ImGui::SameLine(upDownX);
    const float down = ui::HoldButton(ICON_FA_CARET_DOWN " Down", ImVec2(kArrowW, kArrowH));

    // Heading
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Heading");
    ImGui::SameLine(startX + kArrowW + spacing);
    const float ccw = ui::HoldButton(ICON_FA_UNDO " Left", ImVec2(kArrowW, kArrowH));
    ImGui::SameLine();
    const float cw = ui::HoldButton(ICON_FA_REDO " Right", ImVec2(kArrowW, kArrowH));
    ImGui::SameLine();
    ui::HelpMarker("Turns the hotspot's facing direction: tap = 1 degree, hold to keep turning.\n"
                   "0 faces the nose, 90 the right wing, 180 the tail.");

    const Vec3 delta{(right - left) * kStepMeters, (up - down) * kStepMeters, (back - fwd) * kStepMeters};
    if (delta.x != 0.0 || delta.y != 0.0 || delta.z != 0.0) {
        mEditor.Move(i, delta);
        mNotice.clear();
    }
    if (cw != ccw) {
        mEditor.Turn(i, (cw - ccw) * kStepDegrees);
        mNotice.clear();
    }

    ImGui::Spacing();
    if (ui::Button(ICON_FA_UNDO_ALT " Revert this hotspot", ImVec2(0, 0), mEditor.IsChanged(i))) {
        LOG_INFO("reverted '{}'", mEditor.Saved(i).spot.name);
        mEditor.Revert(i);
    }
}

void SpotEditWindow::DrawStatusAndActions()
{
    const std::size_t changes = mEditor.ChangeCount();
    const std::size_t spots   = mEditor.ChangedSpotCount();

    if (!mError.empty()) {
        ui::StatusStrip(std::string(ICON_FA_EXCLAMATION_TRIANGLE " ") + mError, theme::kError, theme::kTextOnColor);
    } else if (changes > 0) {
        ui::StatusStrip(std::format(ICON_FA_CIRCLE " {} in {} - save before reloading",
                                    Plural(changes, "unsaved change", "unsaved changes"),
                                    Plural(spots, "hotspot", "hotspots")),
                        theme::kChanged, theme::kTextOnColor);
    } else if (!mNotice.empty()) {
        ui::StatusStrip(mNotice, theme::kPanelBgDark, theme::kText);
    } else if (mEditor.LastSaveTime()) {
        ui::StatusStrip(std::format(ICON_FA_CHECK " Saved at {} - reload the aircraft to use the new hotspots",
                                    ClockTime(*mEditor.LastSaveTime())),
                        theme::kSaved, theme::kTextOnColor);
    } else {
        ui::StatusStrip("No unsaved changes", theme::kPanelBgDark, theme::kText);
    }

    const float w = ui::FillWidth(3) - 10.0f;
    const ImVec2 size(w, 40.0f);
    if (ui::ColorButton(ICON_FA_SAVE " Save", size, theme::kChanged, theme::kChangedHover,
                        theme::kChangedActive, changes > 0))
        DoSave(false);
    ImGui::SameLine();
    if (ui::Button(ICON_FA_TIMES " Cancel changes", size, changes > 0))
        mAskCancel = true;
    ImGui::SameLine();
    if (ui::Button(ICON_FA_HISTORY " Restore backup", size, mEditor.HasBackup()))
        mAskRestore = true;
    ImGui::SameLine();
    if (const auto t = mEditor.BackupTime()) {
        const std::string help = std::format(
            "Backup of the original file, made at {}{}. Restoring discards all saves made since then, "
            "and any unsaved changes.", ClockTime(*t),
            mEditor.BackupMadeThisRun() ? " in this X-Plane run" : " in an earlier X-Plane run");
        ui::HelpMarker(help.c_str());
    } else {
        ui::HelpMarker("A backup is made the first time you save this aircraft's hotspots in this X-Plane run.");
    }
}

// --- actions --------------------------------------------------------------------------

void SpotEditWindow::UseHeadPosition()
{
    std::string reason;
    const auto pose = xp::GetHeadPose(mEditor.File(), reason);
    if (!pose) {
        mNotice = std::string(ICON_FA_EXCLAMATION_TRIANGLE " ") + reason;
        LOG_WARN("head position not used: {}", reason);
        return;
    }
    Vec3 position = pose->position;
    if (mEditor.Edited(mSelected).kind == SpotKind::Standing)
        position.y = mEditor.Edited(mSelected).position.y;     // feet stay on the floor
    mEditor.PlaceAt(mSelected, position, pose->heading);
    mNotice.clear();
    LOG_INFO("'{}' placed at head position x={:.4f} y={:.4f} z={:.4f} heading={:.1f}",
             mEditor.Edited(mSelected).name, position.x, position.y, position.z, pose->heading);
}

void SpotEditWindow::DoSave(bool overwriteOutsideChanges)
{
    mError.clear();
    switch (mEditor.Save(overwriteOutsideChanges)) {
        case SaveResult::Saved:
            mNotice.clear();
            break;
        case SaveResult::ChangedOnDisk:
            mAskOverwrite = true;
            break;
        case SaveResult::Failed:
            mError = "Save failed: " + mEditor.LastError();
            break;
        case SaveResult::NothingToSave:
            break;
    }
}

// --- popups ---------------------------------------------------------------------------

void SpotEditWindow::DrawPopups()
{
    constexpr const char* kExplainId   = "How changes are saved";
    constexpr const char* kCancelId    = "Discard unsaved changes?";
    constexpr const char* kRestoreId   = "Restore the backup?";
    constexpr const char* kOverwriteId = "File changed outside Hotspot VR";

    if (mAskExplain)   { ImGui::OpenPopup(kExplainId);   mAskExplain   = false; }
    if (mAskCancel)    { ImGui::OpenPopup(kCancelId);    mAskCancel    = false; }
    if (mAskRestore)   { ImGui::OpenPopup(kRestoreId);   mAskRestore   = false; }
    if (mAskOverwrite) { ImGui::OpenPopup(kOverwriteId); mAskOverwrite = false; }

    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;
    const ImVec2 buttonSize(140.0f, 36.0f);

    CenterNextPopup();
    if (ImGui::BeginPopupModal(kExplainId, nullptr, flags)) {
        WrappedText(kExplainText);
        ImGui::Spacing();
        if (ui::Button("Close", buttonSize))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    CenterNextPopup();
    if (ImGui::BeginPopupModal(kCancelId, nullptr, flags)) {
        WrappedText(std::format("Discard {}?", Plural(mEditor.ChangeCount(), "unsaved change", "unsaved changes")));
        for (const SpotChanges& c : mEditor.Changes()) {
            std::string what;
            for (const std::string& w : c.what)
                what += (what.empty() ? "" : ", ") + w;
            WrappedText(std::format("  - {}: {}", c.name.empty() ? "(no name)" : c.name, what));
        }
        ImGui::Spacing();
        if (ui::Button("Discard all", buttonSize)) {
            mEditor.RevertAll();
            mRenaming = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::Button("Keep", buttonSize))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    CenterNextPopup();
    if (ImGui::BeginPopupModal(kRestoreId, nullptr, flags)) {
        const auto t = mEditor.BackupTime();
        WrappedText(std::format("The vrconfig file will be replaced by its backup{}.",
                                t ? " from " + ClockTime(*t) : std::string()));
        WrappedText("All saves made since then are undone.");
        if (mEditor.ChangeCount() > 0)
            WrappedText(std::format("Your {} are discarded too.",
                                    Plural(mEditor.ChangeCount(), "unsaved change", "unsaved changes")),
                        &theme::kChangedText);
        ImGui::Spacing();
        if (ui::Button("Restore", buttonSize)) {
            mError.clear();
            if (mEditor.RestoreBackup())
                mNotice = ICON_FA_HISTORY " Backup restored - reload the aircraft to use it";
            else
                mError = "Restore failed: " + mEditor.LastError();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::Button("Keep current", buttonSize))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    CenterNextPopup();
    if (ImGui::BeginPopupModal(kOverwriteId, nullptr, flags)) {
        WrappedText("The vrconfig file was changed by another program after Hotspot VR read it.");
        WrappedText("Saving now replaces those outside changes with this file plus your edits.");
        ImGui::Spacing();
        if (ui::Button("Save anyway", buttonSize)) {
            ImGui::CloseCurrentPopup();
            DoSave(true);
        }
        ImGui::SameLine();
        if (ui::Button("Don't save", buttonSize))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace hotspot_vr::hotspot

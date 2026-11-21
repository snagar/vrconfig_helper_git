// hpvr - Hotspot page

#include "SpotPage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <utility>

#include "imgui.h"
#include "IconsFontAwesome5.h"

#include "Log.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "util/FileUtil.h"

namespace hpvr::hotspot {

namespace theme = ui::theme;
using ui::PopupAnswer;

namespace {

constexpr double kStepMeters       = 0.01;   // one tap = 1 cm
constexpr double kStepDegrees      = 1.0;    // one tap = 1 degree
constexpr float  kArrowW = 92.0f;
constexpr float  kArrowH = 40.0f;
constexpr double kHeadingMatch = 0.05;    // degrees: preset button shown as current
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

// Live view (WYSIWYG) on/off; remembered until X-Plane quits.
bool sLiveView = true;

struct HeadingPreset {
    const char* label;
    double      degrees;
};
// Directions relative to the aircraft: N = nose, E = right wing, S = tail, W = left wing.
const HeadingPreset kHeadingPresets[] = {
    {ICON_FA_CHEVRON_CIRCLE_UP    " N",   0.0},
    {ICON_FA_CHEVRON_CIRCLE_RIGHT " E",  90.0},
    {ICON_FA_CHEVRON_CIRCLE_DOWN  " S", 180.0},
    {ICON_FA_CHEVRON_CIRCLE_LEFT  " W", 270.0},
};

double HeadingDifference(double first, double second)
{
    return std::fabs(std::fmod(first - second + 540.0, 360.0) - 180.0);
}

constexpr const char* kPopupExplain   = "How changes are saved";
constexpr const char* kPopupAdd       = "New hotspot";
constexpr const char* kPopupRefresh   = "Re-read the file?";
constexpr const char* kPopupCancel    = "Discard unsaved changes?";
constexpr const char* kPopupRestore   = "Restore the backup?";
constexpr const char* kPopupOverwrite = "File changed outside VRConfig Helper";
constexpr const char* kPopupAdvanced  = "Advanced";
constexpr const char* kPopupNotes     = "Notes about this vrconfig file";

const char* kExplainText =
    "- Your changes are kept until you press Save or Cancel changes, even if you go back "
    "to Home or close the window.\n"
    "- Rename, Add and Delete are changes too: nothing is written before Save.\n"
    "- Save writes all changes to the aircraft's vrconfig file.\n"
    "- The first time you save an aircraft's hotspots in this X-Plane run, a backup of the original "
    "file is made. Later saves don't change it.\n"
    "- Restore backup brings back the file as it was before your first save in this run. In the next "
    "X-Plane run, your first save replaces the backup.\n"
    "- Cancel changes discards everything not yet saved; you will be asked to confirm.\n"
    "- Refresh reads the file again; unsaved changes are discarded after you confirm.\n"
    "- Loading or reloading an aircraft discards changes that haven't been saved.\n"
    "- A hotspot with commas between its numbers (\"0.39, 0.64\") cannot be edited: pick it and press "
    "Remove the commas - they are removed when you Save.\n"
    "- The orange " ICON_FA_EXCLAMATION_TRIANGLE " button next to Explain lists remarks about the file: "
    "unknown or misspelled lines, lines outside a block, hotspot names used twice.\n"
    "- X-Plane reads the vrconfig file when the aircraft loads: reload the aircraft to try your new hotspots.";

using ui::Plural;

std::vector<std::string> ChangeLines(const SpotEditor& editor)
{
    std::vector<std::string> lines;
    for (const SpotChanges& change : editor.Changes()) {
        std::string what;
        for (const std::string& whatChanged : change.what)
            what += (what.empty() ? "" : ", ") + whatChanged;
        lines.push_back(std::format("{}: {}", change.name.empty() ? "(no name)" : change.name, what));
    }
    return lines;
}

} // namespace

SpotPage::SpotPage(SpotEditor& editor, GoHome goHome)
    : mEditor(editor), mGoHome(std::move(goHome))
{
}

void SpotPage::Reload()
{
    mRenaming = false;
    mError.clear();
    mAircraft = xp::GetUserAircraft();
    if (!mAircraft) {
        mEditor.Discard();
        mOpenResult = OpenResult::NoFile;
        return;
    }
    // A file started by the plugin (not on disk yet) with pending changes is kept.
    if (mEditor.IsOpen() && !mEditor.FileOnDisk() && mEditor.FilePath() == mAircraft->vrconfigFile &&
        mEditor.ChangeCount() > 0) {
        mOpenResult = OpenResult::KeptChanges;
    } else {
        mOpenResult = mEditor.Open(mAircraft->vrconfigFile);
    }
    if (mEditor.SpotCount() == 0) {
        mSelected = 0;
    } else {
        mSelected = std::min(mSelected, mEditor.SpotCount() - 1);
        if (!mEditor.IsEditable(mSelected) && !mEditor.IsDeleted(mSelected)) {
            for (std::size_t i = 0; i < mEditor.SpotCount(); ++i) {
                if (mEditor.IsEditable(i)) { mSelected = i; break; }
            }
        }
    }
    LOG_DEBUG("reloaded: {} spots, selected {}", mEditor.SpotCount(), mSelected);
}

std::string SpotPage::SpotLabel(std::size_t spotIndex) const
{
    const std::string& name = mEditor.Edited(spotIndex).name;
    return name.empty() ? std::format("Hotspot {}", spotIndex + 1) : name;
}

void SpotPage::SelectByName(const std::string& name)
{
    for (std::size_t i = 0; i < mEditor.SpotCount(); ++i) {
        if (mEditor.Edited(i).name == name) { mSelected = i; return; }
    }
    mSelected = std::min(mSelected, mEditor.SpotCount() ? mEditor.SpotCount() - 1 : 0);
}

// --- frame ----------------------------------------------------------------------------

void SpotPage::ui_draw()
{
    ui_header();
    ImGui::Separator();

    if (!mEditor.IsOpen()) {
        ui_not_available();
    } else if (mEditor.SpotCount() == 0) {
        ui_empty_file();
    } else {
        mSelected = std::min(mSelected, mEditor.SpotCount() - 1);
        ui_spot_picker();
        ui_spot_actions();
        ImGui::Separator();
        // A deleted spot keeps the layout, greyed out.
        ImGui::BeginDisabled(mEditor.IsDeleted(mSelected));
        ui_value_table();
        ui_move_controls();
        ImGui::EndDisabled();
        ImGui::Separator();
        ui_status_and_save();
    }
    ui_popups();
}

void SpotPage::ui_header()
{
    const char* explain  = ICON_FA_INFO_CIRCLE " Explain";
    const float explainW = ImGui::CalcTextSize(explain).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    // Remarks about the file (unknown lines, duplicate names, ...): a small orange button.
    const std::size_t notes = mEditor.IsOpen() ? mEditor.File().Notes().size() : 0;
    const std::string notesLabel = std::format(ICON_FA_EXCLAMATION_TRIANGLE " {}##filenotes", notes);
    const float notesW = notes == 0 ? 0.0f
        : ImGui::CalcTextSize(notesLabel.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f +
          ImGui::GetStyle().ItemSpacing.x;

    if (ui::ui_button(ICON_FA_HOME " Home", ImVec2(0, 44.0f)) && mGoHome) {
        mRenaming = false;
        mGoHome();
    }
    ImGui::SameLine();
    // Name and file between Home and Explain; long ones are shortened in the middle.
    const float textW = ImGui::GetContentRegionMax().x - explainW - notesW - ImGui::GetCursorPosX() -
                        ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginGroup();
    ui::ui_text_ellipsis(mAircraft ? mAircraft->displayName : std::string("No aircraft"), textW);
    if (mAircraft)
        ui::ui_text_ellipsis(util::Utf8(mAircraft->vrconfigFile.filename()), textW, &theme::kTextDisabled);
    ImGui::EndGroup();
    if (notes > 0) {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - explainW - notesW);
        if (ui::ui_color_button(notesLabel.c_str(), ImVec2(notesW - ImGui::GetStyle().ItemSpacing.x, 0.0f),
                                theme::kChanged))
            mAskNotes = true;
    }
    ImGui::SameLine(ImGui::GetContentRegionMax().x - explainW);
    if (ui::ui_button(explain))
        mAskExplain = true;
}

void SpotPage::ui_not_available()
{
    ImGui::Spacing();
    if (!mAircraft) {
        ui::ui_wrapped_text("X-Plane did not report a user aircraft.", &theme::kErrorText);
    } else if (mOpenResult == OpenResult::ReadError) {
        ui::ui_wrapped_text(std::format(ICON_FA_EXCLAMATION_TRIANGLE " Cannot read the vrconfig file: {}",
                                        mEditor.LastError()), &theme::kErrorText);
    } else {
        ui::ui_wrapped_text("This aircraft has no vrconfig file.");
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDisabled);
        ui::ui_wrapped_text(util::Utf8(mAircraft->vrconfigFile));
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if (ui::ui_button(ICON_FA_PLUS "  Create the file and add a hotspot", ImVec2(0, 44.0f))) {
            mEditor.StartNewFile(mAircraft->vrconfigFile);
            OpenAddPopup();
        }
        ImGui::SameLine();
        ui::ui_help_marker("Creates <aircraft>_vrconfig.txt next to the .acf file when you Save. "
                           "X-Plane reads teleport hotspots from that file.");
    }
    ImGui::Spacing();
    if (ui::ui_button(ICON_FA_SYNC " Refresh"))
        Reload();
}

void SpotPage::ui_empty_file()
{
    ImGui::Spacing();
    ui::ui_wrapped_text(mEditor.FileOnDisk()
        ? "No teleport hotspots (BEGIN_TELEPORT_HOTSPOT) in this file yet."
        : "New vrconfig file - it is written when you Save.");
    ImGui::Spacing();
    if (ui::ui_button(ICON_FA_PLUS " Add", ImVec2(120.0f, 40.0f)))
        OpenAddPopup();
    ImGui::SameLine();
    if (ui::ui_button(ICON_FA_SYNC " Refresh", ImVec2(120.0f, 40.0f)))
        Reload();
    if (mEditor.ChangeCount() > 0 || mEditor.LastSaveTime()) {
        ImGui::Separator();
        ui_status_and_save();
    }
}

void SpotPage::ui_spot_picker()
{
    const std::size_t count = mEditor.SpotCount();

    auto lmbda_marker = [this](std::size_t spotIndex, ImVec4& color) -> std::string {
        if (mEditor.IsDeleted(spotIndex)) { color = theme::kChangedText; return ICON_FA_TIMES " "; }
        if (mEditor.IsNew(spotIndex))     { color = theme::kChangedText; return ICON_FA_CIRCLE " " ICON_FA_PLUS " "; }
        if (!mEditor.IsEditable(spotIndex)) { color = theme::kErrorText; return ICON_FA_EXCLAMATION_TRIANGLE " "; }
        if (mEditor.IsChanged(spotIndex)) { color = theme::kChangedText; return ICON_FA_CIRCLE " "; }
        if (mEditor.SavedThisRun(spotIndex)) { color = theme::kSavedText; return ICON_FA_CHECK " "; }
        color = theme::kText;
        return "";
    };
    auto lmbda_text = [&](std::size_t spotIndex, ImVec4& color) {
        std::string text = lmbda_marker(spotIndex, color) + SpotLabel(spotIndex);
        if (mEditor.IsDeleted(spotIndex))
            text += " - will be deleted";
        return text;
    };

    const char* kind  = vrconfig::KindKeyword(mEditor.Edited(mSelected).kind);
    const float kindW = ImGui::CalcTextSize("STANDING").x + ImGui::CalcTextSize("(?)").x + 24.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Hotspot:");
    ImGui::SameLine();

    ImVec4 previewColor;
    const std::string preview = lmbda_text(mSelected, previewColor);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - kindW);
    ImGui::PushStyleColor(ImGuiCol_Text, previewColor);
    const bool open = ImGui::BeginCombo("##spot", preview.c_str(), ImGuiComboFlags_HeightLarge);
    ImGui::PopStyleColor();
    if (open) {
        for (std::size_t i = 0; i < count; ++i) {
            ImVec4 color;
            const std::string item = std::format("{}##{}", lmbda_text(i, color), i);
            const bool selectable = mEditor.IsEditable(i) || mEditor.IsDeleted(i) || mEditor.CommasFixable(i);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            if (ImGui::Selectable(item.c_str(), i == mSelected,
                                  selectable ? 0 : ImGuiSelectableFlags_Disabled)) {
                mSelected = i;
                mRenaming = false;
                LOG_DEBUG_AS("lmbda_selectSpot", "selected '{}'", SpotLabel(i));
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(kind);
    ImGui::SameLine();
    ui::ui_help_marker("The hotspot type, written to the file as SITTING or STANDING.\n"
                       "It does not change the stored position: that is always exactly "
                       "what you set.");

    if (const SpotRecord* rec = mEditor.Record(mSelected); rec && !rec->editable) {
        if (mEditor.CommasFixed(mSelected)) {
            ui::ui_wrapped_text(ICON_FA_CIRCLE " The commas between the numbers are removed when you Save.",
                                &theme::kChangedText);
        } else if (mEditor.CommasFixable(mSelected)) {
            // Values like "0.39, 0.64": X-Plane expects spaces. One button makes the hotspot editable.
            const char* fix = ICON_FA_MAGIC " Remove the commas";
            const float fixW = ImGui::CalcTextSize(fix).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::AlignTextToFramePadding();
            ui::ui_text_ellipsis(std::format(ICON_FA_EXCLAMATION_TRIANGLE " {}", rec->problem),
                                 ImGui::GetContentRegionAvail().x - fixW - ImGui::GetStyle().ItemSpacing.x,
                                 &theme::kErrorText);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - fixW);
            if (ui::ui_color_button(fix, ImVec2(fixW, 0.0f), theme::kChanged)) {
                mEditor.FixCommas(mSelected);
                mNotice.clear();
            }
        } else {
            ui::ui_wrapped_text(std::format(ICON_FA_EXCLAMATION_TRIANGLE " Line {}: {}", rec->lineNumber, rec->problem),
                                &theme::kErrorText);
        }
    }
}

void SpotPage::ui_spot_actions()
{
    if (mRenaming) {
        ui_rename_row();
        return;
    }

    const float  helpW = ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 size(ui::ui_fill_width(4, helpW), 38.0f);
    const std::size_t spotIndex = mSelected;
    const bool deleted  = mEditor.IsDeleted(spotIndex);

    if (ui::ui_button(ICON_FA_PEN " Rename", size, mEditor.IsEditable(spotIndex))) {
        mRenaming    = true;
        mFocusRename = true;
        std::snprintf(mRenameText.data(), mRenameText.size(), "%s", mEditor.Edited(spotIndex).name.c_str());
    }
    ImGui::SameLine();
    if (ui::ui_button(ICON_FA_PLUS " Add", size))
        OpenAddPopup();
    ImGui::SameLine();
    if (deleted) {
        if (ui::ui_color_button(ICON_FA_UNDO " Undo delete", size, theme::kChanged, theme::kChangedHover,
                                theme::kChangedActive)) {
            mEditor.UndoDelete(spotIndex);
            mNotice.clear();
        }
    } else {
        const SpotRecord* rec = mEditor.Record(spotIndex);
        const bool canDelete  = rec == nullptr || rec->endIdx != SpotRecord::kNoLine;
        if (ui::ui_button(ICON_FA_TRASH " Delete", size, canDelete)) {
            if (mEditor.Delete(spotIndex))                     // a new spot was removed at once
                mSelected = std::min(spotIndex, mEditor.SpotCount() ? mEditor.SpotCount() - 1 : 0);
            mNotice.clear();
        }
    }
    ImGui::SameLine();
    if (ui::ui_button(ICON_FA_SYNC " Refresh", size)) {
        if (mEditor.ChangeCount() > 0)
            mAskRefresh = true;
        else
            Reload();
    }
    ImGui::SameLine();
    ui::ui_help_marker("Rename, Add and Delete are unsaved changes until you press Save.\n"
                       "Delete only marks the hotspot (it is removed from the file when you Save); "
                       "Undo delete or Cancel changes brings it back.\n"
                       "Refresh reads the file again (unsaved changes are discarded after you confirm).");

}

void SpotPage::ui_rename_row()
{
    const float buttonsW = 80.0f + 100.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonsW);
    if (mFocusRename) {
        ImGui::SetKeyboardFocusHere();
        mFocusRename = false;
    }
    const bool enter = ImGui::InputTextWithHint("##rename", mEditor.NextFreeName().c_str(),
                                                mRenameText.data(), mRenameText.size(),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ui::ui_button("OK", ImVec2(80.0f, 0)) || enter) {
        mEditor.Rename(mSelected, mRenameText.data());
        mRenaming = false;
        mNotice.clear();
    }
    ImGui::SameLine();
    if (ui::ui_button("Cancel", ImVec2(100.0f, 0)))
        mRenaming = false;

    const std::string typed = mRenameText.data();
    if (!typed.empty() && mEditor.NameInUse(typed, mSelected))
        ui::ui_wrapped_text(ICON_FA_EXCLAMATION_TRIANGLE " Another hotspot has this name.", &theme::kChangedText);
}

void SpotPage::ui_value_table()
{
    const std::size_t spotIndex = mSelected;
    const TeleportSpot* saved = mEditor.SavedSpot(spotIndex);           // nullptr for a new spot
    TeleportSpot edited       = mEditor.Edited(spotIndex);
    const std::optional<TeleportSpot> backup = mEditor.BackupSpot(spotIndex);
    const bool allGreen = mEditor.SavedThisRun(spotIndex) && !backup;    // saved this run, not in the backup

    ImGui::TextUnformatted("Position (meters) and heading (degrees)");
    ImGui::SameLine();
    ui::ui_help_marker("Saved: what is in the file now. New: your edit.\n"
                       ICON_FA_CIRCLE " (orange) marks an unsaved change.\n"
                       "Green saved values were changed by a save in this X-Plane run.\n"
                       "Directions are relative to the aircraft: X toward the right wing, "
                       "Y up, Z toward the tail; heading 0 faces the nose, 90 the right wing, "
                       "180 the tail, 270 the left wing.\n"
                       "Pitch / Roll: the tilt of the view (PRESET_THE / PRESET_PHI), "
                       "fine tuning with " ICON_FA_SLIDERS_H " Adv.");

    bool edit = false;
    auto lmbda_row = [&](const char* label, std::optional<double> savedValue, double& newValue,
                         std::optional<double> backupValue, const char* id, int decimals) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);

        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        if (savedValue) {
            const bool green = allGreen || (backupValue && std::fabs(*backupValue - *savedValue) > 0.00005);
            ImGui::PushStyleColor(ImGuiCol_Text, green ? theme::kSavedText : theme::kText);
            ImGui::TextUnformatted(std::format("{:.{}f}", *savedValue, decimals).c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("new");
        }

        ImGui::TableSetColumnIndex(2);
        const bool changed = !savedValue || std::fabs(newValue - *savedValue) > 0.00005;
        ImGui::PushStyleColor(ImGuiCol_Text, changed ? theme::kChangedText : theme::kText);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputDouble(id, &newValue, 0.0, 0.0, std::format("%.{}f", decimals).c_str()))
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
    // Pitch and roll share one row (always shown, so the layout never jumps); fine tuning is in "Adv".
    auto lmbda_tiltRow = [&]() {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Pitch / Roll");

        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        if (saved) {
            const bool green = allGreen ||
                (backup && (std::fabs(backup->pitch - saved->pitch) > 0.00005 || std::fabs(backup->roll - saved->roll) > 0.00005));
            ImGui::PushStyleColor(ImGuiCol_Text, green ? theme::kSavedText : theme::kText);
            ImGui::TextUnformatted(std::format("{:.1f} / {:.1f}", saved->pitch, saved->roll).c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("new");
        }

        ImGui::TableSetColumnIndex(2);
        const bool pitchChanged = !saved || std::fabs(edited.pitch - saved->pitch) > 0.00005;
        const bool rollChanged  = !saved || std::fabs(edited.roll - saved->roll) > 0.00005;
        const float spacing   = ImGui::GetStyle().ItemSpacing.x;
        const float halfWidth = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Text, pitchChanged ? theme::kChangedText : theme::kText);
        ImGui::SetNextItemWidth(halfWidth);
        if (ImGui::InputDouble("##the", &edited.pitch, 0.0, 0.0, "%.1f"))
            edit = true;
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, spacing);
        ImGui::PushStyleColor(ImGuiCol_Text, rollChanged ? theme::kChangedText : theme::kText);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputDouble("##phi", &edited.roll, 0.0, 0.0, "%.1f"))
            edit = true;
        ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(3);
        if (pitchChanged || rollChanged) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kChangedText);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(ICON_FA_CIRCLE);
            ImGui::PopStyleColor();
        }
    };
    auto lmbda_saved = [&](double TeleportSpot::*field) -> std::optional<double> {
        return saved ? std::optional<double>((*saved).*field) : std::nullopt;
    };
    auto lmbda_savedPos = [&](double vrconfig::Vec3::*axis) -> std::optional<double> {
        return saved ? std::optional<double>(saved->position.*axis) : std::nullopt;
    };
    auto lmbda_backup = [&](double TeleportSpot::*field) -> std::optional<double> {
        return backup ? std::optional<double>((*backup).*field) : std::nullopt;
    };
    auto lmbda_backupPos = [&](double vrconfig::Vec3::*axis) -> std::optional<double> {
        return backup ? std::optional<double>(backup->position.*axis) : std::nullopt;
    };

    const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##values", 4, flags)) {
        ImGui::TableSetupColumn("",      ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Saved", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("New",   ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("",      ImGuiTableColumnFlags_WidthFixed, 18.0f);
        ImGui::TableHeadersRow();

        using Axis = vrconfig::Vec3;
        lmbda_row("X (right)", lmbda_savedPos(&Axis::x), edited.position.x, lmbda_backupPos(&Axis::x), "##x", 4);
        lmbda_row("Y (up)",    lmbda_savedPos(&Axis::y), edited.position.y, lmbda_backupPos(&Axis::y), "##y", 4);
        lmbda_row("Z (aft)",   lmbda_savedPos(&Axis::z), edited.position.z, lmbda_backupPos(&Axis::z), "##z", 4);
        lmbda_row("Heading",   lmbda_saved(&TeleportSpot::heading), edited.heading,
                  lmbda_backup(&TeleportSpot::heading), "##psi", 1);
        lmbda_tiltRow();
        ImGui::EndTable();
    }

    if (edit) {
        mEditor.SetValues(spotIndex, edited);
        mNotice.clear();
    }
}

void SpotPage::ui_move_controls()
{
    const std::size_t spotIndex = mSelected;
    const bool editable = mEditor.IsEditable(spotIndex);

    // Head position <-> hotspot: store where I am / go to where the hotspot is.
    ImGui::Spacing();
    const bool outside   = xp::IsTeleportedOutside();
    const bool inCockpit = xp::IsInCockpitView();
    const float helpW    = ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x;
    const float rowW     = ImGui::GetContentRegionAvail().x - helpW - ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 useHeadSize(rowW * 0.62f, 44.0f);
    const ImVec2 goToSize(rowW - useHeadSize.x, 44.0f);
    if (ui::ui_button(ICON_FA_CROSSHAIRS "  Use my current head position", useHeadSize, editable && !outside))
        UseHeadPosition();
    ImGui::SameLine();
    if (ui::ui_color_button(ICON_FA_STREET_VIEW "  Go to hotspot", goToSize, theme::kGoTo, editable && !outside && inCockpit))
        GoToHotspot();
    ImGui::SameLine();
    const std::string headHelp = std::string(
        outside ? "Not available: you are teleported outside the aircraft. Teleport back inside first.\n\n"
                : (inCockpit ? "" : "Go to hotspot is not available while the view is outside the cockpit.\n\n")) +
        "Use my current head position: sit or stand where you want this hotspot, look in the "
        "direction it should face, then press. Your head position (X, Y, Z) and heading are "
        "stored as they are; pitch and roll are set to 0.\n\n"
        "Go to hotspot: focus to the hotspot location - moves the camera to the hotspot's "
        "position, heading, pitch and roll (also when Live view is off). Nothing is changed in the hotspot.";
    ui::ui_help_marker(headHelp.c_str());

    ImGui::Spacing();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float startX  = ImGui::GetCursorPosX();
    const float upDownX = startX + 3.0f * (kArrowW + spacing) + 24.0f;
    const ImVec2 arrow(kArrowW, kArrowH);

    ImGui::SetCursorPosX(startX + kArrowW + spacing);
    const float fwd = ui::ui_hold_button(ICON_FA_ARROW_UP " Forward", arrow, editable);
    ImGui::SameLine(upDownX);
    const float up = ui::ui_hold_button(ICON_FA_CARET_UP " Up", arrow, editable);
    ImGui::SameLine();
    ui::ui_help_marker("Tap to move 1 cm. Hold to keep moving - the longer you hold, the faster it moves.\n"
                       "Forward / Back / Left / Right follow the direction the camera looks "
                       "(outside the cockpit: the hotspot's heading). Up / Down are vertical.\n"
                       "The teleport box moves with the hotspot.");

    ImGui::SetCursorPosX(startX);
    const float left = ui::ui_hold_button(ICON_FA_ARROW_LEFT " Left", arrow, editable);
    ImGui::SameLine();
    const float back = ui::ui_hold_button(ICON_FA_ARROW_DOWN " Back", arrow, editable);
    ImGui::SameLine();
    const float right = ui::ui_hold_button(ICON_FA_ARROW_RIGHT " Right", arrow, editable);
    ImGui::SameLine(upDownX);
    const float down = ui::ui_hold_button(ICON_FA_CARET_DOWN " Down", arrow, editable);

    // Heading: fine turn and the four directions, on one line.
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Heading");
    ImGui::SameLine();
    ui::ui_help_marker("Turns the hotspot's facing direction, relative to the aircraft.\n"
                       ICON_FA_UNDO " / " ICON_FA_REDO ": tap = 1 degree, hold to keep turning.\n"
                       "N = nose (0), E = right wing (90), S = tail (180), W = left wing (270). "
                       "The direction the hotspot faces now is highlighted.");
    ImGui::SameLine();
    const float  presetGap   = 10.0f;
    const float  headingW    = (ImGui::GetContentRegionAvail().x - 5.0f * spacing - presetGap) / 6.0f;
    const ImVec2 headingSize(headingW, kArrowH);
    const float ccw = ui::ui_hold_button(ICON_FA_UNDO " 1\xC2\xB0", headingSize, editable);
    ImGui::SameLine();
    const float cw = ui::ui_hold_button(ICON_FA_REDO " 1\xC2\xB0", headingSize, editable);
    std::optional<double> presetHeading;
    for (const HeadingPreset& preset : kHeadingPresets) {
        ImGui::SameLine(0.0f, &preset == &kHeadingPresets[0] ? spacing + presetGap : spacing);
        const bool current = HeadingDifference(mEditor.Edited(spotIndex).heading, preset.degrees) < kHeadingMatch;
        if (ui_heading_preset_button(preset.label, headingSize, current, editable))
            presetHeading = preset.degrees;
    }

    bool repositioned = false;
    // Forward / sideways steps follow the camera heading (aircraft axes: x right, z aft).
    const double forwardSteps = fwd - back;
    const double sidewaysSteps = right - left;
    Vec3 delta{0.0, (up - down) * kStepMeters, 0.0};
    if (forwardSteps != 0.0 || sidewaysSteps != 0.0) {
        const double heading = xp::GetCameraHeading().value_or(mEditor.Edited(spotIndex).heading);
        const double radians = heading * kDegreesToRadians;
        // heading 0 looks at the nose (-z), 90 at the right wing (+x)
        const Vec3 forwardAxis{std::sin(radians), 0.0, -std::cos(radians)};
        const Vec3 rightAxis{std::cos(radians), 0.0, std::sin(radians)};
        delta.x = (forwardSteps * forwardAxis.x + sidewaysSteps * rightAxis.x) * kStepMeters;
        delta.z = (forwardSteps * forwardAxis.z + sidewaysSteps * rightAxis.z) * kStepMeters;
        LOG_DEBUG("move along heading {:.1f}: dx={:.4f} dz={:.4f}", heading, delta.x, delta.z);
    }
    if (delta.x != 0.0 || delta.y != 0.0 || delta.z != 0.0) {
        mEditor.Move(spotIndex, delta);
        repositioned = true;
    }
    if (cw != ccw) {
        mEditor.Turn(spotIndex, (cw - ccw) * kStepDegrees);
        repositioned = true;
    }
    if (presetHeading) {
        mEditor.SetHeading(spotIndex, *presetHeading);
        LOG_DEBUG("'{}' heading set to {}", SpotLabel(spotIndex), *presetHeading);
        repositioned = true;
    }

    // Revert + live view
    ImGui::Spacing();
    if (ui::ui_button(ICON_FA_UNDO_ALT " Revert this hotspot", ImVec2(0, 0), editable && mEditor.IsChanged(spotIndex))) {
        LOG_INFO("reverted '{}'", SpotLabel(spotIndex));
        if (mEditor.Revert(spotIndex))
            mSelected = std::min(spotIndex, mEditor.SpotCount() ? mEditor.SpotCount() - 1 : 0);
        else
            repositioned = true;
    }
    ImGui::SameLine();
    if (ui::ui_color_button(ICON_FA_SLIDERS_H " Adv", ImVec2(80.0f, 0.0f), theme::kAdvanced, editable))
        OpenAdvancedPopup();
    ImGui::SameLine();
    ui_live_view_toggle();

    if (repositioned) {
        mNotice.clear();
        ApplyLiveView();
    }
}

bool SpotPage::ui_heading_preset_button(const char* label, const ImVec2& size, bool current, bool enabled)
{
    // The current direction: darker button with a white frame.
    if (current && enabled) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, theme::kTextOnColor);
    }
    const bool pressed = ui::ui_color_button(label, size, current ? theme::kButtonActive : theme::kButton,
                                             theme::kButtonHover, theme::kButtonActive, enabled);
    if (current && enabled) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
    return pressed;
}

void SpotPage::ui_live_view_toggle()
{
    const std::string label = mLiveViewPaused.empty() || !sLiveView ? "Live view (WYSIWYG)"
                                                                    : "Live view (paused)";
    const float width = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                        ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().ItemSpacing.x +
                        ImGui::CalcTextSize("(?)").x;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - width);
    if (!mLiveViewPaused.empty() && sLiveView)
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kChangedText);
    if (ImGui::Checkbox(label.c_str(), &sLiveView)) {
        LOG_INFO("live view {}", sLiveView ? "on" : "off");
        if (sLiveView)
            ApplyLiveView();
        else
            mLiveViewPaused.clear();
    }
    if (!mLiveViewPaused.empty() && sLiveView)
        ImGui::PopStyleColor();
    ImGui::SameLine();
    const std::string help = std::string(
        "Live view: every move, turn and direction button (and pitch / roll in Adv) also moves "
        "the camera to the hotspot, so you see the real result at once.\n"
        "Only inside the cockpit; outside the cockpit or while teleported outside the camera is not moved.") +
        (mLiveViewPaused.empty() ? std::string() : "\nPaused now: " + mLiveViewPaused + ".");
    ui::ui_help_marker(help.c_str());
}

void SpotPage::GoToHotspot()
{
    if (mSelected >= mEditor.SpotCount() || !mEditor.IsEditable(mSelected))
        return;
    const TeleportSpot& spot = mEditor.Edited(mSelected);
    std::string reason;
    if (xp::MoveHeadTo(mEditor.File(), spot.position, spot.heading, spot.pitch, spot.roll, reason)) {
        LOG_INFO("camera moved to '{}'", SpotLabel(mSelected));
        mLiveViewPaused.clear();
    } else {
        mNotice = std::string(ICON_FA_EXCLAMATION_TRIANGLE " Go to hotspot: ") + reason;
        LOG_INFO("go to '{}' not possible: {}", SpotLabel(mSelected), reason);
    }
}

void SpotPage::ApplyLiveView()
{
    if (!sLiveView || mSelected >= mEditor.SpotCount() || !mEditor.IsEditable(mSelected))
        return;
    const TeleportSpot& spot = mEditor.Edited(mSelected);
    std::string reason;
    if (xp::MoveHeadTo(mEditor.File(), spot.position, spot.heading, spot.pitch, spot.roll, reason)) {
        if (!mLiveViewPaused.empty())
            LOG_INFO("live view resumed");
        mLiveViewPaused.clear();
    } else {
        if (mLiveViewPaused != reason)
            LOG_INFO("live view paused: {}", reason);
        mLiveViewPaused = reason;
    }
}

void SpotPage::ui_status_and_save()
{
    const std::size_t changes = mEditor.ChangeCount();
    const std::size_t spots   = mEditor.ChangedSpotCount();

    if (!mError.empty()) {
        ui::ui_status_strip(std::string(ICON_FA_EXCLAMATION_TRIANGLE " ") + mError, theme::kError, theme::kTextOnColor);
    } else if (changes > 0) {
        ui::ui_status_strip(std::format(ICON_FA_CIRCLE " {} in {} - save before reloading",
                                        Plural(changes, "unsaved change", "unsaved changes"),
                                        Plural(spots, "hotspot", "hotspots")),
                            theme::kChanged, theme::kTextOnColor);
    } else if (!mNotice.empty()) {
        ui::ui_status_strip(mNotice, theme::kPanelBgDark, theme::kText);
    } else if (mEditor.LastSaveTime()) {
        ui::ui_status_strip(std::format(ICON_FA_CHECK " Saved at {} - reload the aircraft to use the new hotspots",
                                        ClockTime(*mEditor.LastSaveTime())),
                            theme::kSaved, theme::kTextOnColor);
    } else {
        ui::ui_status_strip("No unsaved changes", theme::kPanelBgDark, theme::kText);
    }

    const float  helpW = ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 size(ui::ui_fill_width(3, helpW), 40.0f);
    if (ui::ui_color_button(ICON_FA_SAVE " Save", size, theme::kChanged, theme::kChangedHover,
                            theme::kChangedActive, changes > 0))
        DoSave(false);
    ImGui::SameLine();
    if (ui::ui_button(ICON_FA_TIMES " Cancel changes", size, changes > 0))
        mAskCancel = true;
    ImGui::SameLine();
    if (ui::ui_button(ICON_FA_HISTORY " Restore backup", size, mEditor.HasBackup()))
        mAskRestore = true;
    ImGui::SameLine();
    if (const auto backupTime = mEditor.BackupTime()) {
        const std::string help = std::format(
            "Backup of the original file, made at {}{}. Restoring discards all saves made since then, "
            "and any unsaved changes.", ClockTime(*backupTime),
            mEditor.BackupMadeThisRun() ? " in this X-Plane run" : " in an earlier X-Plane run");
        ui::ui_help_marker(help.c_str());
    } else {
        ui::ui_help_marker("A backup is made the first time you save this aircraft's hotspots in this X-Plane run.");
    }
}

// --- actions --------------------------------------------------------------------------

void SpotPage::UseHeadPosition()
{
    std::string reason;
    const auto pose = xp::GetHeadPose(mEditor.File(), reason);
    if (!pose) {
        mNotice = std::string(ICON_FA_EXCLAMATION_TRIANGLE " ") + reason;
        LOG_WARN("head position not used: {}", reason);
        return;
    }
    // SITTING / STANDING is metadata: the head position is stored as it is.
    const Vec3 position = pose->position;
    mEditor.PlaceAt(mSelected, position, pose->heading);       // pitch and roll -> 0
    mNotice.clear();
    LOG_INFO("'{}' placed at head position x={:.4f} y={:.4f} z={:.4f} heading={:.1f}",
             mEditor.Edited(mSelected).name, position.x, position.y, position.z, pose->heading);
}

void SpotPage::DoSave(bool overwriteOutsideChanges)
{
    mError.clear();
    const std::string selectedName = mEditor.SpotCount() ? mEditor.Edited(mSelected).name : std::string();
    switch (mEditor.Save(overwriteOutsideChanges)) {
        case SaveResult::Saved:
            mNotice.clear();
            mOpenResult = OpenResult::Opened;
            SelectByName(selectedName);
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

void SpotPage::OpenAddPopup()
{
    mAddName.fill('\0');
    mAddError.clear();
    const bool haveSpot = mEditor.SpotCount() > 0 && mSelected < mEditor.SpotCount() &&
                          !mEditor.IsDeleted(mSelected);
    mAddKind = haveSpot && mEditor.Edited(mSelected).kind == SpotKind::Standing ? 1 : 0;
    mAddFrom = xp::IsTeleportedOutside() && haveSpot ? 1 : 0;
    mAskAdd  = true;
}

void SpotPage::AddSpotFromPopup()
{
    NewSpotRequest req;
    req.name = mAddName.data();
    req.kind = mAddKind == 1 ? SpotKind::Standing : SpotKind::Sitting;
    const bool haveSpot = mEditor.SpotCount() > 0 && mSelected < mEditor.SpotCount();
    if (haveSpot)
        req.boxTemplate = mSelected;

    if (mAddFrom == 0) {
        std::string reason;
        const auto pose = xp::GetHeadPose(mEditor.File(), reason);
        if (!pose) {
            mAddError = reason;
            LOG_WARN("new hotspot not added: {}", reason);
            return;                                    // popup stays open
        }
        req.position = pose->position;                 // stored as it is, for both types
        req.heading  = pose->heading;
    } else {
        req.copyOf = mSelected;
    }

    mSelected = mEditor.AddSpot(req);
    mNotice.clear();
    mAddError.clear();
    ImGui::CloseCurrentPopup();
}

// --- popups ---------------------------------------------------------------------------

void SpotPage::ui_popups()
{
    if (mAskExplain)   { ImGui::OpenPopup(kPopupExplain);   mAskExplain   = false; }
    if (mAskAdd)       { ImGui::OpenPopup(kPopupAdd);       mAskAdd       = false; }
    if (mAskRefresh)   { ImGui::OpenPopup(kPopupRefresh);   mAskRefresh   = false; }
    if (mAskCancel)    { ImGui::OpenPopup(kPopupCancel);    mAskCancel    = false; }
    if (mAskRestore)   { ImGui::OpenPopup(kPopupRestore);   mAskRestore   = false; }
    if (mAskOverwrite) { ImGui::OpenPopup(kPopupOverwrite); mAskOverwrite = false; }
    if (mAskAdvanced)  { ImGui::OpenPopup(kPopupAdvanced);  mAskAdvanced  = false; }
    if (mAskNotes)     { ImGui::OpenPopup(kPopupNotes);     mAskNotes     = false; }

    ui_popup_explain();
    ui_popup_add_spot();
    ui_popup_refresh();
    ui_popup_cancel_changes();
    ui_popup_restore_backup();
    ui_popup_overwrite_file();
    ui_popup_advanced();
    ui_popup_notes();
}

void SpotPage::ui_popup_notes()
{
    std::string text = "Remarks about this vrconfig file - X-Plane may read these lines differently than "
                       "intended:\n";
    if (mEditor.IsOpen()) {
        for (const std::string& note : mEditor.File().Notes())
            text += "\n- " + note;
    }
    ui::ui_popup_message(kPopupNotes, text);
}

void SpotPage::ui_popup_explain()
{
    ui::ui_popup_message(kPopupExplain, kExplainText);
}

void SpotPage::ui_popup_add_spot()
{
    ui::ui_popup_center_next();
    if (!ImGui::BeginPopupModal(kPopupAdd, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        return;

    const bool haveSpot = mEditor.SpotCount() > 0 && mSelected < mEditor.SpotCount();
    const bool outside  = xp::IsTeleportedOutside();
    if (outside && mAddFrom == 0 && haveSpot)
        mAddFrom = 1;
    if (!haveSpot)
        mAddFrom = 0;

    // Name
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name:");
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##addname", mEditor.NextFreeName().c_str(), mAddName.data(), mAddName.size());
    // Always one line here (hint, warning or blank) so nothing moves while typing.
    const std::string typed = mAddName.data();
    ImGui::SetCursorPosX(90.0f);
    if (typed.empty()) {
        ImGui::TextDisabled("Empty = \"%s\"", mEditor.NextFreeName().c_str());
    } else if (mEditor.NameInUse(typed)) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kChangedText);
        ImGui::TextUnformatted(ICON_FA_EXCLAMATION_TRIANGLE " Another hotspot has this name.");
        ImGui::PopStyleColor();
    } else {
        ImGui::TextUnformatted(" ");
    }

    // Type
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Type:");
    ImGui::SameLine(90.0f);
    ImGui::RadioButton("SITTING", &mAddKind, 0);
    ImGui::SameLine();
    ImGui::RadioButton("STANDING", &mAddKind, 1);

    // Start position
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Start at:");
    ImGui::SameLine(90.0f);
    ImGui::BeginGroup();
    ImGui::BeginDisabled(outside);
    ImGui::RadioButton("my current head position", &mAddFrom, 0);
    ImGui::EndDisabled();
    if (outside) {
        ImGui::SameLine();
        ui::ui_help_marker("Not available: you are teleported outside the aircraft.");
    }
    ImGui::BeginDisabled(!haveSpot);
    const std::string copyLabel = haveSpot ? std::format("a copy of \"{}\"", SpotLabel(mSelected))
                                           : std::string("a copy of the selected hotspot");
    ImGui::RadioButton(copyLabel.c_str(), &mAddFrom, 1);
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::SameLine();
    ui::ui_help_marker("Head position: sit or stand where the hotspot should be and look the way it "
                       "should face; your head position and heading are stored as they are.\n"
                       "Copy: starts on the selected hotspot (position and heading); move it with the arrows.\n"
                       "Pitch and roll start at 0. The type does not change the position.\n"
                       "The teleport box gets the shape of an existing hotspot (same type first).");

    if (!mAddError.empty())
        ui::ui_wrapped_text(std::string(ICON_FA_EXCLAMATION_TRIANGLE " ") + mAddError, &theme::kErrorText);

    ImGui::Spacing();
    const ImVec2 size(150.0f, 36.0f);
    if (ui::ui_button(ICON_FA_PLUS " Add", size, !(mAddFrom == 0 && outside)))
        AddSpotFromPopup();
    ImGui::SameLine();
    if (ui::ui_button("Cancel", size)) {
        mAddError.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void SpotPage::ui_popup_refresh()
{
    ui::YesNoText text;
    text.message  = std::format("Refreshing discards {}:", Plural(mEditor.ChangeCount(), "unsaved change", "unsaved changes"));
    text.details  = ChangeLines(mEditor);
    text.yesLabel = "Discard and refresh";
    text.noLabel  = "Keep";
    text.yesColor = theme::kChanged;
    if (ui::ui_popup_yes_no(kPopupRefresh, text) == PopupAnswer::Yes) {
        const std::string name = mEditor.SpotCount() ? mEditor.Edited(mSelected).name : std::string();
        mEditor.Discard();
        Reload();
        SelectByName(name);
        mNotice = ICON_FA_SYNC " File read again - unsaved changes were discarded";
    }
}

void SpotPage::ui_popup_cancel_changes()
{
    ui::YesNoText text;
    text.message  = std::format("Discard {}?", Plural(mEditor.ChangeCount(), "unsaved change", "unsaved changes"));
    text.details  = ChangeLines(mEditor);
    text.yesLabel = "Discard all";
    text.noLabel  = "Keep";
    text.yesColor = theme::kChanged;
    if (ui::ui_popup_yes_no(kPopupCancel, text) == PopupAnswer::Yes) {
        const std::string name = mEditor.SpotCount() ? mEditor.Edited(mSelected).name : std::string();
        mEditor.RevertAll();
        mRenaming = false;
        SelectByName(name);
    }
}

void SpotPage::ui_popup_restore_backup()
{
    ui::YesNoText text;
    const auto backupTime = mEditor.BackupTime();
    text.message  = std::format("The vrconfig file will be replaced by its backup{}. "
                                "All saves made since then are undone.",
                                backupTime ? " from " + ClockTime(*backupTime) : std::string());
    if (mEditor.ChangeCount() > 0)
        text.note = std::format("Your {} are discarded too.",
                                Plural(mEditor.ChangeCount(), "unsaved change", "unsaved changes"));
    text.yesLabel = "Restore";
    text.noLabel  = "Keep current";
    if (ui::ui_popup_yes_no(kPopupRestore, text) == PopupAnswer::Yes) {
        mError.clear();
        if (mEditor.RestoreBackup()) {
            mNotice = ICON_FA_HISTORY " Backup restored - reload the aircraft to use it";
            mSelected = 0;
        } else {
            mError = "Restore failed: " + mEditor.LastError();
        }
    }
}

void SpotPage::ui_popup_overwrite_file()
{
    ui::YesNoText text;
    text.message  = "The vrconfig file was changed by another program after VRConfig Helper read it. "
                    "Saving now replaces those outside changes with this file plus your edits.";
    text.yesLabel = "Save anyway";
    text.noLabel  = "Don't save";
    text.yesColor = theme::kChanged;
    if (ui::ui_popup_yes_no(kPopupOverwrite, text) == PopupAnswer::Yes)
        DoSave(true);
}

// --- Advanced popup ---------------------------------------------------------------

void SpotPage::OpenAdvancedPopup()
{
    mCopyTargets.assign(mEditor.SpotCount(), 0);
    mCopyFieldIndex = 1;                                       // Y: the usual one to align
    mCopyValue      = GetSpotField(mEditor.Edited(mSelected), kAllSpotFields[mCopyFieldIndex]);
    mCopyResult.clear();
    mAskAdvanced = true;
}

void SpotPage::ui_popup_advanced()
{
    ui::ui_popup_center_next();
    if (!ImGui::BeginPopupModal(kPopupAdvanced, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        return;

    const std::size_t spotIndex = mSelected;
    if (mCopyTargets.size() != mEditor.SpotCount())
        mCopyTargets.assign(mEditor.SpotCount(), 0);

    ImGui::TextUnformatted("Hotspot:");
    ImGui::SameLine();
    const bool spotChanged = mEditor.IsChanged(spotIndex);
    ImGui::PushStyleColor(ImGuiCol_Text, spotChanged ? theme::kChangedText : theme::kText);
    ImGui::TextUnformatted(SpotLabel(spotIndex).c_str());
    if (spotChanged) {
        ImGui::SameLine();
        ImGui::TextUnformatted(ICON_FA_CIRCLE);
    }
    ImGui::PopStyleColor();

    ui_advanced_tilt();
    ui_advanced_copy();

    ImGui::Spacing();
    ImGui::Separator();
    if (ui::ui_button("Close", ImVec2(150.0f, 36.0f)))
        ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    ui_advanced_unsaved();
    ImGui::EndPopup();
}

// Unsaved changes of the whole edit session, next to Close (same text as the main window's strip).
void SpotPage::ui_advanced_unsaved()
{
    const std::size_t changes = mEditor.ChangeCount();
    ImGui::AlignTextToFramePadding();
    if (changes == 0) {
        ImGui::TextDisabled("No unsaved changes");
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kChangedText);
    ImGui::TextUnformatted(std::format(ICON_FA_CIRCLE " {} in {}",
                                       Plural(changes, "unsaved change", "unsaved changes"),
                                       Plural(mEditor.ChangedSpotCount(), "hotspot", "hotspots")).c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ui::ui_help_marker("Changes made here are like every other change: they stay unsaved until you "
                       "press Save in the main window (Cancel changes there discards them).");
}

void SpotPage::ui_advanced_tilt()
{
    const std::size_t spotIndex = mSelected;
    const bool editable = mEditor.IsEditable(spotIndex);

    ui::ui_section_title("Pitch and roll", "Fine tuning of the hotspot's tilt: tap = 1 degree, hold to keep turning, "
                       "0 = neutral.\n"
                       "Pitch (PRESET_THE): nose up / down, -90..90.\n"
                       "Roll / tilt (PRESET_PHI): sideways, -180..180.\n"
                       "With Live view on, the camera tilts too. Use my current head position "
                       "sets both back to 0.");
    ImGui::Spacing();

    bool changed = false;
    auto lmbda_tiltRow = [&](SpotField field, const char* minusLabel, const char* plusLabel, const char* id) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(SpotFieldLabel(field));
        ImGui::SameLine(150.0f);
        double value = GetSpotField(mEditor.Edited(spotIndex), field);
        const std::optional<double> saved = mEditor.SavedSpot(spotIndex)
            ? std::optional<double>(GetSpotField(*mEditor.SavedSpot(spotIndex), field)) : std::nullopt;
        const bool isChanged = !saved || std::fabs(*saved - value) > 0.00005;
        ImGui::PushStyleColor(ImGuiCol_Text, isChanged ? theme::kChangedText : theme::kText);
        ImGui::SetNextItemWidth(80.0f);
        ImGui::BeginDisabled(!editable);
        if (ImGui::InputDouble(id, &value, 0.0, 0.0, "%.1f")) {
            mEditor.SetField(spotIndex, field, value);
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
        const ImVec2 size(70.0f, 0.0f);
        ImGui::SameLine();
        const float minus = ui::ui_hold_button(minusLabel, size, editable);
        ImGui::SameLine();
        const float plus = ui::ui_hold_button(plusLabel, size, editable);
        ImGui::SameLine();
        const std::string zeroLabel = std::string("0##") + id;
        const bool zero = ui::ui_button(zeroLabel.c_str(), ImVec2(44.0f, 0.0f), editable);
        if (plus != minus) {
            mEditor.ChangeField(spotIndex, field, (plus - minus) * kStepDegrees);
            changed = true;
        }
        if (zero) {
            mEditor.SetField(spotIndex, field, 0.0);
            changed = true;
        }
        if (isChanged) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kChangedText);
            ImGui::TextUnformatted(ICON_FA_CIRCLE);
            ImGui::PopStyleColor();
        }
    };
    lmbda_tiltRow(SpotField::Pitch, ICON_FA_ARROW_DOWN " 1\xC2\xB0", ICON_FA_ARROW_UP " 1\xC2\xB0", "##pitch");
    lmbda_tiltRow(SpotField::Roll,  ICON_FA_UNDO " 1\xC2\xB0",       ICON_FA_REDO " 1\xC2\xB0",     "##roll");

    if (changed) {
        mNotice.clear();
        ApplyLiveView();
    }
}

void SpotPage::ui_advanced_copy()
{
    const std::size_t spotIndex = mSelected;
    const std::size_t count     = mEditor.SpotCount();
    const SpotField   field     = kAllSpotFields[mCopyFieldIndex];
    const int         decimals  = SpotFieldDecimals(field);

    ImGui::Spacing();
    ui::ui_section_title("Copy a value to other hotspots", "Pick a value, check the hotspots it should go to, then Apply.\n"
                       "The value starts as the selected hotspot's value; you can type another one.\n"
                       "X / Y / Z move each hotspot's teleport box along. The changes are unsaved "
                       "until you press Save, like every other change.\n"
                       ICON_FA_CIRCLE " (orange) marks a hotspot with unsaved changes; an orange value is unsaved.");
    ImGui::Spacing();

    // Which value
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Value:");
    ImGui::SameLine(80.0f);
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::BeginCombo("##copyfield", SpotFieldLabel(field))) {
        for (int fieldIndex = 0; fieldIndex < static_cast<int>(std::size(kAllSpotFields)); ++fieldIndex) {
            if (ImGui::Selectable(SpotFieldLabel(kAllSpotFields[fieldIndex]), fieldIndex == mCopyFieldIndex)) {
                mCopyFieldIndex = fieldIndex;
                mCopyValue = GetSpotField(mEditor.Edited(spotIndex), kAllSpotFields[fieldIndex]);
                mCopyResult.clear();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("=");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputDouble("##copyvalue", &mCopyValue, 0.0, 0.0, std::format("%.{}f", decimals).c_str());
    ImGui::SameLine();
    if (ui::ui_button(ICON_FA_UNDO "##fromselected", ImVec2(0, 0)))
        mCopyValue = GetSpotField(mEditor.Edited(spotIndex), field);
    ImGui::SameLine();
    ui::ui_help_marker("Take the value of the selected hotspot again.");

    // Targets
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Apply to:");
    ImGui::SameLine();
    if (ui::ui_button("All", ImVec2(60.0f, 0.0f))) {
        for (std::size_t target = 0; target < count; ++target)
            mCopyTargets[target] = target != spotIndex && mEditor.IsEditable(target);
    }
    ImGui::SameLine();
    if (ui::ui_button("None", ImVec2(60.0f, 0.0f)))
        mCopyTargets.assign(count, 0);

    const float rowHeight = ImGui::GetFrameHeightWithSpacing();
    const float listHeight = rowHeight * static_cast<float>(std::clamp<std::size_t>(count - 1, 3, 7)) +
                             ImGui::GetStyle().WindowPadding.y;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kInputBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::kPanelBg);           // checkboxes visible on the white list
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme::kPanelBgDark);
    if (ImGui::BeginChild("##copytargets", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders)) {
        const float valueX = ImGui::GetContentRegionAvail().x * 0.52f;
        const float dotX   = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(ICON_FA_CIRCLE).x;
        for (std::size_t target = 0; target < count; ++target) {
            if (target == spotIndex)
                continue;
            const bool usable = mEditor.IsEditable(target);
            if (!usable)
                mCopyTargets[target] = 0;
            const double current = GetSpotField(mEditor.Edited(target), field);
            const TeleportSpot* targetSaved = mEditor.SavedSpot(target);
            const bool fieldChanged = usable && (!targetSaved || std::fabs(GetSpotField(*targetSaved, field) - current) > 0.00005);
            auto lmbda_currentValue = [&]() {
                ImGui::PushStyleColor(ImGuiCol_Text, fieldChanged ? theme::kChangedText : theme::kText);
                ImGui::TextUnformatted(std::format("{:.{}f}", current, decimals).c_str());
                ImGui::PopStyleColor();
            };
            ImGui::PushID(static_cast<int>(target));
            ImGui::BeginDisabled(!usable);
            bool checked = mCopyTargets[target] != 0;
            if (ImGui::Checkbox(SpotLabel(target).c_str(), &checked)) {
                mCopyTargets[target] = checked ? 1 : 0;
                mCopyResult.clear();
            }
            ImGui::SameLine(valueX);
            if (!usable) {
                ImGui::TextUnformatted(mEditor.IsDeleted(target) ? "(will be deleted)" : "(cannot be edited)");
            } else if (checked) {
                lmbda_currentValue();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, theme::kChangedText);
                ImGui::TextUnformatted(std::format(ICON_FA_ARROW_RIGHT " {:.{}f}", mCopyValue, decimals).c_str());
                ImGui::PopStyleColor();
            } else {
                lmbda_currentValue();
            }
            if (usable && mEditor.IsChanged(target)) {
                ImGui::SameLine(dotX);
                ImGui::PushStyleColor(ImGuiCol_Text, theme::kChangedText);
                ImGui::TextUnformatted(ICON_FA_CIRCLE);
                ImGui::PopStyleColor();
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3);

    std::size_t checkedCount = 0;
    for (char flag : mCopyTargets)
        checkedCount += flag ? 1 : 0;

    // Always one line here (result or blank), so nothing moves.
    if (mCopyResult.empty())
        ImGui::TextUnformatted(" ");
    else
        ui::ui_wrapped_text(mCopyResult, &theme::kChangedText);

    const std::string applyLabel = std::format(ICON_FA_CHECK " Apply to {}", Plural(checkedCount, "hotspot", "hotspots"));
    if (ui::ui_color_button(applyLabel.c_str(), ImVec2(220.0f, 36.0f), theme::kAdvanced, checkedCount > 0)) {
        for (std::size_t target = 0; target < count; ++target) {
            if (mCopyTargets[target])
                mEditor.SetField(target, field, mCopyValue);
        }
        mCopyResult = std::format("{} = {:.{}f} applied to {} - unsaved until Save",
                                  SpotFieldLabel(field), mCopyValue, decimals,
                                  Plural(checkedCount, "hotspot", "hotspots"));
        LOG_INFO("{} = {:.{}f} applied to {}", SpotFieldLabel(field), mCopyValue, decimals,
                 Plural(checkedCount, "hotspot", "hotspots"));
        mNotice.clear();
    }
}

} // namespace hpvr::hotspot

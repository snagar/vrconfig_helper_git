// hpvr - "Extract manipulators to vrconfig" page

#include "ManipPage.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <utility>

#include "imgui.h"
#include "imgui_internal.h"     // ImGuiItemFlags_MixedValue (group checkbox, partly ticked)
#include "IconsFontAwesome5.h"

#include "Log.h"
#include "hotspot/SpotEditor.h"     // ClockTime
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "util/FileUtil.h"

namespace hpvr::manip {

namespace fs    = std::filesystem;
namespace theme = ui::theme;
using ui::Plural;
using ui::PopupAnswer;

namespace {

constexpr const char* kPopupExplain   = "Extract manipulators";
constexpr const char* kPopupFiles     = "Files of this aircraft";
constexpr const char* kPopupTypeList  = "Manipulators of one type";
constexpr const char* kPopupCancel    = "Discard unsaved changes?";
constexpr const char* kPopupRestore   = "Restore the backup?";
constexpr const char* kPopupOverwrite = "File changed outside VRConfig Helper";

constexpr std::size_t kMaxListedChanges = 10;      // in the Cancel popup
constexpr float       kTypeRowsShown    = 3.5f;    // type area height; the half row shows it scrolls

const char* kExplainText =
    "Step 1 of the VR setup: every ticked manipulator gets a BEGIN_MANIP block in the aircraft's "
    "vrconfig file, with default settings. A later screen will let you customise each one.\n\n"
    "1. Read the aircraft files: the aircraft's .acf lists the objects it uses (other variants in the "
    "same folder have their own .acf). They are read in the background; Abort stops it. Then choose "
    "the files to use: each shows how many manipulators it has; files without any are greyed out. "
    "This is asked again every time an aircraft is loaded.\n"
    "2. Pick the manipulators to extract: tick the types (from manip_types.ini in the plugin folder, or "
    "custom_manip_types.ini when you made one - "
    "add new types there), press Extract to vrconfig, then check the result, grouped by type. "
    "Ticked = written on Save. New manipulators start ticked; ones already in the file (\"exists\") "
    "start unticked - tick one to replace its block. Override existing ticks them all.\n\n"
    "- Defaults come from each type's template in the ini (knobs: DEG_PER_ACTION 5, altitude 10, "
    "selectors 30).\n"
    "- Nothing is written before Save. Cancel changes unticks everything.\n"
    "- The first Save in this X-Plane run makes a backup: <aircraft>_vrconfig.txt.manip.bak "
    "(separate from the hotspot backup). Restore backup puts it back.\n"
    "- Loading or reloading an aircraft discards unsaved changes.\n"
    "- X-Plane reads the vrconfig file when the aircraft loads: reload it to try the new settings.";

// "{up_down}" / "{left_right}" in an ini label are drawn as arrows.
std::string TypeLabel(const std::string& label)
{
    std::string text = label;
    auto lmbda_replace = [&text](std::string_view from, std::string_view to) {
        for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
            text.replace(at, from.size(), to);
    };
    lmbda_replace("{up_down}", ICON_FA_ARROWS_ALT_V);
    lmbda_replace("{left_right}", ICON_FA_ARROWS_ALT_H);
    return text;
}

// Checkbox drawn with the "mixed" mark when only some of its group is ticked.
bool GroupCheckbox(const char* label, std::size_t ticked, std::size_t total, bool& tickAll)
{
    bool all = total > 0 && ticked == total;
    const bool mixed = ticked > 0 && ticked < total;
    if (mixed)
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
    const bool pressed = ImGui::Checkbox(label, &all);
    if (mixed)
        ImGui::PopItemFlag();
    tickAll = mixed ? true : all;
    return pressed;
}

} // namespace

ManipPage::ManipPage(ManipEditor& editor, GoHome goHome, const fs::path& pluginFolder)
    : mEditor(editor), mGoHome(std::move(goHome)), mTypeIni(pluginFolder)
{
    mTypes = mTypeIni.Load().ForExtract();
    for (const ManipTypeDef& def : mTypes.types)
        mTypeChecked.try_emplace(def.type, def.checked);
}

void ManipPage::Reload()
{
    mError.clear();
    const auto previous = mAircraft;
    mAircraft = xp::GetUserAircraft();
    if (!mAircraft) {
        ResetFiles();
        mEditor.Discard();
        mOpenResult = OpenResult::ReadError;
        mExtracted = false;
        return;
    }
    if (!previous || previous->acfFile != mAircraft->acfFile) {
        ResetFiles();                                  // another aircraft: step 1 again
        mExtracted = false;
    }
    mOpenResult = mEditor.Open(mAircraft->vrconfigFile);
    if (mEditor.RowCount() == 0 && mEditor.ChangeCount() == 0)
        mExtracted = false;                            // e.g. the editor was discarded (aircraft loaded)
}

void ManipPage::OnAircraftLoaded()
{
    ResetFiles();
    mExtracted = false;
}

void ManipPage::ResetFiles()
{
    mScanner.Abort();
    mReadState = ReadState::NotRead;
    mAircraftScan.reset();
    mFileTicked.clear();
    mPopupTicked.clear();
    mConfirmedPaths.clear();
    mFilesConfirmed = false;
    mExtractWhenDone = false;
    mSelected.reset();
    mUnpicked.clear();                                 // another aircraft: everything picked again
}

bool ManipPage::TypeChecked(const std::string& type) const
{
    const auto found = mTypeChecked.find(type);
    return found != mTypeChecked.end() && found->second;
}

std::size_t ManipPage::ControlsFound(const std::string& type) const
{
    if (!mSelected)
        return 0;
    const auto found = mSelected->controls.find(type);
    return found == mSelected->controls.end() ? 0 : found->second;
}

std::size_t ManipPage::ControlsPicked(const std::string& type) const
{
    if (!mSelected)
        return 0;
    return static_cast<std::size_t>(std::count_if(mSelected->manips.begin(), mSelected->manips.end(),
        [&](const ObjManip& manip) { return manip.type == type && !mUnpicked.contains(manip.Key()); }));
}

void ManipPage::OpenTypeList(const std::string& type)
{
    mListType = type;
    mListItems.clear();
    mListTicked.clear();
    for (std::size_t i = 0; i < mSelected->manips.size(); ++i) {
        if (mSelected->manips[i].type != type)
            continue;
        mListItems.push_back(i);
        mListTicked.push_back(mUnpicked.contains(mSelected->manips[i].Key()) ? 0 : 1);
    }
    mAskTypeList = true;
}

bool ManipPage::FileUsable(std::size_t file) const
{
    const ObjFileScan& scan = mAircraftScan->files[file];
    return scan.object.exists && scan.ControlCount() > 0;
}

// --- step 1: files ------------------------------------------------------------------------

void ManipPage::ReadFiles(bool extractWhenDone)
{
    if (!mAircraft)
        return;
    mTypes = mTypeIni.Load().ForExtract();             // read again for every read (parsed when changed)
    for (const ManipTypeDef& def : mTypes.types)
        mTypeChecked.try_emplace(def.type, def.checked);   // a new type starts as the ini says
    mExtractWhenDone = extractWhenDone;
    mScanner.Start(mAircraft->acfFile, mTypes);
    mReadState = ReadState::Reading;
    LOG_INFO("reading the objects of {}", util::Utf8(mAircraft->acfFile.filename()));
}

void ManipPage::PollRead()
{
    if (mReadState != ReadState::Reading || mScanner.Running())
        return;
    std::optional<AircraftScan> result = mScanner.TakeResult();
    if (!result) {
        mReadState = ReadState::Aborted;
        mExtractWhenDone = false;
        if (!mFilesConfirmed)
            mAircraftScan.reset();
        LOG_INFO("reading aborted");
        return;
    }
    mAircraftScan = std::move(result);
    mReadState = ReadState::Read;

    // The worker does not log (Log.txt is not thread safe): report here.
    std::size_t withManips = 0, missing = 0;
    for (const ObjFileScan& file : mAircraftScan->files) {
        if (!file.object.exists) {
            ++missing;
            LOG_WARN("{} lists {} - not found", util::Utf8(mAircraftScan->acfFile.filename()), file.object.listedAs);
        }
        if (file.ControlCount() > 0)
            ++withManips;
        for (const std::string& problem : file.problems)
            LOG_WARN("read: {}", problem);
    }
    if (!mAircraftScan->error.empty())
        LOG_ERROR("{}", mAircraftScan->error);
    LOG_INFO("read done: {} objects, {} with manipulators, {} missing", mAircraftScan->files.size(), withManips,
             missing);

    // Same files as the confirmed choice (Extract read them again): keep it.
    std::set<std::string> paths;
    for (const ObjFileScan& file : mAircraftScan->files)
        paths.insert(util::Utf8(file.object.path));
    std::vector<char> ticked(mAircraftScan->files.size(), 0);
    bool sameFiles = mFilesConfirmed;
    for (std::size_t i = 0; i < ticked.size(); ++i) {
        if (mFilesConfirmed)
            ticked[i] = mConfirmedPaths.contains(util::Utf8(mAircraftScan->files[i].object.path)) && FileUsable(i);
        else
            ticked[i] = FileUsable(i);                 // default: every file with manipulators
    }
    for (const std::string& path : mConfirmedPaths)
        sameFiles = sameFiles && paths.contains(path);

    if (sameFiles) {
        UseFiles(ticked);
        if (mExtractWhenDone) {
            mExtractWhenDone = false;
            ExtractFromSelected();
        }
        return;
    }
    mExtractWhenDone = false;
    mFilesConfirmed = false;
    mSelected.reset();
    mPopupTicked = ticked;
    if (mAircraftScan->error.empty())
        mAskFiles = true;                              // step 1b: choose the files
}

void ManipPage::UseFiles(const std::vector<char>& ticked)
{
    mFileTicked = ticked;
    mConfirmedPaths.clear();
    std::vector<const ObjFileScan*> selected;
    for (std::size_t i = 0; i < mFileTicked.size(); ++i) {
        if (mFileTicked[i]) {
            selected.push_back(&mAircraftScan->files[i]);
            mConfirmedPaths.insert(util::Utf8(mAircraftScan->files[i].object.path));
        }
    }
    mSelected = CombineScans(selected, mTypes);
    mFilesConfirmed = true;
    LOG_INFO("files confirmed: {} of {}, {} manipulators", selected.size(), mFileTicked.size(),
             mSelected->manips.size());
    for (const auto& [type, lines] : mSelected->unsupported)
        LOG_INFO("not in manip_types.ini: ATTR_manip_{} ({} lines)", type, lines);
}

// --- step 2: extract ----------------------------------------------------------------------

void ManipPage::OnExtract()
{
    mError.clear();
    // New ini content or changed files: read again first, then extract.
    mTypeIni.Load();
    if (!mSelected || mTypeIni.ParsedLastTime() || StampsChanged(mSelected->stamps)) {
        LOG_INFO("extract: {} - reading the files again first",
                 mTypeIni.ParsedLastTime() ? "manip_types.ini changed" : "the files changed");
        ReadFiles(true);
        return;
    }
    ExtractFromSelected();
}

void ManipPage::ExtractFromSelected()
{
    std::vector<ObjManip> manips;
    for (const ObjManip& manip : mSelected->manips) {
        if (TypeChecked(manip.type) && !mUnpicked.contains(manip.Key()))
            manips.push_back(manip);
    }
    const ExtractSummary summary = mEditor.Extract(manips, mOverride);
    mExtracted = true;
    mNotice = std::format(ICON_FA_INFO_CIRCLE " {} found: {} to write, {} already in the file", summary.found,
                          summary.added + summary.overridden, summary.exists + summary.saved);
}

// --- frame ----------------------------------------------------------------------------

void ManipPage::ui_draw()
{
    PollRead();
    ui_header();
    ImGui::Separator();

    if (!mAircraft) {
        ui::ui_wrapped_text("X-Plane did not report a user aircraft.", &theme::kErrorText);
    } else if (mOpenResult == OpenResult::ReadError) {
        ui::ui_wrapped_text(std::format(ICON_FA_EXCLAMATION_TRIANGLE " Cannot read the vrconfig file: {}",
                                        mEditor.LastError()), &theme::kErrorText);
        if (ui::ui_button(ICON_FA_SYNC " Refresh"))
            Reload();
    } else {
        ui_step_files();
        ui_step_manipulators();
        ImGui::Separator();
        ui_status_and_save();                          // always usable: unsaved work must stay saveable
    }
    ui_popups();
}

void ManipPage::ui_header()
{
    const char* explain  = ICON_FA_INFO_CIRCLE " Explain";
    const float explainW = ImGui::CalcTextSize(explain).x + ImGui::GetStyle().FramePadding.x * 2.0f;

    if (ui::ui_button(ICON_FA_HOME " Home", ImVec2(0, 44.0f)) && mGoHome)
        mGoHome();
    ImGui::SameLine();
    // Name and file between Home and Explain; long ones are shortened in the middle.
    const float textW = ImGui::GetContentRegionMax().x - explainW - ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginGroup();
    ui::ui_text_ellipsis(mAircraft ? mAircraft->displayName : std::string("No aircraft"), textW);
    if (mAircraft) {
        const std::string file = util::Utf8(mAircraft->vrconfigFile.filename());
        ui::ui_text_ellipsis(mEditor.IsOpen() && !mEditor.FileOnDisk() ? file + " (new)" : file, textW,
                             &theme::kTextDisabled);
    }
    ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetContentRegionMax().x - explainW);
    if (ui::ui_button(explain))
        mAskExplain = true;
}

void ManipPage::ui_step_files()
{
    ui::ui_step_title(1, "Read the aircraft files", mFilesConfirmed, !mFilesConfirmed,
                  "The aircraft's .acf lists the objects (OBJ files) it uses; only those are read, so "
                  "other variants in the same folder (e.g. a G1000 or seaplane .acf) are left out.\n"
                  "After reading, choose the files to use: each shows how many manipulators it has.\n"
                  "Asked again every time an aircraft is loaded.");

    const ImGuiStyle& style = ImGui::GetStyle();
    const float right = ImGui::GetContentRegionMax().x;
    const std::string acfName = util::Utf8(mAircraft->acfFile.filename());

    if (mReadState == ReadState::Reading) {
        const char* abort  = ICON_FA_STOP " Abort";
        const float abortW = ImGui::CalcTextSize(abort).x + style.FramePadding.x * 2.0f;
        const std::string overlay = mScanner.FilesTotal() == 0
            ? std::string("Reading the .acf...")
            : std::format("Reading {} of {} files - {:.0f}%", std::min(mScanner.FilesDone() + 1, mScanner.FilesTotal()),
                          mScanner.FilesTotal(), mScanner.Fraction() * 100.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme::kButton);
        ImGui::ProgressBar(mScanner.FilesTotal() == 0 ? -1.0f * static_cast<float>(ImGui::GetTime()) : mScanner.Fraction(),
                           ImVec2(right - ImGui::GetCursorPosX() - abortW - style.ItemSpacing.x, 36.0f), overlay.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(right - abortW);
        if (ui::ui_color_button(abort, ImVec2(abortW, 36.0f), theme::kError)) {
            mScanner.Abort();
            LOG_INFO("reading aborted by the user");
        }
        return;
    }

    const bool haveFiles = mAircraftScan && mAircraftScan->error.empty();
    if (!haveFiles) {
        // Not read yet (or aborted / .acf unreadable): the one thing to do now.
        if (ui::ui_button(ICON_FA_FOLDER_OPEN "  Read aircraft files", ImVec2(240.0f, 36.0f)))
            ReadFiles(false);
        // Next to the button, one line; a long .acf name is shortened in the middle.
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        const float textW = right - ImGui::GetCursorPosX();
        if (mAircraftScan && !mAircraftScan->error.empty())
            ui::ui_text_ellipsis(std::string(ICON_FA_EXCLAMATION_TRIANGLE " ") + mAircraftScan->error, textW, &theme::kErrorText);
        else if (mReadState == ReadState::Aborted)
            ui::ui_text_ellipsis(ICON_FA_EXCLAMATION_TRIANGLE " Reading aborted", textW, &theme::kChangedText);
        else
            ui::ui_text_ellipsis(acfName, textW, &theme::kTextDisabled);
        return;
    }

    // Read: choose / show the files.
    std::size_t usable = 0, ticked = 0;
    for (std::size_t i = 0; i < mAircraftScan->files.size(); ++i) {
        usable += FileUsable(i) ? 1 : 0;
        ticked += (mFilesConfirmed && i < mFileTicked.size() && mFileTicked[i]) ? 1 : 0;
    }
    const char* choose = mFilesConfirmed ? ICON_FA_LIST " Change files" : ICON_FA_LIST " Choose files";
    const char* again  = ICON_FA_SYNC " Read again";
    const float chooseW = ImGui::CalcTextSize(choose).x + style.FramePadding.x * 2.0f;
    const float againW  = ImGui::CalcTextSize(again).x + style.FramePadding.x * 2.0f;

    ImGui::AlignTextToFramePadding();
    if (mFilesConfirmed) {
        ImGui::TextUnformatted(std::format("{} of {} files used", ticked, mAircraftScan->files.size()).c_str());
        ImGui::SameLine();
        ImGui::TextColored(theme::kSavedText, ICON_FA_CHECK);
    } else {
        ImGui::TextUnformatted(std::format("{} files, {} with manipulators", mAircraftScan->files.size(), usable).c_str());
    }
    ImGui::SameLine(right - chooseW - againW - style.ItemSpacing.x);
    if (mFilesConfirmed) {
        if (ui::ui_button(choose, ImVec2(chooseW, 0.0f))) {
            mPopupTicked = mFileTicked;
            mAskFiles = true;
        }
    } else if (ui::ui_color_button(choose, ImVec2(chooseW, 0.0f), theme::kChanged)) {   // the next thing to do
        mAskFiles = true;
    }
    ImGui::SameLine(right - againW);
    if (ui::ui_button(again, ImVec2(againW, 0.0f)))
        ReadFiles(false);
}

void ManipPage::ui_step_manipulators()
{
    ui::ui_step_title(2, "Pick the manipulators to extract", false, mFilesConfirmed,
                  "Tick the manipulator types to take from the chosen files and press Extract to vrconfig. "
                  "Then check the result: each ticked row is written to the vrconfig file when you Save.\n"
                  "The types come from manip_types.ini in the plugin folder. To add types, copy it to "
                  "custom_manip_types.ini and edit the copy (a plugin update replaces manip_types.ini).");
    ImGui::BeginDisabled(!mFilesConfirmed);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDisabled);
    ui::ui_wrapped_text(mFilesConfirmed
        ? "Tick the types, press Extract, then untick what you don't want."
        : "Do step 1 first: read the aircraft files and choose them.");
    ImGui::PopStyleColor();
    ui_type_filters();
    ui_extract_row();
    ui_result();
    ImGui::EndDisabled();
}

void ManipPage::ui_type_filters()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowH   = ImGui::GetFrameHeightWithSpacing();
    const float height = rowH * kTypeRowsShown + style.WindowPadding.y;
    const bool counted = mSelected.has_value();

    // Tighter padding and a thinner scroll bar leave room for 3 columns.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, style.WindowPadding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10.0f);
    const bool typesShown = ImGui::BeginChild("##types", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
    ImGui::PopStyleVar(2);
    if (typesShown) {
        constexpr int   kMaxColumns = 3;
        constexpr float kPillPadX   = 4.0f;          // count button padding
        constexpr float kPillGap    = 3.0f;          // between the label and the count
        constexpr float kColumnGap  = 4.0f;          // between columns
        const float rowStartX = ImGui::GetCursorPosX();
        float labelW = 0.0f;                        // category checkbox column: its widest label
        for (ManipCategory category : kAllCategories)
            labelW = std::max(labelW, ImGui::GetFrameHeight() + style.ItemInnerSpacing.x +
                                        ImGui::CalcTextSize(CategoryLabel(category)).x + kColumnGap * 2.0f);

        // The count button's text: "28", or "26/28" when some are unticked in its list.
        auto lmbda_countText = [this](const std::string& type) {
            const std::size_t found  = ControlsFound(type);
            const std::size_t picked = ControlsPicked(type);
            return picked == found ? std::to_string(found) : std::format("{}/{}", picked, found);
        };
        // Each column is as wide as its widest entry (checkbox + label + count),
        // so a longer count ("12/14") never runs into the next column. 3 columns
        // when they fit, else fewer.
        auto lmbda_itemWidth = [&](const ManipTypeDef& def) {
            float width = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(TypeLabel(def.label).c_str()).x;
            if (counted)
                width += kPillGap + ImGui::CalcTextSize(lmbda_countText(def.type).c_str()).x + kPillPadX * 2.0f;
            return width + kColumnGap;
        };
        const float spaceW = ImGui::GetContentRegionAvail().x - labelW;
        int columns = 1;
        std::vector<float> columnX(1, 0.0f);           // left edge of each column, after the category label
        for (int tryColumns = kMaxColumns; tryColumns >= 1; --tryColumns) {
            std::vector<float> widths(static_cast<std::size_t>(tryColumns), 0.0f);
            for (ManipCategory category : kAllCategories) {
                int column = 0;
                for (const ManipTypeDef& def : mTypes.types) {
                    if (def.category != category)
                        continue;
                    float& width = widths[static_cast<std::size_t>(column)];
                    width = std::max(width, lmbda_itemWidth(def));
                    column = (column + 1) % tryColumns;
                }
            }
            float total = 0.0f;
            for (float width : widths)
                total += width;
            if (total <= spaceW || tryColumns == 1) {
                columns = tryColumns;
                columnX.assign(1, 0.0f);
                for (int column = 1; column < tryColumns; ++column)
                    columnX.push_back(columnX.back() + widths[static_cast<std::size_t>(column - 1)]);
                break;
            }
        }

        for (ManipCategory category : kAllCategories) {
            std::vector<const ManipTypeDef*> members;
            for (const ManipTypeDef& def : mTypes.types) {
                if (def.category == category)
                    members.push_back(&def);
            }
            if (members.empty())
                continue;
            const std::size_t ticked = static_cast<std::size_t>(std::count_if(members.begin(), members.end(),
                [this](const ManipTypeDef* def) { return TypeChecked(def->type); }));

            ImGui::PushID(static_cast<int>(category));
            bool tickAll = false;
            if (GroupCheckbox(CategoryLabel(category), ticked, members.size(), tickAll)) {
                for (const ManipTypeDef* def : members)
                    mTypeChecked[def->type] = tickAll;
                LOG_DEBUG_AS("lmbda_category", "{}: {}", CategoryLabel(category), tickAll ? "all" : "none");
            }
            int column = 0;
            for (const ManipTypeDef* def : members) {
                if (column == columns) {
                    ImGui::NewLine();
                    column = 0;
                }
                ImGui::SameLine(rowStartX + labelW + columnX[static_cast<std::size_t>(column++)]);
                const std::size_t found = ControlsFound(def->type);
                const std::string label = std::format("{}##{}", TypeLabel(def->label), def->type);
                bool checked = TypeChecked(def->type);
                ImGui::BeginDisabled(counted && found == 0);           // none in these files
                if (ImGui::Checkbox(label.c_str(), &checked))
                    mTypeChecked[def->type] = checked;
                if (counted) {
                    // The count is a button: it opens the list of this type's manipulators.
                    const std::size_t picked = ControlsPicked(def->type);
                    const std::string count = std::format("{}##list_{}", lmbda_countText(def->type), def->type);
                    ImGui::SameLine(0.0f, kPillGap);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kPillPadX, 1.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                    const bool open = picked == found ? ui::ui_button(count.c_str())
                                                      : ui::ui_color_button(count.c_str(), ImVec2(0.0f, 0.0f), theme::kChanged);
                    ImGui::PopStyleVar(2);
                    if (open)
                        OpenTypeList(def->type);
                }
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }

        // Types in the files that the ini does not know, and ini problems.
        if (counted && !mSelected->unsupported.empty()) {
            std::string types;
            for (const auto& [type, lines] : mSelected->unsupported)
                types += (types.empty() ? "ATTR_manip_" : ", ATTR_manip_") + type;
            ui::ui_wrapped_text(std::format(ICON_FA_EXCLAMATION_TRIANGLE " Not in {}: {}", mTypeIni.File().UsedName(), types),
                                &theme::kChangedText);
        }
        if (const std::string notice = mTypeIni.Notice(); !notice.empty())
            ui::ui_wrapped_text(std::string(ICON_FA_EXCLAMATION_TRIANGLE " ") + notice,
                                mTypeIni.File().Found() ? &theme::kChangedText : &theme::kErrorText);
        for (const std::string& problem : mTypes.problems)
            ui::ui_wrapped_text(std::format(ICON_FA_EXCLAMATION_TRIANGLE " {} {}", mTypeIni.File().UsedName(), problem),
                                &theme::kErrorText);
        if (mTypeIni.File().Custom())
            ui::ui_wrapped_text(std::format(ICON_FA_INFO_CIRCLE " Using {} (your own types)", mTypeIni.File().UsedName()),
                                &theme::kTextDisabled);
    }
    ImGui::EndChild();
}

void ManipPage::ui_extract_row()
{
    ImGui::Spacing();
    const bool anyType = std::any_of(mTypes.types.begin(), mTypes.types.end(),
                                     [this](const ManipTypeDef& def) { return TypeChecked(def.type); });
    const bool canExtract = mFilesConfirmed && mReadState != ReadState::Reading && anyType;
    if (ui::ui_button(ICON_FA_FILE_EXPORT "  Extract to vrconfig", ImVec2(240.0f, 36.0f), canExtract))
        OnExtract();
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImGui::Checkbox("Override existing", &mOverride);
    ImGui::SameLine();
    ui::ui_help_marker("Off: a manipulator that already has a block in the vrconfig file starts unticked "
                       "(\"exists\") and is left as it is.\n"
                       "On: those start ticked too - their blocks are replaced by the default settings.\n"
                       "You can always tick or untick each row in the result.\n"
                       "This is step 1 of the VR setup: a later screen customises each manipulator.");
    ImGui::EndGroup();
}

void ManipPage::ui_result()
{
    const ExtractSummary summary = mEditor.Summary();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::AlignTextToFramePadding();
    if (!mExtracted) {
        ImGui::TextUnformatted("Result");
    } else {
        ImGui::TextUnformatted(std::format("Result: {} found", summary.found).c_str());
        if (summary.added + summary.overridden > 0) {
            ImGui::SameLine();
            ImGui::TextColored(theme::kChangedText, "\xC2\xB7 %zu to write", summary.added + summary.overridden);
        }
        if (summary.exists > 0) {
            ImGui::SameLine();
            ImGui::TextUnformatted(std::format("\xC2\xB7 {} exist", summary.exists).c_str());
        }
        if (summary.saved > 0) {
            ImGui::SameLine();
            ImGui::TextColored(theme::kSavedText, "\xC2\xB7 %zu saved", summary.saved);
        }
    }
    ImGui::SameLine();
    ui::ui_help_marker("Grouped by type. Ticked = written to the vrconfig file when you Save (orange "
                       "until then): new manipulators are added at the end, ones that exist get their "
                       "block replaced. Untick what you don't want.\n"
                       "Status: added / overridden (ticked), exists, " ICON_FA_CHECK " saved (green), "
                       "skipped (unticked new one).\n"
                       "Default settings only: a later screen customises each manipulator.");
    if (mExtracted && mEditor.RowCount() > 0) {
        const char* none = "None";
        const char* all  = "All";
        const float noneW = ImGui::CalcTextSize(none).x + style.FramePadding.x * 2.0f;
        const float allW  = ImGui::CalcTextSize(all).x + style.FramePadding.x * 2.0f;
        const float right = ImGui::GetContentRegionMax().x;
        ImGui::SameLine(right - noneW - allW - style.ItemSpacing.x);
        if (ui::ui_button(all, ImVec2(allW, 0.0f)))
            mEditor.SetAllTicked(true);
        ImGui::SameLine(right - noneW);
        if (ui::ui_button(none, ImVec2(noneW, 0.0f)))
            mEditor.SetAllTicked(false);
    }

    // The list takes the rest of the page above the status strip and the Save row.
    const float bottom = 40.0f + 34.0f + style.ItemSpacing.y * 3.0f + style.WindowPadding.y + 6.0f;
    const float height = std::max(80.0f, ImGui::GetContentRegionAvail().y - bottom);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kInputBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::kPanelBg);            // checkboxes visible on the white list
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme::kPanelBgDark);
    if (ImGui::BeginChild("##result", ImVec2(0.0f, height), ImGuiChildFlags_Borders)) {
        if (!mExtracted) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDisabled);
            ui::ui_wrapped_text(mFilesConfirmed
                ? "Press Extract to vrconfig to list the manipulators of the chosen types.\n\n"
                  "This is step 1 of the VR setup: each manipulator gets default settings. "
                  "A later screen will let you customise each one."
                : "The manipulators appear here after step 1 and Extract to vrconfig.");
            ImGui::PopStyleColor();
        } else if (mEditor.RowCount() == 0) {
            ui::ui_wrapped_text("No manipulator of the chosen types in these files.");
        } else {
            // Groups in the ini order of their type; rows in file order.
            std::vector<std::string> groupOrder;
            for (const ManipTypeDef& def : mTypes.types)
                groupOrder.push_back(def.type);
            std::map<std::string, std::vector<std::size_t>> groups;
            for (std::size_t row = 0; row < mEditor.RowCount(); ++row) {
                const std::string& type = mEditor.Row(row).type;
                if (!groups.contains(type) && std::find(groupOrder.begin(), groupOrder.end(), type) == groupOrder.end())
                    groupOrder.push_back(type);                 // kept from an earlier extraction
                groups[type].push_back(row);
            }

            struct Entry {
                const std::string* type;    // group header when row is empty
                std::optional<std::size_t> row;
            };
            std::vector<Entry> entries;
            for (const std::string& type : groupOrder) {
                const auto group = groups.find(type);
                if (group == groups.end())
                    continue;
                entries.push_back({&group->first, std::nullopt});
                if (!mCollapsed.contains(type)) {
                    for (std::size_t row : group->second)
                        entries.push_back({&group->first, row});
                }
            }

            const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##rows", 2, flags)) {
                ImGui::TableSetupColumn("name",   ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthFixed, 104.0f);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(entries.size()));
                while (clipper.Step()) {
                    for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                        const Entry& entry = entries[static_cast<std::size_t>(index)];
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        if (!entry.row) {
                            // Group header: fold arrow, group tick, type, count.
                            const std::string& type = *entry.type;
                            const std::vector<std::size_t>& rows = groups[type];
                            const std::size_t ticked = static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(),
                                [this](std::size_t row) { return mEditor.Ticked(row); }));
                            ImGui::PushID(type.c_str());
                            const bool folded = mCollapsed.contains(type);
                            if (ImGui::ArrowButton("##fold", folded ? ImGuiDir_Right : ImGuiDir_Down)) {
                                if (folded) mCollapsed.erase(type); else mCollapsed.insert(type);
                            }
                            ImGui::SameLine();
                            bool tickAll = false;
                            if (GroupCheckbox("##group", ticked, rows.size(), tickAll)) {
                                for (std::size_t row : rows)
                                    mEditor.SetTicked(row, tickAll);
                                mNotice.clear();
                            }
                            ImGui::SameLine();
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted(type.c_str());
                            ImGui::SameLine();
                            ImGui::TextDisabled("(%zu)", rows.size());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextDisabled("%s", CategoryShortLabel(mEditor.Row(rows.front()).category));
                            ImGui::PopID();
                            continue;
                        }

                        const std::size_t row  = *entry.row;
                        const RowStatus status = mEditor.Status(row);
                        const ImVec4& color = status == RowStatus::Saved   ? theme::kSavedText
                                            : mEditor.Ticked(row)          ? theme::kChangedText
                                            : status == RowStatus::Skipped ? theme::kTextDisabled
                                                                           : theme::kText;
                        ImGui::PushID(static_cast<int>(row));
                        ImGui::Indent(ImGui::GetFrameHeight() + style.ItemSpacing.x);
                        bool ticked = mEditor.Ticked(row);
                        if (ImGui::Checkbox("##tick", &ticked)) {
                            mEditor.SetTicked(row, ticked);
                            mNotice.clear();
                        }
                        ImGui::SameLine();
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextColored(color, "%s", mEditor.Row(row).DisplayName().c_str());
                        ImGui::Unindent(ImGui::GetFrameHeight() + style.ItemSpacing.x);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::AlignTextToFramePadding();
                        switch (status) {
                            case RowStatus::Added:      ImGui::TextColored(theme::kChangedText, ICON_FA_CIRCLE " added");      break;
                            case RowStatus::Overridden: ImGui::TextColored(theme::kChangedText, ICON_FA_CIRCLE " overridden"); break;
                            case RowStatus::Saved:      ImGui::TextColored(theme::kSavedText, ICON_FA_CHECK " saved");         break;
                            case RowStatus::Exists:     ImGui::TextUnformatted("exists");                                      break;
                            case RowStatus::Skipped:    ImGui::TextDisabled("skipped");                                        break;
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
        }
        if (mSelected) {
            for (const std::string& problem : mSelected->problems)
                ui::ui_wrapped_text(std::string(ICON_FA_EXCLAMATION_TRIANGLE " ") + problem, &theme::kErrorText);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3);
}

void ManipPage::ui_status_and_save()
{
    const std::size_t changes = mEditor.ChangeCount();

    if (!mError.empty()) {
        ui::ui_status_strip(std::string(ICON_FA_EXCLAMATION_TRIANGLE " ") + mError, theme::kError, theme::kTextOnColor);
    } else if (changes > 0) {
        ui::ui_status_strip(std::format(ICON_FA_CIRCLE " {} - save before reloading",
                                        Plural(changes, "unsaved change", "unsaved changes")),
                            theme::kChanged, theme::kTextOnColor);
    } else if (!mNotice.empty()) {
        ui::ui_status_strip(mNotice, theme::kPanelBgDark, theme::kText);
    } else if (mEditor.LastSaveTime()) {
        ui::ui_status_strip(std::format(ICON_FA_CHECK " Saved at {} - reload the aircraft to use it",
                                        hotspot::ClockTime(*mEditor.LastSaveTime())),
                            theme::kSaved, theme::kTextOnColor);
    } else if (!mEditor.FileOnDisk()) {
        ui::ui_status_strip("No vrconfig file yet - it is created when you Save", theme::kPanelBgDark, theme::kText);
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
            "Backup of the vrconfig file before the first manipulator save, made at {}{} "
            "(<aircraft>_vrconfig.txt.manip.bak). Restoring discards all manipulator saves made since "
            "then, and any unsaved changes.", hotspot::ClockTime(*backupTime),
            mEditor.BackupMadeThisRun() ? " in this X-Plane run" : " in an earlier X-Plane run");
        ui::ui_help_marker(help.c_str());
    } else {
        ui::ui_help_marker("A backup (<aircraft>_vrconfig.txt.manip.bak) is made the first time you save "
                           "manipulators of this aircraft in this X-Plane run.");
    }
}

// --- actions --------------------------------------------------------------------------

void ManipPage::DoSave(bool overwriteOutsideChanges)
{
    mError.clear();
    switch (mEditor.Save(overwriteOutsideChanges)) {
        case SaveResult::Saved:
            mNotice.clear();
            mOpenResult = OpenResult::Opened;
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

void ManipPage::ui_popups()
{
    if (mAskExplain)   { ImGui::OpenPopup(kPopupExplain);   mAskExplain   = false; }
    if (mAskFiles)     { ImGui::OpenPopup(kPopupFiles);     mAskFiles     = false; }
    if (mAskTypeList)  { ImGui::OpenPopup(kPopupTypeList);  mAskTypeList  = false; }
    if (mAskCancel)    { ImGui::OpenPopup(kPopupCancel);    mAskCancel    = false; }
    if (mAskRestore)   { ImGui::OpenPopup(kPopupRestore);   mAskRestore   = false; }
    if (mAskOverwrite) { ImGui::OpenPopup(kPopupOverwrite); mAskOverwrite = false; }

    ui_popup_explain();
    ui_popup_files();
    ui_popup_type_list();
    ui_popup_cancel_changes();
    ui_popup_restore_backup();
    ui_popup_overwrite_file();
}

void ManipPage::ui_popup_explain()
{
    ui::ui_popup_message(kPopupExplain, kExplainText);
}

void ManipPage::ui_popup_files()
{
    ui::ui_popup_center_next();
    if (!ImGui::BeginPopupModal(kPopupFiles, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        return;
    if (!mAircraftScan) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const auto& files = mAircraftScan->files;
    mPopupTicked.resize(files.size(), 0);
    ImGui::Dummy(ImVec2(470.0f, 0.0f));                // popup width

    ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDisabled);
    ui::ui_wrapped_text(std::format("The objects listed in {}. Tick the files to take manipulators from; "
                                    "files without manipulators are greyed out.",
                                    util::Utf8(mAircraftScan->acfFile.filename())));
    ImGui::PopStyleColor();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowH   = ImGui::GetFrameHeightWithSpacing();
    const float height = std::min(rowH * static_cast<float>(files.size()) + style.WindowPadding.y * 2.0f, 380.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kInputBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::kPanelBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme::kPanelBgDark);
    if (ImGui::BeginChild("##files", ImVec2(0.0f, height), ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTable("##filelist", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("file",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, 128.0f);
            for (std::size_t i = 0; i < files.size(); ++i) {
                const ObjFileScan& file = files[i];
                const bool usable = FileUsable(i);
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                bool tick = mPopupTicked[i] != 0;
                ImGui::BeginDisabled(!usable);
                if (ImGui::Checkbox("##use", &tick))
                    mPopupTicked[i] = tick ? 1 : 0;
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                // The file name first (never clipped), then its folder relative to the aircraft, in grey.
                std::error_code ec;
                const fs::path relative = fs::relative(file.object.path, mAircraftScan->acfFile.parent_path(), ec);
                const std::string name   = util::Utf8(file.object.path.filename());
                std::string folder = ec ? std::string() : relative.parent_path().generic_string();
                if (file.object.byName)
                    folder = folder.empty() ? "by name" : folder + ", by name";
                if (usable)
                    ImGui::TextUnformatted(name.c_str());
                else
                    ImGui::TextDisabled("%s", name.c_str());
                if (!folder.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", folder.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                if (!file.object.exists)
                    ImGui::TextColored(theme::kChangedText, ICON_FA_EXCLAMATION_TRIANGLE " missing");
                else if (file.ControlCount() == 0)
                    ImGui::TextDisabled("none");
                else
                    ImGui::TextUnformatted(Plural(file.ControlCount(), "manipulator", "manipulators").c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3);

    std::size_t ticked = 0;
    for (std::size_t i = 0; i < files.size(); ++i)
        ticked += mPopupTicked[i] ? 1 : 0;
    if (ui::ui_button("All")) {
        for (std::size_t i = 0; i < files.size(); ++i)
            mPopupTicked[i] = FileUsable(i) ? 1 : 0;
    }
    ImGui::SameLine();
    if (ui::ui_button("None"))
        std::fill(mPopupTicked.begin(), mPopupTicked.end(), 0);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(std::format("{} of {} files", ticked, files.size()).c_str());

    ImGui::Spacing();
    const ImVec2 size((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f, 40.0f);
    if (ui::ui_color_button(ICON_FA_CHECK " Use selected files", size, theme::kSaved, ticked > 0)) {
        UseFiles(mPopupTicked);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::ui_button("Cancel", size))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// The manipulators of one type in the chosen files, each with a tick (picked =
// taken by Extract). Changes apply on OK.
void ManipPage::ui_popup_type_list()
{
    ui::ui_popup_center_next();
    if (!ImGui::BeginPopupModal(kPopupTypeList, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        return;
    if (!mSelected) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::Dummy(ImVec2(470.0f, 0.0f));                // popup width
    ImGui::TextUnformatted(std::format("ATTR_manip_{}", mListType).c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDisabled);
    ui::ui_wrapped_text("Tick the manipulators to extract; unticked ones are left out (also when you "
                        "extract again).");
    ImGui::PopStyleColor();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowH   = ImGui::GetFrameHeightWithSpacing();
    const float height = std::min(rowH * static_cast<float>(mListItems.size()) + style.WindowPadding.y * 2.0f, 380.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kInputBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::kPanelBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme::kPanelBgDark);
    if (ImGui::BeginChild("##typelist", ImVec2(0.0f, height), ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTable("##manips", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("file", ImGuiTableColumnFlags_WidthFixed, 190.0f);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(mListItems.size()));
            while (clipper.Step()) {
                for (int item = clipper.DisplayStart; item < clipper.DisplayEnd; ++item) {
                    const ObjManip& manip = mSelected->manips[mListItems[static_cast<std::size_t>(item)]];
                    ImGui::PushID(item);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    bool tick = mListTicked[static_cast<std::size_t>(item)] != 0;
                    if (ImGui::Checkbox("##pick", &tick))
                        mListTicked[static_cast<std::size_t>(item)] = tick ? 1 : 0;
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    if (tick)
                        ImGui::TextUnformatted(manip.DisplayName().c_str());
                    else
                        ImGui::TextDisabled("%s", manip.DisplayName().c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", util::Utf8(manip.objFile.filename()).c_str());
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3);

    const std::size_t ticked = static_cast<std::size_t>(std::count(mListTicked.begin(), mListTicked.end(), 1));
    if (ui::ui_button("All"))
        std::fill(mListTicked.begin(), mListTicked.end(), 1);
    ImGui::SameLine();
    if (ui::ui_button("None"))
        std::fill(mListTicked.begin(), mListTicked.end(), 0);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(std::format("{} of {} picked", ticked, mListItems.size()).c_str());

    ImGui::Spacing();
    const ImVec2 size((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f, 40.0f);
    if (ui::ui_color_button(ICON_FA_CHECK " OK", size, theme::kSaved)) {
        for (std::size_t item = 0; item < mListItems.size(); ++item) {
            const std::string key = mSelected->manips[mListItems[item]].Key();
            if (mListTicked[item])
                mUnpicked.erase(key);
            else
                mUnpicked.insert(key);
        }
        LOG_INFO("{}: {} of {} manipulators picked", mListType, ticked, mListItems.size());
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::ui_button("Cancel", size))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void ManipPage::ui_popup_cancel_changes()
{
    ui::YesNoText text;
    text.message = std::format("Discard {}?", Plural(mEditor.ChangeCount(), "unsaved change", "unsaved changes"));
    const std::vector<std::string> lines = mEditor.ChangeLines();
    for (std::size_t i = 0; i < lines.size() && i < kMaxListedChanges; ++i)
        text.details.push_back(lines[i]);
    if (lines.size() > kMaxListedChanges)
        text.details.push_back(std::format("... and {} more", lines.size() - kMaxListedChanges));
    text.yesLabel = "Discard all";
    text.noLabel  = "Keep";
    text.yesColor = theme::kChanged;
    if (ui::ui_popup_yes_no(kPopupCancel, text) == PopupAnswer::Yes) {
        mEditor.CancelChanges();
        mNotice.clear();
    }
}

void ManipPage::ui_popup_restore_backup()
{
    ui::YesNoText text;
    const auto backupTime = mEditor.BackupTime();
    text.message = std::format("The vrconfig file will be replaced by its manipulator backup{}. "
                               "All manipulator saves made since then are undone.",
                               backupTime ? " from " + hotspot::ClockTime(*backupTime) : std::string());
    if (mEditor.ChangeCount() > 0)
        text.note = std::format("Your {} discarded too.",
                                Plural(mEditor.ChangeCount(), "unsaved change is", "unsaved changes are"));
    text.yesLabel = "Restore";
    text.noLabel  = "Keep current";
    if (ui::ui_popup_yes_no(kPopupRestore, text) == PopupAnswer::Yes) {
        mError.clear();
        if (mEditor.RestoreBackup()) {
            mOpenResult = OpenResult::Opened;
            mNotice = ICON_FA_HISTORY " Backup restored - reload the aircraft to use it";
        } else {
            mError = "Restore failed: " + mEditor.LastError();
        }
    }
}

void ManipPage::ui_popup_overwrite_file()
{
    ui::YesNoText text;
    text.message  = "The vrconfig file was changed by another program since VRConfig Helper read it.";
    text.note     = "Save anyway: your manipulator changes are applied to the file as it is now.";
    text.yesLabel = "Save anyway";
    text.noLabel  = "Don't save";
    text.yesColor = theme::kChanged;
    if (ui::ui_popup_yes_no(kPopupOverwrite, text) == PopupAnswer::Yes)
        DoSave(true);
}

} // namespace hpvr::manip

// hpvr - "VRConfig Manipulator Updater" page

#include "UpdaterPage.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>
#include <utility>

#include "imgui.h"
#include "IconsFontAwesome5.h"

#include "Log.h"
#include "hotspot/SpotEditor.h"     // ClockTime
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "vrconfig/VrConfigFile.h"
#include "util/FileUtil.h"

namespace hpvr::manip {

namespace fs    = std::filesystem;
namespace theme = ui::theme;
using ui::Plural;
using ui::PopupAnswer;

namespace {

constexpr const char* kPopupExplain   = "VRConfig Manipulator Updater";
constexpr const char* kPopupNotes     = "Notes about this vrconfig file";
constexpr const char* kPopupEdit      = "Update manipulator";
constexpr const char* kPopupDiscard   = "Discard your text?";
constexpr const char* kPopupCancel    = "Discard unsaved updates?";
constexpr const char* kPopupRestore   = "Restore the updater backup?";
constexpr const char* kPopupOverwrite = "File changed outside VRConfig Helper";

constexpr std::size_t kMaxListedChanges = 10;
constexpr std::size_t kTextBufferSize   = 16384;   // one block typed by hand
constexpr float       kPulseSeconds     = 2.0f;    // warning: 1 s fade in, 1 s fade out
constexpr float       kTypeRowsShown    = 2.5f;
// Edit popup: box heights in text lines, and the parts below the Change area.
constexpr float       kBeforeLines      = 3.6f;
constexpr float       kAfterLines       = 4.6f;
constexpr float       kButtonRowH       = 40.0f;

float ChecksHeight() { return ImGui::GetFrameHeightWithSpacing() * 1.5f + 10.0f; }

// Height of what follows the Change area: After label + box, the free text tick,
// the checks and the buttons.
float BelowChangeHeight()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetTextLineHeight() * kAfterLines +
           style.FramePadding.y * 2.0f + ChecksHeight() + kButtonRowH + style.ItemSpacing.y * 4.0f;
}

const char* kExplainText =
    "Change the manipulator blocks (BEGIN_MANIP ... END_MANIP) of a vrconfig file: their VR settings, "
    "or convert a manipulator to another type with an ACTION. Usually after Extract manipulators to "
    "vrconfig has added the blocks.\n\n"
    "1. Choose the aircraft: the aircraft in the loaded aircraft's folder are listed first (blue), the "
    "loaded one at the top. List all aircraft adds every aircraft under X-Plane's Aircraft folder (red: "
    "another folder). X-Plane reads a vrconfig file when its aircraft loads, so changes to another "
    "aircraft are only seen after loading it.\n"
    "2. Read vrconfig: lists the blocks of that aircraft's vrconfig file, with a tick per type and a Find "
    "field. The aircraft's OBJ files are read too (in the background, Abort stops it): blocks that match no "
    "manipulator are marked - X-Plane can show an error for those when the aircraft loads - and conversions "
    "are filled in with the manipulator's own values.\n\n"
    "Press a name to open its block: Before (as saved), Change (ACTION and settings, each with a help "
    "marker) and After (what Save will write). Apply keeps the change in memory.\n"
    "- Not every combination can be offered: \"Edit the After text myself (advanced)\" lets you type the "
    "block yourself - on your responsibility. It is checked when you apply it; warnings can be applied "
    "anyway.\n"
    "- Conversions marked " ICON_FA_CHECK " are used by Laminar's own aircraft; " ICON_FA_EXCLAMATION_TRIANGLE
    " = not confirmed (seen in other aircraft, or not seen at all).\n"
    "- \"hold/spring\" = HOLD_MANIP: the command keeps running while the control is held - for "
    "spring-loaded switches, starter keys and trim switches.\n"
    "- Nothing is written before Save. Cancel changes undoes them all.\n"
    "- The first Save in this X-Plane run makes a backup: <aircraft>_vrconfig.txt.updater.bak (apart from "
    "the hotspot and Extract backups). Restore backup puts it back.\n"
    "- Every Save also keeps a copy of the saved file: <aircraft>_vrconfig.txt.last_save. An aircraft "
    "update that replaces the vrconfig file leaves it alone, so your work is not lost.\n"
    "- The ACTIONs and settings, with their help texts, come from manip_actions.ini in the plugin folder, "
    "the types from manip_types.ini. Don't edit those (a plugin update replaces them): copy one to "
    "custom_manip_actions.ini / custom_manip_types.ini and edit the copy - it is then used instead.\n"
    "- Hotspots are not shown here: use the Hotspot page.";

std::string Lower(std::string text)
{
    for (char& character : text)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return text;
}

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

std::string JoinLines(const std::vector<std::string>& lines)
{
    std::string text;
    for (const std::string& line : lines)
        text += line + "\n";
    if (!text.empty())
        text.pop_back();
    return text;
}

// Single-line text field on a std::string (copied through a buffer each frame).
bool InputString(const char* id, std::string& value, float width, ImGuiInputTextFlags flags = 0)
{
    std::array<char, 512> buffer{};
    std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(id, buffer.data(), buffer.size(), flags)) {
        value = buffer.data();
        return true;
    }
    return false;
}

// Read-only multi-line box that can still be selected and copied.
void ReadOnlyText(const char* id, const std::string& text, const ImVec2& vec_text_box_size)
{
    std::vector<char> buffer(text.begin(), text.end());
    buffer.push_back('\0');
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::kPanelBg);
    ImGui::InputTextMultiline(id, buffer.data(), buffer.size(), vec_text_box_size, ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor();
}

float ButtonWidth(const char* label)
{
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

} // namespace

UpdaterPage::UpdaterPage(ManipUpdater& updater, GoHome goHome, const fs::path& pluginFolder)
    : mUpdater(updater), mGoHome(std::move(goHome)), mTypeIni(pluginFolder), mActionIni(pluginFolder)
{
    mTypes   = mTypeIni.Load();                          // also tells the vrconfig reader the rules
    mActions = mActionIni.Load();
}

// --- state ---------------------------------------------------------------------------------------

void UpdaterPage::Reload()
{
    mError.clear();
    mLoaded = xp::GetUserAircraft();
    if (mChosenAcf.empty() && mLoaded)
        mChosenAcf = mLoaded->acfFile;
    RebuildChoices();
    // Read again (kept when it has unsaved updates); the file may belong to the
    // aircraft that is loaded now, or not any more.
    if (mUpdater.IsOpen() && mRead)
        mOpenResult = mUpdater.Open(mUpdater.FilePath(), IsLoaded(mReadAcf));
}

void UpdaterPage::OnAircraftLoaded(bool discarded)
{
    mLoaded = xp::GetUserAircraft();
    if (discarded || !mUpdater.IsOpen())
        ForgetRead();
    else if (mRead)
        mOpenResult = mUpdater.Open(mUpdater.FilePath(), IsLoaded(mReadAcf));   // keeps the updates of another aircraft
    RebuildChoices();
}

void UpdaterPage::ForgetRead()
{
    mScanner.Abort();
    mRead = false;
    mReadAcf.clear();
    mObjState = ObjState::NotRead;
    mObjManips.clear();
    mObjIndex.clear();
    mObjFiles = 0;
}

void UpdaterPage::RebuildChoices()
{
    mChoices.clear();
    const fs::path loadedFolder = mLoaded ? mLoaded->acfFile.parent_path() : fs::path();
    std::set<std::string> listed;
    auto lmbda_add = [&](const fs::path& acf, bool sameFolder) {
        if (!listed.insert(util::Utf8(acf.lexically_normal())).second)
            return;
        Choice choice;
        choice.acfFile    = acf;
        choice.sameFolder = sameFolder;
        choice.loaded     = mLoaded && acf.lexically_normal() == mLoaded->acfFile.lexically_normal();
        if (sameFolder) {
            choice.label = util::Utf8(acf.filename());
        } else {
            std::error_code ec;
            const fs::path relative = fs::relative(acf, xp::GetAircraftFolder(), ec);
            choice.label = ec || relative.empty() ? acf.generic_string() : relative.generic_string();
        }
        mChoices.push_back(std::move(choice));
    };
    if (mLoaded) {
        lmbda_add(mLoaded->acfFile, true);                       // the loaded one first
        for (const fs::path& acf : ListAcfInFolder(loadedFolder))
            lmbda_add(acf, true);
    }
    if (mListAll && mAllAcf) {
        for (const fs::path& acf : *mAllAcf)
            lmbda_add(acf, !loadedFolder.empty() && acf.parent_path().lexically_normal() == loadedFolder.lexically_normal());
    }
    if (!mChosenAcf.empty() && !listed.contains(util::Utf8(mChosenAcf.lexically_normal())))
        lmbda_add(mChosenAcf, !loadedFolder.empty() && mChosenAcf.parent_path() == loadedFolder);  // keep the choice
}

void UpdaterPage::PollFinder()
{
    if (!mListAll || mFinder.Running() || mAllAcf)
        return;
    if (auto result = mFinder.TakeResult()) {
        mAllAcf = std::move(result);
        LOG_INFO("found {} aircraft under {}", mAllAcf->size(), util::Utf8(xp::GetAircraftFolder()));
        RebuildChoices();
    }
}

const UpdaterPage::Choice* UpdaterPage::ChosenChoice() const
{
    for (const Choice& choice : mChoices) {
        if (choice.acfFile == mChosenAcf)
            return &choice;
    }
    return nullptr;
}

bool UpdaterPage::IsLoaded(const fs::path& acfFile) const
{
    return mLoaded && !acfFile.empty() && acfFile.lexically_normal() == mLoaded->acfFile.lexically_normal();
}

bool UpdaterPage::ChosenIsLoaded() const
{
    return IsLoaded(mChosenAcf);
}

void UpdaterPage::ReadVrconfig()
{
    mError.clear();
    mNotice.clear();
    if (mChosenAcf.empty())
        return;
    mTypes   = mTypeIni.Load();
    mActions = mActionIni.Load();
    const fs::path vrconfig = vrconfig::VrConfigPathFor(mChosenAcf);
    mOpenResult = mUpdater.Open(vrconfig, ChosenIsLoaded());
    mRead    = true;
    mReadAcf = mChosenAcf;
    mTypeChecked.clear();                                        // every type shown again
    mGroupFilter.reset();
    LOG_INFO("updater: read {} ({})", util::Utf8(vrconfig), ChosenIsLoaded() ? "loaded aircraft" : "not loaded");

    // The aircraft's OBJ files, in the background.
    mObjManips.clear();
    mObjIndex.clear();
    mObjFiles = 0;
    if (mOpenResult == UpdaterOpen::Opened || mOpenResult == UpdaterOpen::KeptChanges) {
        mScanner.Start(mChosenAcf, mTypes);
        mObjState = ObjState::Reading;
    } else {
        mObjState = ObjState::NotRead;
    }
}

void UpdaterPage::PollObjScan()
{
    if (mObjState != ObjState::Reading || mScanner.Running())
        return;
    std::optional<AircraftScan> result = mScanner.TakeResult();
    if (!result) {
        mObjState = ObjState::Aborted;
        LOG_INFO("updater: reading the OBJ files aborted");
        return;
    }
    std::vector<const ObjFileScan*> files;
    for (const ObjFileScan& file : result->files) {
        if (file.object.exists)
            files.push_back(&file);
        else
            LOG_WARN("{} lists {} - not found", util::Utf8(result->acfFile.filename()), file.object.listedAs);
        for (const std::string& problem : file.problems)
            LOG_WARN("read: {}", problem);
    }
    if (!result->error.empty())
        LOG_ERROR("{}", result->error);
    ObjScanResult combined = CombineScans(files, mTypes);
    mObjManips = std::move(combined.manips);
    mObjFiles  = files.size();
    for (std::size_t i = 0; i < mObjManips.size(); ++i)
        mObjIndex.emplace(mObjManips[i].Key(), i);
    // No OBJ file at all (or an unreadable .acf): nothing to compare with, so no block is marked.
    mObjState = result->error.empty() && mObjFiles > 0 ? ObjState::Read : ObjState::Aborted;
    LOG_INFO("updater: {} OBJ files, {} manipulators", mObjFiles, mObjManips.size());
}

const ObjManip* UpdaterPage::ObjFor(const std::string& type, const std::vector<std::string>& refs,
                                    const std::string& tooltip) const
{
    if (mObjState != ObjState::Read)
        return nullptr;
    const auto found = mObjIndex.find(ManipKey(type, refs));
    if (found == mObjIndex.end())
        return nullptr;
    const ObjManip& manip = mObjManips[found->second];
    if (tooltip.empty() || manip.tooltips.empty())
        return &manip;
    for (const std::string& objTooltip : manip.tooltips) {
        if (Lower(objTooltip) == Lower(tooltip))
            return &manip;
    }
    return nullptr;                                              // the tooltip tells it apart: none matches
}

CheckContext UpdaterPage::MakeCheckContext(const std::string& identityBefore) const
{
    CheckContext context;
    context.types          = &mTypes;
    context.actions        = &mActions;
    context.identityBefore = identityBefore;
    if (mObjState == ObjState::Read) {
        context.inObj = [this](const ManipBlock& block) {
            return ObjFor(block.type, block.refs, block.tooltip) != nullptr;
        };
    }
    return context;
}

std::string UpdaterPage::TypeLabelFor(const std::string& type) const
{
    const ManipTypeDef* def = FindType(mTypes, type);
    return def ? TypeLabel(def->label) : type;
}

std::string UpdaterPage::TypeHelpFor(const std::string& type) const
{
    const ManipTypeDef* def = FindType(mTypes, type);
    if (!def)
        return std::format("{}: not a type VRConfig Helper knows. X-Plane matches the block against an "
                           "ATTR_manip_{} in the OBJ files.", type, type);
    return std::format("{} ({})\n{}", type, CategoryShortLabel(def->category),
                       def->help.empty() ? "No help text in the types file." : def->help);
}

std::vector<UpdaterPage::BlockInfo> UpdaterPage::Blocks() const
{
    std::vector<BlockInfo> blocks;
    if (!mUpdater.IsOpen())
        return blocks;
    std::map<std::string, std::size_t> firstLine;                // identity -> line
    for (std::size_t i = 0; i < mUpdater.BlockCount(); ++i) {
        const vrconfig::ManipRecord& record = mUpdater.Record(i);
        BlockInfo info;
        info.index      = i;
        info.name       = ManipUpdater::BlockName(record);
        info.type       = record.type;
        info.canEdit    = mUpdater.CanEdit(i);
        info.changed    = mUpdater.IsChanged(i);
        info.removed    = mUpdater.IsRemoved(i);
        info.handEdited = mUpdater.IsHandEdited(i);
        info.saved      = mUpdater.SavedThisRun(i);
        info.prefixed   = record.rawRefs != record.refs;

        std::string identity = ManipKey(record.type, record.refs) + "|" + Lower(record.tooltip);
        if (const auto known = firstLine.find(identity); known != firstLine.end())
            info.duplicateOf = known->second;
        else
            firstLine.emplace(std::move(identity), record.lineNumber);
        if (mObjState == ObjState::Read)
            info.notInObj = ObjFor(record.type, record.refs, record.tooltip) == nullptr;

        std::string error;
        if (const auto block = ParseBlock(mUpdater.CurrentLines(i), error)) {
            info.type   = block->type;
            info.spring = block->Has("HOLD_MANIP");
            info.empty  = block->IsEmpty();
            if (const BlockLine* action = block->Find("ACTION"); action && !action->values.empty())
                info.action = action->values.front();
        }
        blocks.push_back(std::move(info));
    }
    return blocks;
}

// --- frame ---------------------------------------------------------------------------------------

void UpdaterPage::ui_draw()
{
    PollFinder();
    PollObjScan();
    ui_header();
    ImGui::Separator();
    ui_step_aircraft();
    ui_step_read();
    ImGui::Separator();
    ui_status_and_save();
    ui_popups();
}

void UpdaterPage::ui_header()
{
    const char* explain  = ICON_FA_INFO_CIRCLE " Explain";
    const float explainW = ButtonWidth(explain);

    if (ui::ui_button(ICON_FA_HOME " Home", ImVec2(0, 44.0f)) && mGoHome)
        mGoHome();
    ImGui::SameLine();
    const float textW = ImGui::GetContentRegionMax().x - explainW - ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginGroup();
    ui::ui_text_ellipsis("VRConfig Manipulator Updater", textW);
    if (mUpdater.IsOpen())
        ui::ui_text_ellipsis(util::Utf8(mUpdater.FilePath().filename()), textW, &theme::kTextDisabled);
    else
        ui::ui_text_ellipsis(mLoaded ? mLoaded->displayName : std::string("No aircraft"), textW, &theme::kTextDisabled);
    ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetContentRegionMax().x - explainW);
    if (ui::ui_button(explain))
        mAskExplain = true;
}

void UpdaterPage::ui_step_aircraft()
{
    const bool chosen = mRead && mReadAcf == mChosenAcf;
    ui::ui_step_title(1, "Choose the aircraft", chosen, !chosen,
                      "The aircraft whose vrconfig file you want to update. The ones in the loaded aircraft's "
                      "folder come first (blue), the loaded one at the top. List all aircraft adds every "
                      "aircraft under X-Plane's Aircraft folder (red = another folder).\n"
                      "X-Plane reads a vrconfig file when its aircraft loads.");
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool locked = mUpdater.IsOpen() && mUpdater.ChangeCount() > 0;
    const Choice* current = ChosenChoice();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Aircraft:");
    ImGui::SameLine();
    const float comboW = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(comboW);
    const ImVec4& currentColor = current && !current->sameFolder ? theme::kOtherFolderText : theme::kSameFolderText;
    const std::string preview = current ? ui::EllipsisMiddle(current->label + (current->loaded ? "  (loaded)" : ""),
                                                             comboW - ImGui::GetFrameHeight() - style.FramePadding.x * 2.0f)
                                        : std::string("No aircraft");
    ImGui::BeginDisabled(locked);
    ImGui::PushStyleColor(ImGuiCol_Text, currentColor);
    const bool open = ImGui::BeginCombo("##aircraft", preview.c_str(), ImGuiComboFlags_HeightLarge);
    ImGui::PopStyleColor();
    if (open) {
        const float itemW = comboW - style.FramePadding.x * 4.0f;
        for (std::size_t i = 0; i < mChoices.size(); ++i) {
            const Choice& choice = mChoices[i];
            const std::string label = std::format("{}##acf{}",
                ui::EllipsisMiddle(choice.label + (choice.loaded ? "  (loaded)" : ""), itemW), i);
            ImGui::PushStyleColor(ImGuiCol_Text, choice.sameFolder ? theme::kSameFolderText : theme::kOtherFolderText);
            if (ImGui::Selectable(label.c_str(), choice.acfFile == mChosenAcf)) {
                if (choice.acfFile != mChosenAcf) {
                    mChosenAcf = choice.acfFile;
                    LOG_INFO_AS("lmbda_chooseAircraft", "updater: chose {}", util::Utf8(choice.acfFile));
                }
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    // List all aircraft (background search) + the note line.
    bool listAll = mListAll;
    if (ImGui::Checkbox("List all aircraft", &listAll)) {
        mListAll = listAll;
        if (mListAll && !mAllAcf && !mFinder.Running()) {
            mFinder.Start(xp::GetAircraftFolder());
            LOG_INFO("searching aircraft under {}", util::Utf8(xp::GetAircraftFolder()));
        }
        if (!mListAll)
            mFinder.Abort();
        RebuildChoices();
    }
    ImGui::SameLine();
    if (mListAll && mFinder.Running()) {
        const char* abort = ICON_FA_STOP " Abort";
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::kTextDisabled, "%s", std::format("Searching... {} folders", mFinder.FoldersSeen()).c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ButtonWidth(abort));
        if (ui::ui_color_button(abort, ImVec2(ButtonWidth(abort), 0.0f), theme::kError)) {
            mFinder.Abort();
            mListAll = false;
            LOG_INFO("aircraft search aborted by the user");
        }
    } else if (mListAll && mAllAcf) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::kTextDisabled, "%s", Plural(mAllAcf->size(), "aircraft found", "aircraft found").c_str());
        ImGui::SameLine();
        if (ui::ui_button(ICON_FA_SYNC "##searchAgain")) {
            mAllAcf.reset();
            mFinder.Start(xp::GetAircraftFolder());
        }
    } else {
        ui::ui_help_marker("Searches X-Plane's Aircraft folder for every .acf file, in the background.");
    }

    // One line kept for the warning / note, so the page does not jump.
    if (locked) {
        ui::ui_text_ellipsis(ICON_FA_LOCK " Save or cancel the changes of this aircraft to choose another one",
                             ImGui::GetContentRegionAvail().x, &theme::kChangedText);
    } else if (current && !current->sameFolder) {
        // Red, fading in for 1 s and out for 1 s.
        const float phase = std::fmod(static_cast<float>(ImGui::GetTime()), kPulseSeconds) / (kPulseSeconds * 0.5f);
        const float alpha = 0.2f + 0.8f * (phase < 1.0f ? phase : 2.0f - phase);
        ImVec4 color = theme::kOtherFolderText;
        color.w = alpha;
        ui::ui_text_ellipsis(ICON_FA_EXCLAMATION_TRIANGLE " Another folder - used only once that aircraft loads", ImGui::GetContentRegionAvail().x, &color);
    } else if (current && !current->loaded && mLoaded) {
        ui::ui_text_ellipsis(ICON_FA_INFO_CIRCLE " Not the loaded variant - load this variant to try it",
                             ImGui::GetContentRegionAvail().x, &theme::kTextDisabled);
    } else {
        ImGui::NewLine();
    }
}

void UpdaterPage::ui_step_read()
{
    const bool chosenRead = mRead && mReadAcf == mChosenAcf;
    ui::ui_step_title(2, "Read vrconfig and pick the manipulators", false, chosenRead,
                      "Read vrconfig lists the manipulator blocks of the aircraft's vrconfig file. Untick a type "
                      "to hide its blocks, type part of a name in Find, or pick a group: the path of the "
                      "block's first command / dataref (e.g. sim/autopilot). Press a name to update its block.\n"
                      "The aircraft's OBJ files are read too, to find blocks that match no manipulator.");
    const ImGuiStyle& style = ImGui::GetStyle();
    const float right = ImGui::GetContentRegionMax().x;

    const char* read = chosenRead ? ICON_FA_SYNC "  Read again" : ICON_FA_FILE_ALT "  Read vrconfig";
    const bool canRead = !mChosenAcf.empty() && !(mUpdater.IsOpen() && mUpdater.ChangeCount() > 0 &&
                                                  mUpdater.FilePath() != vrconfig::VrConfigPathFor(mChosenAcf));
    if (chosenRead ? ui::ui_button(read, ImVec2(150.0f, 36.0f), canRead)
                   : ui::ui_color_button(read, ImVec2(150.0f, 36.0f), theme::kButton, canRead))
        ReadVrconfig();
    ImGui::SameLine();

    // Next to the button: the read result, or the OBJ files progress.
    if (mObjState == ObjState::Reading && chosenRead) {
        const char* abort  = ICON_FA_STOP " Abort";
        const float abortW = ButtonWidth(abort);
        const std::string overlay = mScanner.FilesTotal() == 0
            ? std::string("Reading the OBJ files...")
            : std::format("OBJ files {} of {} - {:.0f}%", std::min(mScanner.FilesDone() + 1, mScanner.FilesTotal()),
                          mScanner.FilesTotal(), mScanner.Fraction() * 100.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme::kButton);
        ImGui::ProgressBar(mScanner.FilesTotal() == 0 ? -1.0f * static_cast<float>(ImGui::GetTime()) : mScanner.Fraction(),
                           ImVec2(right - ImGui::GetCursorPosX() - abortW - style.ItemSpacing.x, 36.0f), overlay.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(right - abortW);
        if (ui::ui_color_button(abort, ImVec2(abortW, 36.0f), theme::kError)) {
            mScanner.Abort();
            LOG_INFO("updater: reading the OBJ files aborted by the user");
        }
    } else {
        ImGui::BeginGroup();
        const float textW = right - ImGui::GetCursorPosX();
        if (!chosenRead) {
            ui::ui_text_ellipsis(mChosenAcf.empty() ? std::string("Choose an aircraft first")
                                                    : util::Utf8(vrconfig::VrConfigPathFor(mChosenAcf).filename()),
                                 textW, &theme::kTextDisabled);
            ui::ui_text_ellipsis("Press Read vrconfig to list its blocks", textW, &theme::kTextDisabled);
        } else if (mOpenResult == UpdaterOpen::NoFile) {
            ui::ui_text_ellipsis(ICON_FA_EXCLAMATION_TRIANGLE " This aircraft has no vrconfig file", textW, &theme::kChangedText);
            ui::ui_text_ellipsis("Extract manipulators to vrconfig creates one", textW, &theme::kTextDisabled);
        } else if (mOpenResult == UpdaterOpen::ReadError) {
            ui::ui_text_ellipsis(ICON_FA_EXCLAMATION_TRIANGLE " Cannot read: " + mUpdater.LastError(), textW, &theme::kErrorText);
        } else {
            const std::size_t notes = mUpdater.File().ManipNotes().size();
            ImGui::TextUnformatted(Plural(mUpdater.BlockCount(), "manipulator block", "manipulator blocks").c_str());
            if (notes > 0) {
                ImGui::SameLine();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 0.0f));
                if (ui::ui_color_button(std::format(ICON_FA_EXCLAMATION_TRIANGLE " {}##notes",
                                                    Plural(notes, "note", "notes")).c_str(),
                                        ImVec2(0.0f, 0.0f), theme::kChanged))
                    mAskNotes = true;
                ImGui::PopStyleVar();
            }
            const std::string objText =
                mObjState == ObjState::Read    ? std::format(ICON_FA_CHECK " {} checked", Plural(mObjFiles, "OBJ file", "OBJ files"))
              : mObjState == ObjState::Aborted ? std::string(mObjFiles == 0 && !mScanner.Running()
                                                    ? ICON_FA_EXCLAMATION_TRIANGLE " No OBJ files found - not checked"
                                                    : ICON_FA_EXCLAMATION_TRIANGLE " OBJ files not checked")
                                               : std::string();
            ui::ui_text_ellipsis(objText, textW, mObjState == ObjState::Read ? &theme::kSavedText : &theme::kChangedText);
        }
        ImGui::EndGroup();
    }

    const bool listed = chosenRead && mUpdater.IsOpen();
    ImGui::BeginDisabled(!listed);
    ui_type_filters();
    ui_find_row();
    ui_block_list();
    ImGui::EndDisabled();
}

void UpdaterPage::ui_type_filters()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeightWithSpacing() * kTypeRowsShown + style.WindowPadding.y;

    std::map<std::string, std::size_t> counts;               // type -> blocks
    std::vector<std::string> order;
    if (mRead && mUpdater.IsOpen()) {
        for (std::size_t i = 0; i < mUpdater.BlockCount(); ++i) {
            const std::string& type = mUpdater.Record(i).type;
            if (counts[type]++ == 0)
                order.push_back(type);
        }
    }
    // The types file order first, then unknown ones.
    std::vector<std::string> sorted;
    for (const ManipTypeDef& def : mTypes.types)
        if (counts.contains(def.type)) sorted.push_back(def.type);
    for (const std::string& type : order)
        if (std::find(sorted.begin(), sorted.end(), type) == sorted.end()) sorted.push_back(type);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, style.WindowPadding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10.0f);
    const bool shown = ImGui::BeginChild("##updtypes", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
    ImGui::PopStyleVar(2);
    if (shown) {
        // The rule files: missing / other version (red / orange), broken entries, a custom file in use.
        auto lmbda_ruleNotes = [](const RuleFile& file, const std::string& notice, const std::vector<std::string>& problems) {
            if (!notice.empty())
                ui::ui_wrapped_text(std::string(ICON_FA_EXCLAMATION_TRIANGLE " ") + notice,
                                    file.Found() ? &theme::kChangedText : &theme::kErrorText);
            for (const std::string& problem : problems)
                ui::ui_wrapped_text(std::format(ICON_FA_EXCLAMATION_TRIANGLE " {} {}", file.UsedName(), problem),
                                    &theme::kErrorText);
            if (file.Custom())
                ui::ui_wrapped_text(std::format(ICON_FA_INFO_CIRCLE " Using {} (your own rules)", file.UsedName()),
                                    &theme::kTextDisabled);
        };
        lmbda_ruleNotes(mTypeIni.File(), mTypeIni.Notice(), mTypes.problems);
        lmbda_ruleNotes(mActionIni.File(), mActionIni.Notice(), mActions.problems);
        if (sorted.empty()) {
            ImGui::TextDisabled("%s", mRead ? "No manipulator blocks in this file." : "The types appear after Read vrconfig.");
        } else {
            // Two columns: checkbox, label, count, help marker.
            const float columnW = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
            const float startX  = ImGui::GetCursorPosX();
            for (std::size_t i = 0; i < sorted.size(); ++i) {
                const std::string& type = sorted[i];
                if (i % 2 == 1)
                    ImGui::SameLine(startX + columnW + style.ItemSpacing.x);
                bool checked = !mTypeChecked.contains(type) || mTypeChecked[type];
                const std::string label = std::format("{} ({})##t_{}", TypeLabelFor(type), counts[type], type);
                if (ImGui::Checkbox(label.c_str(), &checked))
                    mTypeChecked[type] = checked;
                ImGui::SameLine();
                ui::ui_help_marker(TypeHelpFor(type).c_str());
            }
        }
    }
    ImGui::EndChild();
}

void UpdaterPage::ui_find_row()
{
    const char* all  = "All types";
    const char* none = "None";
    const float right = ImGui::GetContentRegionMax().x;
    const ImGuiStyle& style = ImGui::GetStyle();
    constexpr float kGroupW = 150.0f;

    // Groups: the path of each block's first command / dataref, with counts.
    std::map<std::string, std::size_t> groups;
    for (std::size_t i = 0; mUpdater.IsOpen() && mRead && i < mUpdater.BlockCount(); ++i)
        ++groups[ManipGroup(mUpdater.Record(i).refs)];
    if (mGroupFilter && !groups.contains(*mGroupFilter))
        mGroupFilter.reset();                                    // e.g. after a removal was saved
    auto lmbda_groupLabel = [](const std::string& group) { return group.empty() ? std::string("(no path)") : group; };

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Find");
    ImGui::SameLine();
    const float clearW = ImGui::GetFrameHeight();                // square Clear button, easy to hit in VR
    ImGui::SetNextItemWidth(right - ImGui::GetCursorPosX() - clearW - kGroupW - ButtonWidth(all) - ButtonWidth(none) -
                            style.ItemSpacing.x * 4.0f);
    ImGui::InputTextWithHint("##find", "name or path", mFind.data(), mFind.size());
    ImGui::SameLine();
    if (ui::ui_button(ICON_FA_TIMES "##clearfind", ImVec2(clearW, clearW), mFind[0] != '\0'))
        mFind[0] = '\0';
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kGroupW);
    const std::string preview = mGroupFilter ? ui::EllipsisMiddle(lmbda_groupLabel(*mGroupFilter), kGroupW - 30.0f)
                                             : std::string("All groups");
    ImGui::PushStyleColor(ImGuiCol_Text, mGroupFilter ? theme::kSameFolderText : theme::kText);
    const bool open = ImGui::BeginCombo("##group", preview.c_str(), ImGuiComboFlags_HeightLarge);
    ImGui::PopStyleColor();
    if (open) {
        if (ImGui::Selectable("All groups", !mGroupFilter))
            mGroupFilter.reset();
        for (const auto& [group, count] : groups) {
            const std::string label = std::format("{} ({})##g_{}", lmbda_groupLabel(group), count, group);
            if (ImGui::Selectable(label.c_str(), mGroupFilter && *mGroupFilter == group))
                mGroupFilter = group;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(right - ButtonWidth(all) - ButtonWidth(none) - style.ItemSpacing.x);
    if (ui::ui_button(all))
        mTypeChecked.clear();
    ImGui::SameLine(right - ButtonWidth(none));
    if (ui::ui_button(none)) {
        for (std::size_t i = 0; mUpdater.IsOpen() && i < mUpdater.BlockCount(); ++i)
            mTypeChecked[mUpdater.Record(i).type] = false;
    }
}

void UpdaterPage::ui_block_list()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float bottom = 40.0f + 34.0f + style.ItemSpacing.y * 3.0f + style.WindowPadding.y + 6.0f;
    const float height = std::max(80.0f, ImGui::GetContentRegionAvail().y - bottom);

    const std::vector<BlockInfo> blocks = (mRead && mUpdater.IsOpen()) ? Blocks() : std::vector<BlockInfo>{};
    const std::string find = Lower(mFind.data());
    std::vector<const BlockInfo*> shown;
    for (const BlockInfo& info : blocks) {
        if (mTypeChecked.contains(mUpdater.Record(info.index).type) && !mTypeChecked[mUpdater.Record(info.index).type])
            continue;
        if (mGroupFilter && ManipGroup(mUpdater.Record(info.index).refs) != *mGroupFilter)
            continue;
        if (!find.empty()) {
            const vrconfig::ManipRecord& record = mUpdater.Record(info.index);
            std::string haystack = Lower(info.name + " " + record.type + " " + record.tooltip);
            for (const std::string& ref : record.refs)
                haystack += " " + Lower(ref);
            if (haystack.find(find) == std::string::npos)
                continue;
        }
        shown.push_back(&info);
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kInputBg);
    if (ImGui::BeginChild("##blocks", ImVec2(0.0f, height), ImGuiChildFlags_Borders)) {
        if (!mRead || !mUpdater.IsOpen()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDisabled);
            ui::ui_wrapped_text("The manipulator blocks appear here after Read vrconfig.\n\n"
                                "Press a name to see its block before and after, change its VR settings or "
                                "convert it to another type.");
            ImGui::PopStyleColor();
        } else if (blocks.empty()) {
            ui::ui_wrapped_text("This vrconfig file has no manipulator blocks. Extract manipulators to vrconfig "
                                "adds them.");
        } else if (shown.empty()) {
            ImGui::TextDisabled("No block matches the ticked types, Find and the group.");
        } else if (ImGui::BeginTable("##blockrows", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn("tags", ImGuiTableColumnFlags_WidthFixed, 128.0f);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(shown.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const BlockInfo& info = *shown[static_cast<std::size_t>(row)];
                    ImGui::PushID(static_cast<int>(info.index));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    // The name is the button; orange = unsaved change, green = saved in this run.
                    const float nameW = ImGui::GetContentRegionAvail().x;
                    std::string name = info.removed ? std::string(ICON_FA_TIMES " ") + info.name
                                     : info.changed ? std::string(ICON_FA_CIRCLE " ") + info.name
                                     : info.saved   ? std::string(ICON_FA_CHECK " ") + info.name
                                                    : info.name;
                    name = ui::EllipsisMiddle(name, nameW - style.FramePadding.x * 2.0f) + "##name";
                    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
                    const ImVec4& color = info.changed ? theme::kChanged : info.saved ? theme::kSaved : theme::kButton;
                    if (ui::ui_color_button(name.c_str(), ImVec2(nameW, 0.0f), color, info.canEdit))
                        OpenEditor(info.index);
                    ImGui::PopStyleVar();

                    ImGui::TableSetColumnIndex(1);
                    ImGui::AlignTextToFramePadding();
                    const std::string typeText = info.action.empty() ? TypeLabelFor(info.type)
                                                                     : ICON_FA_ARROW_RIGHT " " + TypeLabelFor(info.action);
                    ui::ui_text_ellipsis(typeText, 96.0f, info.action.empty() ? nullptr : &theme::kSameFolderText);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::AlignTextToFramePadding();
                    // The most important tag first; the popup lists them all.
                    if (!info.canEdit)
                        ImGui::TextColored(theme::kErrorText, ICON_FA_EXCLAMATION_TRIANGLE " no END_MANIP");
                    else if (info.duplicateOf != 0)
                        ImGui::TextColored(theme::kChangedText, ICON_FA_EXCLAMATION_TRIANGLE " duplicate");
                    else if (info.notInObj)
                        ImGui::TextColored(theme::kChangedText, ICON_FA_EXCLAMATION_TRIANGLE " not in OBJ");
                    else {
                        std::string tags;
                        if (info.removed)    tags += "removed ";
                        if (info.handEdited) tags += "hand edited ";
                        if (info.spring)     tags += ICON_FA_COMPRESS_ARROWS_ALT " hold/spring ";
                        if (info.empty)      tags += "default ";
                        if (info.prefixed)   tags += "CMND= ";
                        ui::ui_text_ellipsis(tags, 128.0f, &theme::kTextDisabled);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void UpdaterPage::ui_status_and_save()
{
    const std::size_t changes = mUpdater.IsOpen() ? mUpdater.ChangeCount() : 0;
    const bool forLoaded = mUpdater.IsOpen() && mUpdater.ForLoadedAircraft();

    if (!mError.empty()) {
        ui::ui_status_strip(std::string(ICON_FA_EXCLAMATION_TRIANGLE " ") + mError, theme::kError, theme::kTextOnColor);
    } else if (changes > 0) {
        ui::ui_status_strip(std::format(ICON_FA_CIRCLE " {} - nothing is written before Save",
                                        Plural(changes, "unsaved update", "unsaved updates")),
                            theme::kChanged, theme::kTextOnColor);
    } else if (!mNotice.empty()) {
        ui::ui_status_strip(mNotice, theme::kPanelBgDark, theme::kText);
    } else if (mUpdater.IsOpen() && mUpdater.LastSaveTime()) {
        ui::ui_status_strip(std::format(ICON_FA_CHECK " Saved at {} - {}", hotspot::ClockTime(*mUpdater.LastSaveTime()),
                                        forLoaded ? "reload the aircraft to use it"
                                                  : "X-Plane reads it when that aircraft loads"),
                            theme::kSaved, theme::kTextOnColor);
    } else {
        ui::ui_status_strip("No unsaved updates", theme::kPanelBgDark, theme::kText);
    }

    const float  helpW = ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 vec_button_size(ui::ui_fill_width(3, helpW), 40.0f);
    if (ui::ui_color_button(ICON_FA_SAVE " Save", vec_button_size, theme::kChanged, theme::kChangedHover, theme::kChangedActive,
                            changes > 0))
        DoSave(false);
    ImGui::SameLine();
    if (ui::ui_button(ICON_FA_TIMES " Cancel changes", vec_button_size, changes > 0))
        mAskCancel = true;
    ImGui::SameLine();
    if (ui::ui_button(ICON_FA_HISTORY " Restore backup", vec_button_size, mUpdater.HasBackup()))
        mAskRestore = true;
    ImGui::SameLine();
    if (const auto backupTime = mUpdater.BackupTime()) {
        const std::string help = std::format(
            "Backup of the vrconfig file before the first update saved, made at {}{} "
            "(<aircraft>_vrconfig.txt.updater.bak). Restoring undoes every update saved since then, and "
            "discards unsaved ones.", hotspot::ClockTime(*backupTime),
            mUpdater.BackupMadeThisRun() ? " in this X-Plane run" : " in an earlier X-Plane run");
        ui::ui_help_marker(help.c_str());
    } else {
        ui::ui_help_marker("A backup (<aircraft>_vrconfig.txt.updater.bak) is made the first time you save "
                           "updates of this file in this X-Plane run. It is apart from the hotspot and Extract "
                           "backups.");
    }
}

void UpdaterPage::DoSave(bool overwriteOutsideChanges)
{
    mError.clear();
    switch (mUpdater.Save(overwriteOutsideChanges)) {
        case SaveResult::Saved:
            mNotice.clear();
            mOpenResult = UpdaterOpen::Opened;
            break;
        case SaveResult::ChangedOnDisk:
            mAskOverwrite = true;
            break;
        case SaveResult::Failed:
            mError = "Save failed: " + mUpdater.LastError();
            break;
        case SaveResult::NothingToSave:
            break;
    }
}

// --- edit popup ----------------------------------------------------------------------------------

void UpdaterPage::OpenEditor(std::size_t block)
{
    if (!mUpdater.CanEdit(block))
        return;
    mEditIndex  = block;
    mEditBefore = mUpdater.SavedLines(block);
    std::string error;
    const auto saved = ParseBlock(mEditBefore, error);
    mEditIdentityBefore = saved ? saved->Identity() : std::string();
    mEditBase = ParseBlock(mUpdater.IsRemoved(block) ? mEditBefore : mUpdater.CurrentLines(block), error);
    mEditNoStructured.clear();
    mEditSettings = {};
    mEditOriginal = {};
    if (mEditBase) {
        if (const auto settings = ReadSettings(*mEditBase, mEditNoStructured)) {
            mEditSettings = *settings;
            mEditOriginal = *settings;
        }
    } else {
        mEditNoStructured = error;
    }
    // A block typed by hand stays text: the structured editor would rewrite it.
    mEditFreeText = mUpdater.IsHandEdited(block) || !mEditNoStructured.empty() || !mEditBase;
    const std::string text = JoinLines(mUpdater.IsRemoved(block) ? mEditBefore : mUpdater.CurrentLines(block));
    mEditText.assign(kTextBufferSize, '\0');
    std::copy_n(text.begin(), std::min(text.size(), kTextBufferSize - 1), mEditText.begin());
    mEditTextStart = text;
    mAskEdit = true;
    LOG_DEBUG("edit '{}' (line {})", ManipUpdater::BlockName(mUpdater.Record(block)), mUpdater.Record(block).lineNumber);
}

std::vector<std::string> UpdaterPage::EditAfterLines() const
{
    if (mEditFreeText)
        return SplitLines(std::string_view(mEditText.data()));
    if (!mEditBase)
        return {};
    return ApplySettings(*mEditBase, mEditSettings, mActions).Lines();
}

std::vector<std::string> UpdaterPage::EditPrefill(const ActionDef& action) const
{
    std::vector<std::string> values;
    const PrefillRule* rule = action.PrefillFrom(mEditBase->type);
    const ObjManip* obj = ObjFor(mEditBase->type, mEditBase->refs, mEditBase->tooltip);
    const ManipTypeDef* typeDef = FindType(mTypes, mEditBase->type);
    const bool commandRefs = typeDef && typeDef->commandRefs;
    std::size_t nextCommand = 0;
    for (std::size_t i = 0; i < action.params.size(); ++i) {
        const ParamDef& param = action.params[i];
        std::string value = param.defaultValue;
        const int objParam = rule && i < rule->objParams.size() ? rule->objParams[i] : -1;
        if (obj && objParam >= 0 && static_cast<std::size_t>(objParam) < obj->params.size()) {
            value = obj->params[static_cast<std::size_t>(objParam)];
        } else if (param.kind == ParamKind::Dataref && !commandRefs && !mEditBase->refs.empty()) {
            value = mEditBase->refs.front();
        } else if (param.kind == ParamKind::Command && commandRefs && nextCommand < mEditBase->refs.size()) {
            value = mEditBase->refs[nextCommand];
        }
        if (param.kind == ParamKind::Command)
            ++nextCommand;
        values.push_back(value);
    }
    return values;
}

void UpdaterPage::ui_popup_edit()
{
    const ImVec2 vec_window_pos  = ImGui::GetWindowPos();
    const ImVec2 vec_window_size = ImGui::GetWindowSize();
    ImGui::SetNextWindowPos(ImVec2(vec_window_pos.x + vec_window_size.x * 0.5f, vec_window_pos.y + vec_window_size.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(vec_window_size.x - 24.0f, vec_window_size.y - 24.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopupEdit, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                                       ImGuiWindowFlags_NoScrollbar))
        return;
    if (!mUpdater.IsOpen() || mEditIndex >= mUpdater.BlockCount()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const vrconfig::ManipRecord& record = mUpdater.Record(mEditIndex);
    const ImGuiStyle& style = ImGui::GetStyle();

    // Title: name, type (help), line; then what is known about the block.
    ImGui::TextUnformatted(ManipUpdater::BlockName(record).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", std::format("{} - line {}", record.type, record.lineNumber).c_str());
    ImGui::SameLine();
    ui::ui_help_marker(TypeHelpFor(record.type).c_str());
    {
        const std::vector<BlockInfo> blocks = Blocks();
        const BlockInfo& info = blocks[mEditIndex];
        std::vector<std::pair<const ImVec4*, std::string>> notes;
        if (info.duplicateOf != 0)
            notes.emplace_back(&theme::kChangedText, std::format(ICON_FA_EXCLAMATION_TRIANGLE " Same type, references and "
                               "tooltip as line {} - X-Plane can't tell them apart.", info.duplicateOf));
        if (info.notInObj)
            notes.emplace_back(&theme::kChangedText, ICON_FA_EXCLAMATION_TRIANGLE " Not found in the aircraft's OBJ "
                               "files - X-Plane may show an error for this block when the aircraft loads.");
        if (info.prefixed)
            notes.emplace_back(&theme::kTextDisabled, ICON_FA_INFO_CIRCLE " A reference is written with CMND= / DREF=; "
                               "it is matched without the prefix and kept as written.");
        if (mObjState != ObjState::Read)
            notes.emplace_back(&theme::kTextDisabled, ICON_FA_INFO_CIRCLE " The OBJ files are not checked - "
                               "conversions use default values.");
        for (const auto& [color, text] : notes)
            ui::ui_wrapped_text(text, color);
    }
    if (mUpdater.IsRemoved(mEditIndex)) {
        ui::ui_wrapped_text(ICON_FA_TIMES " This block is removed when you Save.", &theme::kChangedText);
        ImGui::Spacing();
        const ImVec2 vec_button_size((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f, 40.0f);
        if (ui::ui_button(ICON_FA_UNDO " Keep the block", vec_button_size)) {
            mUpdater.Revert(mEditIndex);
            OpenEditor(mEditIndex);                              // show it as it is
        }
        ImGui::SameLine();
        if (ui::ui_button("Close", vec_button_size))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    // Before.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Before (saved)");
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ButtonWidth(ICON_FA_COPY " Copy##before"));
    if (ui::ui_button(ICON_FA_COPY " Copy##before"))
        ImGui::SetClipboardText(JoinLines(mEditBefore).c_str());
    ReadOnlyText("##before", JoinLines(mEditBefore),
                 ImVec2(-1.0f, ImGui::GetTextLineHeight() * kBeforeLines + style.FramePadding.y * 2.0f));

    // Change: the structured editor (or why it cannot be used).
    if (!mEditFreeText)
        ui_edit_structured();

    const std::vector<std::string> after = EditAfterLines();
    ui_edit_after(after);
    std::vector<BlockCheck> checks = CheckBlock(after, MakeCheckContext(mEditIdentityBefore));
    if (!mEditFreeText) {
        // Structured: every value of a ticked setting / the ACTION must be filled in.
        auto lmbda_missing = [&checks](const std::string& what, const std::vector<std::string>& values) {
            for (const std::string& value : values) {
                if (value.find_first_not_of(" \t") == std::string::npos) {
                    checks.insert(checks.begin(), BlockCheck{true, std::format("Fill in every value of {}.", what)});
                    return;
                }
            }
        };
        if (mEditSettings.hasAction)
            lmbda_missing("ACTION " + mEditSettings.actionName, mEditSettings.actionParams);
        for (const auto& [keyword, values] : mEditSettings.settings)
            lmbda_missing(keyword, values);
    }
    ui_edit_checks(checks);
    ui_edit_buttons(after, checks);
    ui_popup_discard_text();
    ImGui::EndPopup();
}

void UpdaterPage::ui_edit_structured()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Change");
    ImGui::SameLine();
    ui::ui_help_marker("Pick an ACTION to convert the manipulator, and tick the settings it should have. Each "
                       "one has a help marker. The After box shows the lines Save will write.\n"
                       "Lines the plugin does not know (comments, other directives) are kept as they are.");
    const float height = ImGui::GetContentRegionAvail().y - BelowChangeHeight();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kInputBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::kPanelBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme::kPanelBgDark);
    if (ImGui::BeginChild("##change", ImVec2(0.0f, std::max(120.0f, height)), ImGuiChildFlags_Borders)) {
        const std::string effective = mEditSettings.hasAction ? mEditSettings.actionName : mEditBase->type;
        ui_edit_action(effective);
        ImGui::Spacing();
        ImGui::Separator();
        ui_edit_settings(mEditSettings.hasAction ? mEditSettings.actionName : mEditBase->type);
        const std::size_t others = static_cast<std::size_t>(std::count_if(mEditBase->body.begin(), mEditBase->body.end(),
            [](const BlockLine& line) { return !line.keyword.empty() && !vrconfig::IsManipDirective(line.keyword); }));
        if (others > 0)
            ImGui::TextDisabled("%s", std::format("{} kept as written", Plural(others, "other line", "other lines")).c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3);
    (void)style;
}

void UpdaterPage::ui_edit_action(const std::string& effectiveType)
{
    (void)effectiveType;
    const std::string& type = mEditBase->type;
    const ActionDef* current = mEditSettings.hasAction ? mActions.FindAction(mEditSettings.actionName) : nullptr;

    auto lmbda_label = [&type](const ActionDef& action) {
        std::string mark = action.ConfirmedFor(type) ? ICON_FA_CHECK " " : ICON_FA_EXCLAMATION_TRIANGLE " ";
        return mark + action.label + (action.version >= 12 ? "  (X-Plane 12)" : "");
    };
    const std::string preview = !mEditSettings.hasAction ? std::format("none - stays {}", type)
                              : current ? lmbda_label(*current)
                                        : std::format(ICON_FA_EXCLAMATION_TRIANGLE " {} (not in the actions file)",
                                                      mEditSettings.actionName);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("ACTION");
    ImGui::SameLine(90.0f);
    const float helpW = ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - helpW);
    if (ImGui::BeginCombo("##action", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable(std::format("none - stays {}", type).c_str(), !mEditSettings.hasAction)) {
            mEditSettings.hasAction = false;
            mEditSettings.actionName.clear();
            mEditSettings.actionParams.clear();
        }
        for (const ActionDef& action : mActions.actions) {
            const bool selected = mEditSettings.hasAction && mEditSettings.actionName == action.name;
            if (!action.OfferedFor(type) && !selected)
                continue;
            ImGui::PushStyleColor(ImGuiCol_Text, action.ConfirmedFor(type) ? theme::kText : theme::kChangedText);
            if (ImGui::Selectable(std::format("{}##{}", lmbda_label(action), action.name).c_str(), selected) && !selected) {
                mEditSettings.hasAction  = true;
                mEditSettings.actionName = action.name;
                mEditSettings.actionParams = (mEditOriginal.hasAction && mEditOriginal.actionName == action.name)
                                                 ? mEditOriginal.actionParams
                                                 : EditPrefill(action);
                LOG_DEBUG_AS("lmbda_action", "ACTION {} for {}", action.name, type);
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (current) {
        std::string help = current->help;
        help += current->ConfirmedFor(type)
            ? std::format("\n\n" ICON_FA_CHECK " Laminar's own aircraft convert {} this way.", type)
            : std::format("\n\n" ICON_FA_EXCLAMATION_TRIANGLE " Not confirmed for {}: not seen in Laminar's aircraft. "
                          "Try it in VR.", type);
        if (current->version >= 12)
            help += "\nNeeds X-Plane 12 or later.";
        ui::ui_help_marker(help.c_str());
    } else {
        ui::ui_help_marker("ACTION makes X-Plane treat the manipulator as another type in VR, e.g. a knob that "
                           "runs commands as a knob on a dataref.\n" ICON_FA_CHECK " = Laminar's own aircraft "
                           "convert this type that way, " ICON_FA_EXCLAMATION_TRIANGLE " = not confirmed.");
    }
    if (current && mEditSettings.hasAction) {
        mEditSettings.actionParams.resize(current->params.size());
        ui_param_rows("##actionparams", current->params, mEditSettings.actionParams, current->reverseParam);
    }
}

void UpdaterPage::ui_edit_settings(const std::string& effectiveType)
{
    std::set<std::string> groupsShown;
    for (const SettingDef& setting : mActions.settings) {
        const bool present = mEditSettings.Has(setting.keyword);
        if (!present && !SettingOffered(setting, effectiveType, mTypes))
            continue;
        ImGui::PushID(setting.keyword.c_str());
        if (!setting.group.empty()) {
            // A group (realistic yoke): one combo for all its members.
            if (groupsShown.insert(setting.group).second) {
                std::vector<const SettingDef*> members;
                const SettingDef* chosen = nullptr;
                for (const SettingDef& other : mActions.settings) {
                    if (other.group != setting.group)
                        continue;
                    if (mEditSettings.Has(other.keyword) || SettingOffered(other, effectiveType, mTypes))
                        members.push_back(&other);
                    if (mEditSettings.Has(other.keyword) && !chosen)
                        chosen = &other;
                }
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(setting.group.c_str());
                ImGui::SameLine(110.0f);
                const float helpW = ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - helpW);
                if (ImGui::BeginCombo("##group", chosen ? chosen->label.c_str() : "none")) {
                    if (ImGui::Selectable("none", chosen == nullptr)) {
                        for (const SettingDef* member : members)
                            mEditSettings.settings.erase(member->keyword);
                    }
                    for (const SettingDef* member : members) {
                        if (ImGui::Selectable(member->label.c_str(), member == chosen) && member != chosen) {
                            for (const SettingDef* other : members)
                                mEditSettings.settings.erase(other->keyword);
                            std::vector<std::string> values;
                            if (mEditOriginal.Has(member->keyword)) {
                                values = mEditOriginal.settings.at(member->keyword);
                            } else {
                                for (const ParamDef& param : member->params)
                                    values.push_back(param.defaultValue);
                            }
                            mEditSettings.settings[member->keyword] = values;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                std::string help = setting.group + ": only one of these per block.";
                for (const SettingDef* member : members)
                    help += "\n\n" + member->keyword + ": " + member->help;
                ui::ui_help_marker(help.c_str());
                if (chosen) {
                    std::vector<std::string>& values = mEditSettings.settings[chosen->keyword];
                    values.resize(chosen->params.size());
                    ui_param_rows("##groupparams", chosen->params, values, chosen->reverseParam);
                }
            }
            ImGui::PopID();
            continue;
        }

        bool ticked = present;
        if (ImGui::Checkbox(setting.label.c_str(), &ticked)) {
            if (ticked) {
                std::vector<std::string> values;
                if (mEditOriginal.Has(setting.keyword)) {
                    values = mEditOriginal.settings.at(setting.keyword);
                } else {
                    for (const ParamDef& param : setting.params)
                        values.push_back(param.defaultValue);
                }
                mEditSettings.settings[setting.keyword] = values;
            } else {
                mEditSettings.settings.erase(setting.keyword);
            }
        }
        // A single value goes on the same line.
        if (ticked && setting.params.size() == 1) {
            ImGui::SameLine();
            std::vector<std::string>& values = mEditSettings.settings[setting.keyword];
            values.resize(1);
            const float helpW = ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x;
            InputString("##value", values[0], std::min(90.0f, ImGui::GetContentRegionAvail().x - helpW),
                        setting.params[0].kind == ParamKind::Number ? ImGuiInputTextFlags_CharsScientific : 0);
        }
        ImGui::SameLine();
        std::string help = setting.keyword + ": " + setting.help;
        for (const ParamDef& param : setting.params)
            help += std::format("\n- {}: {}", param.name, param.help);
        if (setting.version >= 12)
            help += "\nNeeds X-Plane 12 or later.";
        if (!SettingOffered(setting, effectiveType, mTypes))
            help += std::format("\nNot usually used with {}.", effectiveType);
        ui::ui_help_marker(help.c_str());
        if (ticked && setting.params.size() > 1) {
            std::vector<std::string>& values = mEditSettings.settings[setting.keyword];
            values.resize(setting.params.size());
            ui_param_rows("##params", setting.params, values, setting.reverseParam);
        }
        ImGui::PopID();
    }
}

// Rows of value fields. Names sharing a prefix ("pitch_center_x/y/z") share a
// row; the entry's `reverse` param gets a Reverse tick (flips its sign).
bool UpdaterPage::ui_param_rows(const char* id, const std::vector<ParamDef>& params, std::vector<std::string>& values,
                                const std::string& reverseParam)
{
    bool changed = false;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushID(id);
    ImGui::Indent(16.0f);
    std::size_t i = 0;
    while (i < params.size()) {
        const std::size_t underscore = params[i].name.rfind('_');
        const std::string prefix = underscore == std::string::npos ? params[i].name : params[i].name.substr(0, underscore);
        std::size_t end = i + 1;
        while (underscore != std::string::npos && end < params.size() && params[end].name.rfind('_') != std::string::npos &&
               params[end].name.substr(0, params[end].name.rfind('_')) == prefix)
            ++end;
        std::string label = prefix;
        std::replace(label.begin(), label.end(), '_', ' ');

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine(16.0f + 104.0f);
        const float helpW   = ImGui::CalcTextSize("(?)").x + style.ItemSpacing.x;
        const bool  isStep  = end - i == 1 && !reverseParam.empty() && params[i].name == reverseParam;
        const float reverseW = isStep ? ImGui::GetFrameHeight() + ImGui::CalcTextSize("Reverse").x + style.ItemInnerSpacing.x +
                                        style.ItemSpacing.x : 0.0f;
        const std::size_t count = end - i;
        float suffixW = 0.0f;
        if (count > 1) {
            for (std::size_t k = i; k < end; ++k)
                suffixW += ImGui::CalcTextSize(params[k].name.substr(params[k].name.rfind('_') + 1).c_str()).x +
                           style.ItemInnerSpacing.x;
        }
        const float fieldW = (ImGui::GetContentRegionAvail().x - helpW - reverseW - suffixW -
                              style.ItemSpacing.x * static_cast<float>(count - 1)) / static_cast<float>(count);
        std::string help;
        for (std::size_t k = i; k < end; ++k) {
            ImGui::PushID(static_cast<int>(k));
            if (count > 1) {
                if (k > i)
                    ImGui::SameLine();
                ImGui::TextDisabled("%s", params[k].name.substr(params[k].name.rfind('_') + 1).c_str());
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            }
            const ImGuiInputTextFlags flags = params[k].kind == ParamKind::Number ? ImGuiInputTextFlags_CharsScientific : 0;
            changed |= InputString("##v", values[k], fieldW, flags);
            const std::string line = std::format("{} ({}): {}", params[k].name, ParamKindName(params[k].kind),
                                                 params[k].help);
            if (help.find(params[k].help) == std::string::npos || params[k].help.empty())
                help += (help.empty() ? "" : "\n") + line;
            ImGui::PopID();
        }
        if (isStep) {
            ImGui::SameLine();
            bool reversed = !values[i].empty() && values[i].front() == '-';
            if (ImGui::Checkbox("Reverse", &reversed)) {
                if (reversed && (values[i].empty() || values[i].front() != '-'))
                    values[i] = "-" + (values[i].empty() ? std::string("0.1") : values[i]);
                else if (!reversed && !values[i].empty() && values[i].front() == '-')
                    values[i].erase(0, 1);
                changed = true;
            }
            help += std::format("\nReverse: flips the sign of {}, so the control works the other way round.", reverseParam);
        }
        ImGui::SameLine();
        ui::ui_help_marker(help.c_str());
        i = end;
    }
    ImGui::Unindent(16.0f);
    ImGui::PopID();
    return changed;
}

void UpdaterPage::ui_edit_after(const std::vector<std::string>& afterLines)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(mEditFreeText ? "After (your text)" : "After (written on Save)");
    const char* copy  = ICON_FA_COPY " Copy##after";
    const char* paste = ICON_FA_PASTE " Paste";
    const float right = ImGui::GetContentRegionMax().x;
    ImGui::SameLine(right - ButtonWidth(copy) - (mEditFreeText ? ButtonWidth(paste) + ImGui::GetStyle().ItemSpacing.x : 0.0f));
    if (ui::ui_button(copy))
        ImGui::SetClipboardText(JoinLines(afterLines).c_str());
    if (mEditFreeText) {
        ImGui::SameLine(right - ButtonWidth(paste));
        if (ui::ui_button(paste)) {
            const char* clip = ImGui::GetClipboardText();
            const std::string text = clip ? clip : "";
            std::fill(mEditText.begin(), mEditText.end(), '\0');
            std::copy_n(text.begin(), std::min(text.size(), kTextBufferSize - 1), mEditText.begin());
        }
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float boxH = mEditFreeText
        ? std::max(120.0f, ImGui::GetContentRegionAvail().y - (ImGui::GetFrameHeightWithSpacing() + ChecksHeight() +
                                                              kButtonRowH + style.ItemSpacing.y * 3.0f))
        : ImGui::GetTextLineHeight() * kAfterLines + style.FramePadding.y * 2.0f;
    if (mEditFreeText) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::kInputBg);
        ImGui::InputTextMultiline("##aftertext", mEditText.data(), mEditText.size(), ImVec2(-1.0f, boxH),
                                  ImGuiInputTextFlags_AllowTabInput);
        ImGui::PopStyleColor();
    } else {
        ReadOnlyText("##after", JoinLines(afterLines), ImVec2(-1.0f, boxH));
    }

    bool freeText = mEditFreeText;
    ImGui::BeginDisabled(!mEditNoStructured.empty() && mEditFreeText);       // no way back to the structured editor
    if (ImGui::Checkbox("Edit the After text myself (advanced)", &freeText)) {
        if (freeText) {
            const std::string text = JoinLines(afterLines);
            mEditText.assign(kTextBufferSize, '\0');
            std::copy_n(text.begin(), std::min(text.size(), kTextBufferSize - 1), mEditText.begin());
            mEditTextStart = text;
            mEditFreeText  = true;
        } else if (std::string(mEditText.data()) != mEditTextStart) {
            mAskDiscardText = true;                                         // asks first
        } else {
            mEditFreeText = false;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ui::ui_help_marker(mEditNoStructured.empty()
        ? "For what the Change area cannot offer: type the block yourself, on your responsibility. It must "
          "stay one block, from BEGIN_MANIP to END_MANIP. When you apply it, it is checked: errors must be "
          "fixed, warnings can be applied anyway. The block then stays text (marked \"hand edited\")."
        : ("The Change area cannot show this block: " + mEditNoStructured + ". Edit it as text.").c_str());
}

void UpdaterPage::ui_edit_checks(const std::vector<BlockCheck>& checks)
{
    const float height = ChecksHeight();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kPanelBg);
    if (ImGui::BeginChild("##checks", ImVec2(0.0f, height), ImGuiChildFlags_Borders)) {
        if (checks.empty()) {
            ImGui::TextColored(theme::kSavedText, ICON_FA_CHECK " No problem found.");
        } else {
            for (const BlockCheck& check : checks) {
                ui::ui_wrapped_text(std::string(check.blocking ? ICON_FA_TIMES_CIRCLE " " : ICON_FA_EXCLAMATION_TRIANGLE " ") +
                                    check.text, check.blocking ? &theme::kErrorText : &theme::kChangedText);
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void UpdaterPage::ui_edit_buttons(const std::vector<std::string>& afterLines, const std::vector<BlockCheck>& checks)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool blocking = std::any_of(checks.begin(), checks.end(), [](const BlockCheck& check) { return check.blocking; });
    const bool warnings = !checks.empty() && !blocking;
    const bool changed  = afterLines != mUpdater.CurrentLines(mEditIndex) ||
                          (mEditFreeText != mUpdater.IsHandEdited(mEditIndex) && afterLines != mEditBefore);
    // Apply is wider: "Apply anyway" must fit.
    const float unit = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * 3.0f) / 4.6f;
    const ImVec2 vec_button_size(unit, kButtonRowH);
    const ImVec2 vec_apply_button_size(unit * 1.6f, kButtonRowH);

    const char* apply = warnings ? ICON_FA_CHECK " Apply anyway" : ICON_FA_CHECK " Apply";
    if (ui::ui_color_button(apply, vec_apply_button_size, warnings ? theme::kChanged : theme::kSaved, !blocking && changed)) {
        mUpdater.SetBlock(mEditIndex, afterLines, mEditFreeText);
        mNotice.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::ui_color_button(ICON_FA_TRASH " Remove", vec_button_size, theme::kError)) {
        mUpdater.RemoveBlock(mEditIndex);
        mNotice.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::ui_button(ICON_FA_UNDO " Revert", vec_button_size, mUpdater.IsChanged(mEditIndex))) {
        mUpdater.Revert(mEditIndex);
        OpenEditor(mEditIndex);
        mAskEdit = false;                                    // the popup is already open
    }
    ImGui::SameLine();
    if (ui::ui_button("Close", vec_button_size))
        ImGui::CloseCurrentPopup();
}

void UpdaterPage::ui_popup_discard_text()
{
    if (mAskDiscardText) {
        ImGui::OpenPopup(kPopupDiscard);
        mAskDiscardText = false;
    }
    ui::YesNoText text;
    text.message  = "Back to the Change area? The text you typed is discarded.";
    text.yesLabel = "Discard my text";
    text.noLabel  = "Keep editing";
    text.yesColor = theme::kChanged;
    if (ui::ui_popup_yes_no(kPopupDiscard, text) == PopupAnswer::Yes)
        mEditFreeText = false;
}

// --- other popups --------------------------------------------------------------------------------

void UpdaterPage::ui_popups()
{
    if (mAskExplain)   { ImGui::OpenPopup(kPopupExplain);   mAskExplain   = false; }
    if (mAskNotes)     { ImGui::OpenPopup(kPopupNotes);     mAskNotes     = false; }
    if (mAskEdit)      { ImGui::OpenPopup(kPopupEdit);      mAskEdit      = false; }
    if (mAskCancel)    { ImGui::OpenPopup(kPopupCancel);    mAskCancel    = false; }
    if (mAskRestore)   { ImGui::OpenPopup(kPopupRestore);   mAskRestore   = false; }
    if (mAskOverwrite) { ImGui::OpenPopup(kPopupOverwrite); mAskOverwrite = false; }

    ui_popup_explain();
    ui_popup_notes();
    ui_popup_edit();
    ui_popup_cancel_changes();
    ui_popup_restore_backup();
    ui_popup_overwrite_file();
}

void UpdaterPage::ui_popup_explain()
{
    ui::ui_popup_message(kPopupExplain, kExplainText);
}

void UpdaterPage::ui_popup_notes()
{
    std::string text = "Lines of the file X-Plane may read differently than intended:\n";
    if (mUpdater.IsOpen()) {
        for (const std::string& note : mUpdater.File().ManipNotes())
            text += "\n- " + note;
    }
    ui::ui_popup_message(kPopupNotes, text);
}

void UpdaterPage::ui_popup_cancel_changes()
{
    ui::YesNoText text;
    text.message = std::format("Discard {}?", Plural(mUpdater.ChangeCount(), "unsaved update", "unsaved updates"));
    const std::vector<std::string> lines = mUpdater.ChangeLines();
    for (std::size_t i = 0; i < lines.size() && i < kMaxListedChanges; ++i)
        text.details.push_back(lines[i]);
    if (lines.size() > kMaxListedChanges)
        text.details.push_back(std::format("... and {} more", lines.size() - kMaxListedChanges));
    text.yesLabel = "Discard all";
    text.noLabel  = "Keep";
    text.yesColor = theme::kChanged;
    if (ui::ui_popup_yes_no(kPopupCancel, text) == PopupAnswer::Yes) {
        mUpdater.CancelChanges();
        mNotice.clear();
    }
}

void UpdaterPage::ui_popup_restore_backup()
{
    ui::YesNoText text;
    const auto backupTime = mUpdater.BackupTime();
    text.message = std::format("The vrconfig file will be replaced by its updater backup{}. Every update saved "
                               "since then is undone.",
                               backupTime ? " from " + hotspot::ClockTime(*backupTime) : std::string());
    if (mUpdater.ChangeCount() > 0)
        text.note = std::format("Your {} discarded too.",
                                Plural(mUpdater.ChangeCount(), "unsaved update is", "unsaved updates are"));
    text.yesLabel = "Restore";
    text.noLabel  = "Keep current";
    if (ui::ui_popup_yes_no(kPopupRestore, text) == PopupAnswer::Yes) {
        mError.clear();
        if (mUpdater.RestoreBackup()) {
            mOpenResult = UpdaterOpen::Opened;
            mNotice = mUpdater.ForLoadedAircraft() ? ICON_FA_HISTORY " Backup restored - reload the aircraft to use it"
                                                   : ICON_FA_HISTORY " Backup restored";
        } else {
            mError = "Restore failed: " + mUpdater.LastError();
        }
    }
}

void UpdaterPage::ui_popup_overwrite_file()
{
    ui::YesNoText text;
    text.message  = "The vrconfig file was changed by another program since VRConfig Helper read it.";
    text.note     = "Save anyway: your updates are applied to the blocks of the file as it is now.";
    text.yesLabel = "Save anyway";
    text.noLabel  = "Don't save";
    text.yesColor = theme::kChanged;
    if (ui::ui_popup_yes_no(kPopupOverwrite, text) == PopupAnswer::Yes)
        DoSave(true);
}

} // namespace hpvr::manip

// hpvr - the plugin's only window

#include "PluginWindow.h"

#include <cmath>
#include <format>
#include <functional>

#include "imgui.h"
#include "IconsFontAwesome5.h"

#include "Log.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace hpvr {

#ifndef VRCONFIG_HELPER_VERSION
#define VRCONFIG_HELPER_VERSION "0.0.0"
#endif

namespace {

constexpr float kHomeButtonHeight = 56.0f;   // large targets for VR controllers
constexpr const char* kPopupAbout = "About VRConfig Helper";
constexpr float kLogoSize = 112.0f;

// The plugin's logo, drawn (no image file): a VR headset whose right lens is a
// knob with a pointer, over a teleport ring, on a rounded blue badge.
void ui_draw_logo(ImDrawList* draw, ImVec2 origin, float size)
{
    const float unit = size / 100.0f;
    auto lmbda_at = [&](float x, float y) { return ImVec2(origin.x + x * unit, origin.y + y * unit); };
    const ImU32 badge   = ImGui::GetColorU32(ui::theme::kButton);
    const ImU32 badgeHi = ImGui::GetColorU32(ui::theme::kButtonHover);
    const ImU32 white   = ImGui::GetColorU32(ui::theme::kTextOnColor);
    const ImU32 accent  = ImGui::GetColorU32(ui::theme::kSaved);
    const ImU32 shadow  = IM_COL32(0, 0, 0, 60);

    draw->AddRectFilled(lmbda_at(2, 4), lmbda_at(100, 102), shadow, 20.0f * unit);
    draw->AddRectFilled(lmbda_at(0, 0), lmbda_at(98, 98), badge, 20.0f * unit);
    draw->AddRectFilled(lmbda_at(0, 0), lmbda_at(98, 46), IM_COL32(255, 255, 255, 28), 20.0f * unit,
                        ImDrawFlags_RoundCornersTop);                                   // soft highlight

    // Teleport ring under the headset.
    draw->AddEllipse(lmbda_at(49, 80), ImVec2(30 * unit, 7 * unit), accent, 0.0f, 32, 3.0f * unit);
    draw->AddEllipse(lmbda_at(49, 80), ImVec2(16 * unit, 3.5f * unit), accent, 0.0f, 24, 2.0f * unit);

    // Headset: strap, body, nose cut-out.
    draw->AddLine(lmbda_at(10, 42), lmbda_at(18, 42), white, 4.0f * unit);
    draw->AddLine(lmbda_at(80, 42), lmbda_at(88, 42), white, 4.0f * unit);
    draw->AddRectFilled(lmbda_at(18, 24), lmbda_at(80, 62), white, 12.0f * unit);
    draw->AddTriangleFilled(lmbda_at(42, 62), lmbda_at(56, 62), lmbda_at(49, 52), badge);
    // Left lens.
    draw->AddCircleFilled(lmbda_at(35, 43), 10.0f * unit, badge, 32);
    draw->AddCircleFilled(lmbda_at(32, 40), 3.0f * unit, badgeHi, 16);
    // Right lens = a knob: dial, ticks, pointer.
    const ImVec2 knob = lmbda_at(63, 43);
    draw->AddCircleFilled(knob, 10.0f * unit, badge, 32);
    for (int i = 0; i < 7; ++i) {
        const float angle = 3.14159f * (0.75f + 0.25f * static_cast<float>(i));      // a 270 degree scale
        const ImVec2 from(knob.x + std::cos(angle) * 6.5f * unit, knob.y + std::sin(angle) * 6.5f * unit);
        const ImVec2 to(knob.x + std::cos(angle) * 9.0f * unit, knob.y + std::sin(angle) * 9.0f * unit);
        draw->AddLine(from, to, white, 1.2f * unit);
    }
    const float pointer = 3.14159f * 1.6f;
    draw->AddLine(knob, ImVec2(knob.x + std::cos(pointer) * 6.0f * unit, knob.y + std::sin(pointer) * 6.0f * unit),
                  accent, 2.5f * unit);
    draw->AddCircleFilled(knob, 2.0f * unit, accent, 12);
}

} // namespace

// The pages' "Home" button.
static std::function<void()> MakeGoHome(PluginWindow* window)
{
    auto lmbda_goHome = [window]() { window->ShowPage(Page::Home); };
    return lmbda_goHome;
}

static const char* PageName(Page page)
{
    switch (page) {
        case Page::Home:    return "Home";
        case Page::Hotspot: return "Hotspot";
        case Page::Manip:   return "Manip";
        case Page::Updater: return "Updater";
    }
    return "";
}

PluginWindow::PluginWindow(hotspot::SpotEditor& spotEditor, manip::ManipEditor& manipEditor,
                           manip::ManipUpdater& updater, const std::filesystem::path& pluginFolder)
    : ImgWindow(WindowGeometry::Centered(kWidth, kHeight, xplm_WindowDecorationRoundRectangle),
                xplm_WindowDecorationRoundRectangle,
                xplm_WindowLayerFloatingWindows),
      mSpotEditor(spotEditor),
      mManipEditor(manipEditor),
      mUpdater(updater),
      mSpotPage(spotEditor, MakeGoHome(this)),
      mManipPage(manipEditor, MakeGoHome(this), pluginFolder),
      mUpdaterPage(updater, MakeGoHome(this), pluginFolder)
{
    SetWindowTitle("VRConfig Helper");
    SetWindowResizingLimits(kWidth, kHeight, kWidth, kHeight);   // fixed size for every page

    // The ImgWindow constructor leaves this window's ImGui context current,
    // so the style set here belongs to this window only.
    ui::theme::ApplyBaseStyle();
}

ImGuiWindowFlags_ PluginWindow::beforeBegin()
{
    return static_cast<ImGuiWindowFlags_>(ImGuiWindowFlags_NoScrollbar |
                                          ImGuiWindowFlags_NoScrollWithMouse);
}

void PluginWindow::ShowPage(Page page)
{
    if (page == Page::Hotspot)
        mSpotPage.Reload();
    else if (page == Page::Manip)
        mManipPage.Reload();
    else if (page == Page::Updater)
        mUpdaterPage.Reload();
    LOG_DEBUG("page {}", PageName(page));
    mPage = page;
}

void PluginWindow::OnAircraftLoaded(bool changesDiscarded, bool updaterDiscarded)
{
    mManipPage.OnAircraftLoaded();                 // step 1 (files) starts over, on any page
    mUpdaterPage.OnAircraftLoaded(updaterDiscarded);
    const char* notice = changesDiscarded ? "Aircraft loaded - unsaved changes were discarded"
                                          : "Aircraft loaded";
    if (mPage == Page::Hotspot) {
        mSpotPage.Reload();
        mSpotPage.SetNotice(notice);
    } else if (mPage == Page::Manip) {
        mManipPage.Reload();
        mManipPage.SetNotice(notice);
    } else if (mPage == Page::Updater) {
        mUpdaterPage.Reload();
        mUpdaterPage.SetNotice(updaterDiscarded ? "Aircraft loaded - unsaved updates of its vrconfig were discarded"
                                                : "Aircraft loaded");
    }
}

void PluginWindow::buildInterface()
{
    switch (mPage) {
        case Page::Home:    ui_home_page();       break;
        case Page::Hotspot: mSpotPage.ui_draw();  break;
        case Page::Manip:   mManipPage.ui_draw(); break;
        case Page::Updater: mUpdaterPage.ui_draw(); break;
    }
}

// `blocked`: why the action cannot be used now (button disabled, reason shown in orange).
bool PluginWindow::ui_home_action(const char* label, const char* description, const std::string& blocked)
{
    const bool pressed = ui::ui_button(label, ImVec2(-1.0f, kHomeButtonHeight), blocked.empty());
    ImGui::PushStyleColor(ImGuiCol_Text, ui::theme::kTextDisabled);
    ui::ui_wrapped_text(description);
    ImGui::PopStyleColor();
    if (!blocked.empty())
        ui::ui_wrapped_text(std::string(ICON_FA_EXCLAMATION_TRIANGLE " ") + blocked, &ui::theme::kChangedText);
    ImGui::Spacing();
    ImGui::Spacing();
    return pressed;
}

void PluginWindow::ui_home_page()
{
    // About, top right; the title centred on the same line.
    const char* about = ICON_FA_INFO_CIRCLE " About";
    const float aboutW = ImGui::CalcTextSize(about).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float top = ImGui::GetCursorPosY();
    ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - aboutW, top));
    if (ui::ui_button(about))
        mAskAbout = true;
    ImGui::SetCursorPosY(top + ImGui::GetStyle().FramePadding.y);
    ui::ui_centered_text("Select an action :");
    ImGui::SetCursorPosY(top + ImGui::GetFrameHeight());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // The pages write vrconfig files: unsaved changes on one page block the others.
    const std::size_t spotChanges    = mSpotEditor.IsOpen() ? mSpotEditor.ChangeCount() : 0;
    const std::size_t manipChanges   = mManipEditor.IsOpen() ? mManipEditor.ChangeCount() : 0;
    const std::size_t updaterChanges = mUpdater.IsOpen() ? mUpdater.ChangeCount() : 0;
    auto lmbda_blocked = [&](bool spot, bool manip, bool updater) {
        std::string why;
        auto lmbda_add = [&why](std::size_t count, const char* one, const char* many, const char* page) {
            if (count == 0)
                return;
            why += (why.empty() ? "" : " ") + std::format("{} on the {} page - save or cancel them first.",
                                                          ui::Plural(count, one, many), page);
        };
        if (spot)    lmbda_add(spotChanges, "unsaved hotspot change", "unsaved hotspot changes", "Hotspot");
        if (manip)   lmbda_add(manipChanges, "unsaved manipulator change", "unsaved manipulator changes",
                               "Extract manipulators");
        if (updater) lmbda_add(updaterChanges, "unsaved manipulator update", "unsaved manipulator updates",
                               "Manipulator Updater");
        return why;
    };

    if (ui_home_action(ICON_FA_MAP_MARKER_ALT "  Hotspot",
                       "Move, turn, rename, add or delete the teleport hotspots of the current "
                       "aircraft (its vrconfig file).", lmbda_blocked(false, true, true)))
        ShowPage(Page::Hotspot);

    if (ui_home_action(ICON_FA_SLIDERS_H "  Extract manipulators to vrconfig",
                       "Finds the knobs, switches and levers in the "
                       "aircraft's OBJ files and adds them to its vrconfig file with default VR "
                       "settings. Step 1 of the VR setup.", lmbda_blocked(true, false, true)))
        ShowPage(Page::Manip);

    if (ui_home_action(ICON_FA_TOOLS "  VRConfig Manipulator Updater",
                       "Tune the knobs, switches and levers already in an aircraft's vrconfig file: how far "
                       "to turn per click, hold/spring switches, wrap-around knobs, the realistic yoke - or "
                       "turn a command knob into a smooth dataref knob. Works for any aircraft.",
                       lmbda_blocked(true, true, false)))
        ShowPage(Page::Updater);

    // Bottom of the page: a request to credit the tool, in black (the theme's text colour) to be easy to read.
    const char* credit = "Developers: if you use VRConfig Helper to build or improve the vrconfig file of an "
                         "aircraft you publish, please credit it in your release notes or documentation, "
                         "e.g. \"VR configuration made with VRConfig Helper\". Thank you!";
    const float wrapW  = ImGui::GetContentRegionAvail().x;
    const float textH  = ImGui::CalcTextSize(credit, nullptr, false, wrapW).y;
    const float bottom = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - textH;
    if (bottom > ImGui::GetCursorPosY()) {
        ImGui::SetCursorPosY(bottom - ImGui::GetStyle().ItemSpacing.y);
        ImGui::Separator();
        ImGui::SetCursorPosY(bottom);
    }
    ui::ui_wrapped_text(credit, &ui::theme::kText);

    if (mAskAbout) {
        ImGui::OpenPopup(kPopupAbout);
        LOG_DEBUG("About opened");
        mAskAbout = false;
    }
    ui_popup_about();
}

void PluginWindow::ui_popup_about()
{
    const ImVec2 windowPos  = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    ImGui::SetNextWindowPos(ImVec2(windowPos.x + windowSize.x * 0.5f, windowPos.y + windowSize.y * 0.4f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(windowSize.x - 60.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopupAbout, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                                        ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 logoAt = ImGui::GetCursorScreenPos();
    ui_draw_logo(ImGui::GetWindowDrawList(), logoAt, kLogoSize);
    ImGui::Dummy(ImVec2(kLogoSize + 4.0f, kLogoSize + 4.0f));
    ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::TextUnformatted("VRConfig Helper");
    ImGui::TextDisabled("Version %s", VRCONFIG_HELPER_VERSION);
    ImGui::Spacing();
    ImGui::TextUnformatted("Designed by Snagar.Dev");
    ImGui::TextUnformatted("Coded by Claude.Ai");
    ImGui::TextUnformatted("License: Free");
    ImGui::EndGroup();
    ImGui::Spacing();
    ImGui::TextDisabled("Inspired by the \"VR Tools\" plugin, by jrKok.");
    ImGui::Spacing();
    ui::ui_wrapped_text("Edits an aircraft's vrconfig file: VR teleport hotspots and the VR behaviour of its "
                        "knobs, switches and levers.", &ui::theme::kTextDisabled);
    ImGui::Spacing();
    if (ui::ui_button("Close", ImVec2(ImGui::GetContentRegionAvail().x, 40.0f))) {
        ImGui::CloseCurrentPopup();
        LOG_DEBUG("About closed");
    }
    ImGui::EndPopup();
}

} // namespace hpvr

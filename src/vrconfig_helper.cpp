// vrconfig_helper - X-Plane plugin entry points
//
// Registers one command ("vrconfig_helper/toggle_main_window") and a
// Plugins > VRConfig Helper menu item bound to it. The command shows or hides the
// plugin window (one window; pages and popups inside it).
// All drawing goes through ImgWindow / Dear ImGui.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#include "XPLMDefs.h"
#include "XPLMDisplay.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"

#include "imgui.h"
#include "ImgWindow.h"

#include "Fonts.h"
#include "Log.h"
#include "PluginWindow.h"
#include "xp/Aircraft.h"
#include "hotspot/SpotEditor.h"
#include "util/FileUtil.h"

#ifndef VRCONFIG_HELPER_VERSION
#define VRCONFIG_HELPER_VERSION "0.0.0"
#endif

namespace {

namespace fs = std::filesystem;

using hpvr::Page;
using hpvr::PluginWindow;
using hpvr::hotspot::SpotEditor;
using hpvr::manip::ManipEditor;
using hpvr::manip::ManipUpdater;

constexpr const char* kPluginName = "VRConfig Helper";
constexpr const char* kPluginId   = "vrconfig_helper";       // signature, log file name
constexpr const char* kPluginDesc = "VR configuration helper: teleport hotspots and manipulators";

std::unique_ptr<PluginWindow> gWindow;
SpotEditor                    gSpotEditor;      // outlives the window: unsaved changes survive closing it
ManipEditor                   gManipEditor;     // same, for the manipulator page
ManipUpdater                  gManipUpdater;    // same, for the manipulator updater page
XPLMCommandRef                gCmdToggle   = nullptr;
XPLMMenuID                    gMenu        = nullptr;
int                           gMenuItem    = -1;

// The plugin's own folder: .../Resources/plugins/vrconfig_helper/
// (the .xpl lives in a <platform>_x64 sub-folder of it).
fs::path PluginFolder()
{
    char path[1024] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, path, nullptr, nullptr);
    fs::path dir = hpvr::util::PathFromUtf8(path).parent_path();
    const std::string sub = hpvr::util::Utf8(dir.filename());
    if (sub.size() > 4 && sub.compare(sub.size() - 4, 4, "_x64") == 0)
        dir = dir.parent_path();
    return dir;
}

void ToggleWindow()
{
    ImGuiContext* prevContext = ImGui::GetCurrentContext();

    if (!gWindow) {
        gWindow = std::make_unique<PluginWindow>(gSpotEditor, gManipEditor, gManipUpdater, PluginFolder());
        gWindow->SetVisible(true);         // moves itself into VR when VR is on
        gWindow->BringWindowToFront();
        LOG_DEBUG("window created (in VR: {})", gWindow->IsInVR());
    } else {
        const bool show = !gWindow->GetVisible();
        if (show && gWindow->CurrentPage() == Page::Hotspot)
            gWindow->ShowPage(Page::Hotspot);          // re-read: the aircraft may have changed
        gWindow->SetVisible(show);
        if (show && !gWindow->IsPoppedOut())
            gWindow->BringWindowToFront();
        LOG_DEBUG("window {}", show ? "shown" : "hidden");
    }

    if (prevContext)
        ImGui::SetCurrentContext(prevContext);
}

int CommandHandler(XPLMCommandRef cmd, XPLMCommandPhase phase, void*)
{
    if (phase == xplm_CommandBegin && cmd == gCmdToggle)
        ToggleWindow();
    return 0;   // consumed (same value for every phase)
}

// Keep the window usable when X-Plane enters / leaves VR.
void FollowVr(int msg)
{
    if (!gWindow || !gWindow->GetVisible())
        return;
    if (msg == XPLM_MSG_ENTERED_VR) {
        if (!gWindow->IsPoppedOut())
            gWindow->SetWindowPositioningMode(xplm_WindowVR);
    } else if (msg == XPLM_MSG_EXITING_VR && gWindow->IsInVR()) {
        // The 2D geometry is lost while in VR: back to the middle of the screen.
        const WindowGeometry centered = WindowGeometry::Centered(PluginWindow::kWidth, PluginWindow::kHeight);
        gWindow->SetWindowPositioningMode(xplm_WindowPositionFree);
        gWindow->SetWindowGeometry(centered.left, centered.top, centered.right, centered.bottom);
    }
}

} // namespace

// --- X-Plane plugin interface ---------------------------------------------------

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc)
{
    std::snprintf(outName, 256, "%s %s", kPluginName, VRCONFIG_HELPER_VERSION);
    std::strcpy(outSig, kPluginId);
    std::strcpy(outDesc, kPluginDesc);

    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    hpvr::log::Init(kPluginId, PluginFolder());
    LOG_INFO("{} version {} starting, log file: {}", kPluginName, VRCONFIG_HELPER_VERSION,
             hpvr::util::Utf8(hpvr::log::GetLogFilePath()));

    gCmdToggle = XPLMCreateCommand("vrconfig_helper/toggle_main_window",
                                   "VRConfig Helper: show/hide the window");
    XPLMRegisterCommandHandler(gCmdToggle, CommandHandler, 1, nullptr);

    const int slot = XPLMAppendMenuItem(XPLMFindPluginsMenu(), kPluginName, nullptr, 0);
    gMenu     = XPLMCreateMenu(kPluginName, XPLMFindPluginsMenu(), slot, nullptr, nullptr);
    gMenuItem = XPLMAppendMenuItemWithCommand(gMenu, "Toggle Window", gCmdToggle);   // no check mark

    LOG_INFO("started");
    return 1;
}

PLUGIN_API void XPluginStop(void)
{
    if (gCmdToggle) {
        XPLMUnregisterCommandHandler(gCmdToggle, CommandHandler, 1, nullptr);
        gCmdToggle = nullptr;
    }
    if (gMenu) {
        XPLMDestroyMenu(gMenu);
        gMenu     = nullptr;
        gMenuItem = -1;
    }
    LOG_INFO("stopped");
    hpvr::log::Shutdown();
}

PLUGIN_API int XPluginEnable(void)
{
    hpvr::fonts::Initialize();
    LOG_INFO("enabled");
    return 1;
}

PLUGIN_API void XPluginDisable(void)
{
    // No drawing happens while disabled, so the window can be deleted
    // directly (SafeDelete would wait for a flight loop that never comes).
    gWindow.reset();
    hpvr::fonts::Shutdown();
    LOG_INFO("disabled");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int msg, void* param)
{
    switch (msg) {
        case XPLM_MSG_ENTERED_VR:
        case XPLM_MSG_EXITING_VR:
            LOG_DEBUG("{} VR", msg == XPLM_MSG_ENTERED_VR ? "entered" : "exiting");
            FollowVr(msg);
            break;

        case XPLM_MSG_PLANE_LOADED:
            // param is the plane index; 0 = the user's aircraft. A reload of the
            // same aircraft counts too: unsaved hotspot changes are discarded.
            if (reinterpret_cast<intptr_t>(param) == 0) {
                const bool hadChanges = (gSpotEditor.IsOpen() && gSpotEditor.ChangeCount() > 0) ||
                                        (gManipEditor.IsOpen() && gManipEditor.ChangeCount() > 0);
                LOG_INFO("user aircraft loaded{}", hadChanges ? ", discarding unsaved changes" : "");
                gSpotEditor.Discard();
                gManipEditor.Discard();
                // The updater keeps the updates of another aircraft's file; those of the
                // aircraft loaded before or now are discarded like the others.
                bool updaterDiscarded = false;
                if (gManipUpdater.IsOpen()) {
                    const auto loaded = hpvr::xp::GetUserAircraft();
                    const bool sameFile = loaded && loaded->vrconfigFile == gManipUpdater.FilePath();
                    if (gManipUpdater.ForLoadedAircraft() || sameFile) {
                        updaterDiscarded = gManipUpdater.ChangeCount() > 0;
                        gManipUpdater.Discard();
                    }
                }
                if (gWindow)
                    gWindow->OnAircraftLoaded(hadChanges || updaterDiscarded, updaterDiscarded);
            }
            break;

        default:
            break;
    }
}

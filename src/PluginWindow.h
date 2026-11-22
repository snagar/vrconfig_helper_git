// hpvr - the plugin's only window
//
// One fixed-size window; its content changes with the page:
//   Home    - the actions the plugin offers
//   Hotspot - teleport hotspots of the user aircraft (SpotPage)
//   Manip   - extract the cockpit OBJ manipulators to vrconfig (ManipPage)
//   Updater - update the manipulator blocks of a vrconfig file (UpdaterPage)
// Questions and forms open as popups on top of the page.

#pragma once

#include <filesystem>
#include <string>

#include "ImgWindow.h"

#include "hotspot/SpotEditor.h"
#include "hotspot/SpotPage.h"
#include "manip/ManipEditor.h"
#include "manip/ManipPage.h"
#include "manip/ManipUpdater.h"
#include "manip/UpdaterPage.h"

namespace hpvr {

enum class Page {
    Home,
    Hotspot,
    Manip,
    Updater,
};

class PluginWindow : public ImgWindow {
public:
    // `pluginFolder`: the plugin's folder (manip_types.ini, manip_actions.ini).
    PluginWindow(hotspot::SpotEditor& spotEditor, manip::ManipEditor& manipEditor, manip::ManipUpdater& updater,
                 const std::filesystem::path& pluginFolder);
    ~PluginWindow() override = default;

    PluginWindow(const PluginWindow&)            = delete;
    PluginWindow& operator=(const PluginWindow&) = delete;

    void ShowPage(Page page);
    Page CurrentPage() const { return mPage; }

    // The user aircraft was (re)loaded; unsaved changes are already discarded
    // (the updater's only when they belonged to that aircraft: `updaterDiscarded`).
    void OnAircraftLoaded(bool changesDiscarded, bool updaterDiscarded);

protected:
    ImGuiWindowFlags_ beforeBegin() override;
    void buildInterface() override;

private:
    void ui_home_page();
    bool ui_home_action(const char* label, const char* description, const std::string& blocked = {});
    void ui_popup_about();

    hotspot::SpotEditor& mSpotEditor;
    manip::ManipEditor&  mManipEditor;
    manip::ManipUpdater& mUpdater;
    Page                 mPage = Page::Home;
    hotspot::SpotPage    mSpotPage;
    manip::ManipPage     mManipPage;
    manip::UpdaterPage   mUpdaterPage;
    bool                 mAskAbout = false;
};

} // namespace hpvr

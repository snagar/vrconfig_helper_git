// hotspot_vr - main window
//
// The entry screen of the plugin: a short title and one large button per
// action the plugin offers. The window itself does not perform the actions;
// it reports the chosen action through a callback so that each action can
// later open its own screen.

#pragma once

#include <functional>

#include "ImgWindow.h"

namespace hotspot_vr {

enum class MainAction {
    Hotspot,           // "Hotspot"
    KnobsToVrconfig    // "Knobs to vrconfig"
};

class MainWindow : public ImgWindow {
public:
    using ActionHandler = std::function<void(MainAction)>;

    static constexpr int kWidth  = 360;
    static constexpr int kHeight = 170;

    explicit MainWindow(ActionHandler onAction);
    ~MainWindow() override = default;

    MainWindow(const MainWindow&)            = delete;
    MainWindow& operator=(const MainWindow&) = delete;

protected:
    ImGuiWindowFlags_ beforeBegin() override;
    void buildInterface() override;

private:
    bool actionButton(const char* label);

    ActionHandler mOnAction;
};

} // namespace hotspot_vr

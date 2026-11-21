// hotspot_vr - main window

#include "MainWindow.h"

#include <utility>

#include "imgui.h"

#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace hotspot_vr {

namespace {

constexpr float kButtonHeight = 48.0f;   // large targets for VR controllers

} // namespace

MainWindow::MainWindow(ActionHandler onAction)
    : ImgWindow(WindowGeometry::Centered(kWidth, kHeight,
                                         xplm_WindowDecorationRoundRectangle),
                xplm_WindowDecorationRoundRectangle,
                xplm_WindowLayerFloatingWindows),
      mOnAction(std::move(onAction))
{
    SetWindowTitle("Hotspot VR");
    SetWindowResizingLimits(kWidth, kHeight, kWidth * 3, kHeight * 3);

    // The ImgWindow constructor leaves this window's ImGui context current,
    // so the style set here belongs to this window only.
    ui::theme::ApplyBaseStyle();
    ImGui::GetStyle().ItemSpacing = ImVec2(8.0f, 10.0f);
}

ImGuiWindowFlags_ MainWindow::beforeBegin()
{
    return static_cast<ImGuiWindowFlags_>(ImGuiWindowFlags_NoScrollbar |
                                          ImGuiWindowFlags_NoScrollWithMouse);
}

bool MainWindow::actionButton(const char* label)
{
    return ui::Button(label, ImVec2(-1.0f, kButtonHeight));
}

void MainWindow::buildInterface()
{
    ui::CenteredText("Select an action :");
    ImGui::Spacing();

    if (actionButton("Hotspot") && mOnAction)
        mOnAction(MainAction::Hotspot);

    if (actionButton("Knobs to vrconfig") && mOnAction)
        mOnAction(MainAction::KnobsToVrconfig);
}

} // namespace hotspot_vr

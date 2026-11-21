// vrconfig_helper - fonts shared by all windows
//
//   fonts::regular     DejaVu Sans from X-Plane (fallback: ImGui default font),
//                      with the Font Awesome icons merged in: ICON_FA_* work in any label
//   fonts::fontawsome  Font Awesome 5 Solid icons, use with the ICON_FA_* names
//                      from IconsFontAwesome5.h, e.g.
//
//       ImGui::PushFont(hpvr::fonts::fontawsome, 0.0f);   // 0 = keep current size
//       ImGui::Button(ICON_FA_CROSSHAIRS);
//       ImGui::PopFont();

#pragma once

#include "imgui.h"
#include "IconsFontAwesome5.h"

namespace hpvr::fonts {

inline constexpr float kFontSize = 16.0f;

extern ImFont* regular;
extern ImFont* fontawsome;

// Creates the shared ImgWindow font atlas and loads the fonts.
// Call from XPluginEnable, before any window is created.
void Initialize();
// Releases the atlas. Call from XPluginDisable, after all windows are gone.
void Shutdown();

} // namespace hpvr::fonts

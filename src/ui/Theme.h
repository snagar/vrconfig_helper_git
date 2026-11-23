// hpvr - colour roles shared by all pages
//
// Which named colour (hpvr_colors.hpp) is used for what. Change a role here and
// every page follows.
//   orange = unsaved change, green = saved in this X-Plane run, red = error.
// State colours are always shown together with a symbol and text.

#pragma once

#include "imgui.h"
#include "hpvr_colors.hpp"

namespace hpvr::ui::theme {

// Panel and text
inline const ImVec4& kPanelBg       = color::color_vec4_hpvr_panel;
inline const ImVec4& kPanelBgDark   = color::color_vec4_hpvr_panel_dark;
inline const ImVec4& kPopupBg       = color::color_vec4_hpvr_popup;
inline const ImVec4& kInputBg       = color::color_vec4_hpvr_input;
inline const ImVec4& kText          = color::color_vec4_black;
inline const ImVec4& kTextDisabled  = color::color_vec4_hpvr_text_dim;
inline const ImVec4& kTextOnColor   = color::color_vec4_white;
inline const ImVec4& kHelpText      = color::color_vec4_hpvr_text_help;

// Standard buttons
inline const ImVec4& kButton        = color::color_vec4_hpvr_button;
inline const ImVec4& kButtonHover   = color::color_vec4_hpvr_button_hover;
inline const ImVec4& kButtonActive  = color::color_vec4_hpvr_button_active;
inline const ImVec4& kButtonDisabled= color::color_vec4_hpvr_button_off;

// Unsaved change (orange)
inline const ImVec4& kChanged       = color::color_vec4_hpvr_unsaved;
inline const ImVec4& kChangedHover  = color::color_vec4_hpvr_unsaved_hover;
inline const ImVec4& kChangedActive = color::color_vec4_hpvr_unsaved_active;
inline const ImVec4& kChangedText   = color::color_vec4_hpvr_unsaved_text;

// Saved in this X-Plane run (green)
inline const ImVec4& kSaved         = color::color_vec4_hpvr_saved;
inline const ImVec4& kSavedText     = color::color_vec4_hpvr_saved_text;

// Advanced options and Go to hotspot (purple)
inline const ImVec4& kAdvanced      = color::color_vec4_hpvr_advanced;
inline const ImVec4& kGoTo          = color::color_vec4_hpvr_advanced;

// Aircraft list of the Updater page: in the loaded aircraft's folder (blue) or not (red)
inline const ImVec4& kSameFolderText  = color::color_vec4_hpvr_same_folder_text;
inline const ImVec4& kOtherFolderText = color::color_vec4_hpvr_other_folder_text;

// The VR note on the Hotspot page and in its Adv popup (aqua)
inline const ImVec4& kVrNoteBg      = color::color_vec4_hpvr_vr_note_bg;
inline const ImVec4& kVrNoteBorder  = color::color_vec4_hpvr_vr_note_border;
inline const ImVec4& kVrNoteText    = color::color_vec4_hpvr_vr_note_text;

// Errors (red)
inline const ImVec4& kError         = color::color_vec4_hpvr_error;
inline const ImVec4& kErrorText     = color::color_vec4_hpvr_error_text;

// Applies the base style to the current ImGui context (once per window).
void ApplyBaseStyle();

} // namespace hpvr::ui::theme

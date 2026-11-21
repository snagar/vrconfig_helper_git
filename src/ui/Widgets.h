// hpvr - reusable ImGui widgets and popups
//
// Naming: functions that draw UI start with "ui_"; popups with "ui_popup_".
// They work on the current ImGui context, so any ImgWindow can use them.

#pragma once

#include <string>
#include <vector>

#include "imgui.h"
#include "Theme.h"

namespace hpvr::ui {

// --- text -------------------------------------------------------------------------

// "1 hotspot" / "3 hotspots".
std::string Plural(std::size_t count, const char* one, const char* many);

// --- widgets ----------------------------------------------------------------------

// Disabled "(?)" marker; hovering it shows `desc`, wrapped to fit the window.
// Used instead of tooltips on the widgets themselves.
void ui_help_marker(const char* desc, const ImVec4& textColor = theme::kHelpText);

// Tap & hold button for VR. Returns the number of steps to apply this frame:
// a tap gives 1; holding repeats after a short pause, faster the longer it is held.
float ui_hold_button(const char* label, const ImVec2& size = ImVec2(0, 0), bool enabled = true);

// Button with its own background colour (hover / pressed shades derived from it);
// white caption. Disabled = grey, not clickable.
bool ui_color_button(const char* label, const ImVec2& size, const ImVec4& color, bool enabled = true);

// Same, with explicit hover and pressed colours.
bool ui_color_button(const char* label, const ImVec2& size, const ImVec4& color,
                     const ImVec4& hover, const ImVec4& active, bool enabled = true);

// Standard blue button that can be disabled.
bool ui_button(const char* label, const ImVec2& size = ImVec2(0, 0), bool enabled = true);

// Full-width coloured strip with wrapped text (status line).
void ui_status_strip(const std::string& text, const ImVec4& bg, const ImVec4& textColor);

// Section title with an optional "(?)" help marker, then a separator line.
void ui_section_title(const char* title, const char* help = nullptr);

// Text centred in the remaining width.
void ui_centered_text(const char* text);

// Text wrapped at the window edge, optionally coloured.
void ui_wrapped_text(const std::string& text, const ImVec4* color = nullptr);
// One line of text no wider than `maxWidth`: a long text keeps its start and its
// end ("Cessna_172SP_G1...n_v2.acf"), so file names stay recognisable.
void ui_text_ellipsis(const std::string& text, float maxWidth, const ImVec4* color = nullptr);
// The text shortened in the middle to fit `maxWidth` (current font).
std::string EllipsisMiddle(const std::string& text, float maxWidth);

// A numbered step: a round badge (blue = to do now, green check = done, grey =
// not yet), the title, an optional help marker and a separator.
void ui_step_title(int number, const char* title, bool done, bool current, const char* help = nullptr);

// Width that makes `count` items fill the rest of the line.
float ui_fill_width(int count, float reserve = 0.0f);

// --- popups -----------------------------------------------------------------------

// Positions and sizes the next popup inside the current window (call before BeginPopupModal).
void ui_popup_center_next();

enum class PopupAnswer {
    None,   // popup not open, or no button pressed this frame
    Yes,
    No,
};

struct YesNoText {
    std::string              message;               // the question
    std::vector<std::string> details;               // optional list under the question
    std::string              note;                  // optional coloured line (e.g. a warning)
    const char*              yesLabel = "Yes";
    const char*              noLabel  = "No";
    ImVec4                   yesColor = theme::kButton;
    ImVec4                   noColor  = theme::kButton;
    ImVec4                   noteColor = theme::kChangedText;
};

// Reusable yes/no question. Open it with ImGui::OpenPopup(id); call every
// frame. Returns Yes / No once, in the frame the button is pressed (the popup
// closes itself).
PopupAnswer ui_popup_yes_no(const char* id, const YesNoText& text);

// Reusable message with a single Close button.
void ui_popup_message(const char* id, const std::string& message, const char* closeLabel = "Close");

} // namespace hpvr::ui

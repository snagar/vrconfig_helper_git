// hpvr - reusable ImGui widgets and popups

#include "Widgets.h"

#include "IconsFontAwesome5.h"

#include <format>

#include <algorithm>

namespace hpvr::ui {

std::string Plural(std::size_t count, const char* one, const char* many)
{
    return std::format("{} {}", count, count == 1 ? one : many);
}

namespace {

constexpr float kHoldDelay     = 0.35f;  // s before a held button starts repeating
constexpr float kHoldRateStart = 4.0f;   // steps per second when repeating starts
constexpr float kHoldRateMax   = 60.0f;  // steps per second after a long hold
constexpr float kHoldRampTime  = 3.0f;   // s to go from start to max rate

const ImVec2 kVecPopupButtonSize(150.0f, 36.0f);

ImVec4 Shade(const ImVec4& baseColor, float factor)
{
    return ImVec4(std::min(baseColor.x * factor, 1.0f), std::min(baseColor.y * factor, 1.0f),
                  std::min(baseColor.z * factor, 1.0f), baseColor.w);
}

} // namespace

// --- style ------------------------------------------------------------------------

void theme::ApplyBaseStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsLight(&style);
    style.WindowPadding    = ImVec2(14.0f, 12.0f);
    style.ItemSpacing      = ImVec2(8.0f, 8.0f);
    style.FramePadding     = ImVec2(8.0f, 5.0f);
    style.FrameRounding    = 3.0f;
    style.PopupRounding    = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.PopupBorderSize  = 2.0f;

    ImVec4* styleColors = style.Colors;
    styleColors[ImGuiCol_WindowBg]          = kPanelBg;
    styleColors[ImGuiCol_PopupBg]           = kPopupBg;
    styleColors[ImGuiCol_Text]              = kText;
    styleColors[ImGuiCol_TextDisabled]      = kTextDisabled;
    styleColors[ImGuiCol_Button]            = kButton;
    styleColors[ImGuiCol_ButtonHovered]     = kButtonHover;
    styleColors[ImGuiCol_ButtonActive]      = kButtonActive;
    styleColors[ImGuiCol_FrameBg]           = kInputBg;
    styleColors[ImGuiCol_FrameBgHovered]    = color::color_vec4_white;
    styleColors[ImGuiCol_FrameBgActive]     = color::color_vec4_white;
    styleColors[ImGuiCol_Header]            = kButtonHover;
    styleColors[ImGuiCol_HeaderHovered]     = kButtonHover;
    styleColors[ImGuiCol_HeaderActive]      = kButton;
    styleColors[ImGuiCol_CheckMark]         = kButton;
    styleColors[ImGuiCol_Separator]         = kPanelBgDark;
    styleColors[ImGuiCol_TableHeaderBg]     = kPanelBgDark;
    styleColors[ImGuiCol_TableBorderLight]  = kPanelBgDark;
    styleColors[ImGuiCol_TableBorderStrong] = kPanelBgDark;
    // ImgWindow draws the modal dimming on top of the popup, so no dimming;
    // popups get a blue border and title bar instead.
    styleColors[ImGuiCol_ModalWindowDimBg]  = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    styleColors[ImGuiCol_Border]            = kButton;
    styleColors[ImGuiCol_TitleBg]           = kButton;
    styleColors[ImGuiCol_TitleBgActive]     = kButton;
    styleColors[ImGuiCol_TitleBgCollapsed]  = kButton;
}

// --- widgets ----------------------------------------------------------------------

void ui_help_marker(const char* desc, const ImVec4& textColor)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        // Wrap inside the window: ImGui cannot draw outside an ImgWindow.
        const float maxWrap  = ImGui::GetFontSize() * 35.0f;
        const float winWidth = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 4.0f;
        const float wrap     = std::max(ImGui::GetFontSize() * 10.0f, std::min(maxWrap, winWidth));

        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(wrap);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
        ImGui::PopStyleColor();
    }
}

float ui_hold_button(const char* label, const ImVec2& vec_button_size, bool enabled)
{
    ui_color_button(label, vec_button_size, theme::kButton, theme::kButtonHover, theme::kButtonActive, enabled);
    if (!enabled)
        return 0.0f;
    if (ImGui::IsItemActivated())
        return 1.0f;                                   // the tap
    if (!ImGui::IsItemActive())
        return 0.0f;

    const float held = ImGui::GetIO().MouseDownDuration[ImGuiMouseButton_Left];
    if (held < kHoldDelay)
        return 0.0f;

    const float ramp = std::min((held - kHoldDelay) / kHoldRampTime, 1.0f);   // 0..1
    const float rate = kHoldRateStart + (kHoldRateMax - kHoldRateStart) * ramp * ramp;

    // Accumulate fractional steps per button so slow rates still move.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID id      = ImGui::GetItemID();
    const float acc       = storage->GetFloat(id, 0.0f) + rate * ImGui::GetIO().DeltaTime;
    const float steps     = static_cast<float>(static_cast<int>(acc));
    storage->SetFloat(id, acc - steps);
    return steps;
}

bool ui_color_button(const char* label, const ImVec2& vec_button_size, const ImVec4& color,
                     const ImVec4& hover, const ImVec4& active, bool enabled)
{
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button,        enabled ? color  : theme::kButtonDisabled);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, enabled ? hover  : theme::kButtonDisabled);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  enabled ? active : theme::kButtonDisabled);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextOnColor);
    const bool pressed = ImGui::Button(label, vec_button_size);
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return pressed && enabled;
}

bool ui_color_button(const char* label, const ImVec2& vec_button_size, const ImVec4& color, bool enabled)
{
    return ui_color_button(label, vec_button_size, color, Shade(color, 1.12f), Shade(color, 0.85f), enabled);
}

bool ui_button(const char* label, const ImVec2& vec_button_size, bool enabled)
{
    return ui_color_button(label, vec_button_size, theme::kButton, theme::kButtonHover, theme::kButtonActive, enabled);
}

void ui_status_strip(const std::string& text, const ImVec4& bg, const ImVec4& textColor)
{
    const ImVec2 vec_strip_pos    = ImGui::GetCursorScreenPos();
    const float  width  = ImGui::GetContentRegionAvail().x;
    const float  pad    = ImGui::GetStyle().FramePadding.y + 2.0f;
    const float  wrap   = width - 2.0f * ImGui::GetStyle().FramePadding.x;
    const ImVec2 vec_text_size  = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
    const float  height = vec_text_size.y + 2.0f * pad;

    ImGui::GetWindowDrawList()->AddRectFilled(
        vec_strip_pos, ImVec2(vec_strip_pos.x + width, vec_strip_pos.y + height),
        ImGui::GetColorU32(bg), ImGui::GetStyle().FrameRounding);

    ImGui::SetCursorScreenPos(ImVec2(vec_strip_pos.x + ImGui::GetStyle().FramePadding.x, vec_strip_pos.y + pad));
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(vec_strip_pos.x, vec_strip_pos.y + height + ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0, 0));
}

void ui_section_title(const char* title, const char* help)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kButtonActive);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (help) {
        ImGui::SameLine();
        ui_help_marker(help);
    }
    ImGui::Separator();
}

void ui_centered_text(const char* text)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float textW = ImGui::CalcTextSize(text).x;
    if (avail > textW)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - textW) * 0.5f);
    ImGui::TextUnformatted(text);
}

std::string EllipsisMiddle(const std::string& text, float maxWidth)
{
    std::string shown = text;
    if (ImGui::CalcTextSize(text.c_str()).x > maxWidth && text.size() > 4) {
        // Drop characters from the middle until it fits (2/3 of the kept text at the start).
        const std::string dots = "...";
        std::size_t keep = text.size();
        while (keep > 2) {
            --keep;
            const std::size_t head = (keep * 2) / 3;
            const std::size_t tail = keep - head;
            shown = text.substr(0, head) + dots + text.substr(text.size() - tail);
            if (ImGui::CalcTextSize(shown.c_str()).x <= maxWidth)
                break;
        }
    }
    return shown;
}

void ui_text_ellipsis(const std::string& text, float maxWidth, const ImVec4* color)
{
    const std::string shown = EllipsisMiddle(text, maxWidth);
    if (color)
        ImGui::TextColored(*color, "%s", shown.c_str());
    else
        ImGui::TextUnformatted(shown.c_str());
}

void ui_wrapped_text(const std::string& text, const ImVec4* color)
{
    if (color)
        ImGui::PushStyleColor(ImGuiCol_Text, *color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    if (color)
        ImGui::PopStyleColor();
}

// A numbered step: a round badge (blue = to do now, green check = done, grey =
// not yet), the title, a help marker and a separator.
void ui_step_title(int number, const char* title, bool done, bool current, const char* help)
{
    ImGui::Spacing();
    const float  radius = ImGui::GetTextLineHeight() * 0.62f;
    const ImVec2 vec_badge_pos     = ImGui::GetCursorScreenPos();
    const ImVec2 vec_badge_center(vec_badge_pos.x + radius, vec_badge_pos.y + ImGui::GetTextLineHeight() * 0.5f);
    const ImVec4& badge = done ? theme::kSaved : current ? theme::kButton : theme::kTextDisabled;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddCircleFilled(vec_badge_center, radius, ImGui::ColorConvertFloat4ToU32(badge));
    const std::string mark = done ? std::string(ICON_FA_CHECK) : std::to_string(number);
    const ImVec2 vec_mark_size = ImGui::CalcTextSize(mark.c_str());
    draw->AddText(ImVec2(vec_badge_center.x - vec_mark_size.x * 0.5f, vec_badge_center.y - vec_mark_size.y * 0.5f),
                  ImGui::ColorConvertFloat4ToU32(theme::kTextOnColor), mark.c_str());
    ImGui::Dummy(ImVec2(radius * 2.0f, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    ImGui::TextColored(current ? theme::kButtonActive : done ? theme::kText : theme::kTextDisabled, "%s", title);
    if (help) {
        ImGui::SameLine();
        ui_help_marker(help);
    }
    ImGui::Separator();
}

float ui_fill_width(int count, float reserve)
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    return (ImGui::GetContentRegionAvail().x - reserve - spacing * static_cast<float>(count - 1)) /
           static_cast<float>(count);
}

// --- popups -----------------------------------------------------------------------

void ui_popup_center_next()
{
    const ImVec2 vec_window_center(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() * 0.5f,
                        ImGui::GetWindowPos().y + ImGui::GetWindowHeight() * 0.5f);
    ImGui::SetNextWindowPos(vec_window_center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(ImGui::GetWindowWidth() * 0.8f, 0.0f),
        ImVec2(ImGui::GetWindowWidth() - 30.0f, ImGui::GetWindowHeight() - 30.0f));
}

PopupAnswer ui_popup_yes_no(const char* id, const YesNoText& text)
{
    PopupAnswer answer = PopupAnswer::None;
    ui_popup_center_next();
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ui_wrapped_text(text.message);
        for (const std::string& line : text.details)
            ui_wrapped_text("  - " + line);
        if (!text.note.empty())
            ui_wrapped_text(text.note, &text.noteColor);
        ImGui::Spacing();
        if (ui_color_button(text.yesLabel, kVecPopupButtonSize, text.yesColor)) {
            answer = PopupAnswer::Yes;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui_color_button(text.noLabel, kVecPopupButtonSize, text.noColor)) {
            answer = PopupAnswer::No;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return answer;
}

void ui_popup_message(const char* id, const std::string& message, const char* closeLabel)
{
    ui_popup_center_next();
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ui_wrapped_text(message);
        ImGui::Spacing();
        if (ui_button(closeLabel, kVecPopupButtonSize))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace hpvr::ui

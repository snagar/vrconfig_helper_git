// hpvr - Hotspot page: move / turn / rename / add / delete the teleport spots
// of the user aircraft's vrconfig file.
//
// Drawn inside the plugin window. The page only shows and edits; every change
// lives in SpotEditor, so leaving the page or closing the window keeps them.

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"

#include "hotspot/SpotEditor.h"
#include "xp/Aircraft.h"

namespace hpvr::hotspot {

class SpotPage {
public:
    using GoHome = std::function<void()>;

    SpotPage(SpotEditor& editor, GoHome goHome);

    // Reads the user aircraft's vrconfig (keeps unsaved changes of the same file).
    void Reload();
    // One-line note shown in the status strip until the next change.
    void SetNotice(const std::string& text) { mNotice = text; }

    // Draws the page into the current ImGui window.
    void ui_draw();

private:
    // page parts
    void ui_header();
    void ui_not_available();
    void ui_empty_file();
    void ui_spot_picker();
    void ui_spot_actions();
    void ui_rename_row();
    void ui_value_table();
    void ui_move_controls();
    void ui_status_and_save();
    void ui_live_view_toggle();
    bool ui_heading_preset_button(const char* label, const ImVec2& size, bool current, bool enabled);

    // popups (opened by flags set during the frame)
    void ui_popups();
    void ui_popup_explain();
    void ui_popup_notes();
    void ui_popup_add_spot();
    void ui_popup_refresh();
    void ui_popup_cancel_changes();
    void ui_popup_restore_backup();
    void ui_popup_overwrite_file();
    void ui_popup_advanced();
    void ui_advanced_unsaved();
    void ui_advanced_tilt();       // part of the Advanced popup: pitch / roll
    void ui_advanced_copy();       // part of the Advanced popup: copy a value to other hotspots

    // actions
    void UseHeadPosition();
    void ApplyLiveView();          // camera to the selected hotspot (when live view is on)
    void GoToHotspot();
    void OpenAdvancedPopup();            // camera to the selected hotspot (button, always)
    void DoSave(bool overwriteOutsideChanges);
    void OpenAddPopup();
    void AddSpotFromPopup();
    void SelectByName(const std::string& name);
    std::string SpotLabel(std::size_t spotIndex) const;

    SpotEditor&                     mEditor;
    GoHome                          mGoHome;
    std::optional<xp::UserAircraft> mAircraft;
    OpenResult                      mOpenResult = OpenResult::NoFile;
    std::size_t                     mSelected   = 0;

    bool                            mRenaming    = false;
    bool                            mFocusRename = false;
    std::array<char, 256>           mRenameText{};

    // Add popup state
    std::array<char, 256>           mAddName{};
    int                             mAddKind = 0;      // 0 sitting, 1 standing
    int                             mAddFrom = 0;      // 0 head position, 1 copy of selected
    std::string                     mAddError;

    std::string                     mNotice;
    std::string                     mLiveViewPaused;   // why the camera was not moved (empty = following)
    std::string                     mError;            // last save / restore error

    // Advanced popup state
    int                             mCopyFieldIndex = 1;       // index into kAllSpotFields
    double                          mCopyValue      = 0.0;
    std::vector<char>               mCopyTargets;              // 1 = apply to this spot
    std::string                     mCopyResult;

    bool mAskAdvanced = false, mAskNotes = false;
    bool mAskExplain = false, mAskAdd = false, mAskRefresh = false,
         mAskCancel = false, mAskRestore = false, mAskOverwrite = false;
};

} // namespace hpvr::hotspot

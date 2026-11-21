// hotspot_vr - "Hotspot" screen: move / turn / rename the teleport spots of
// the user aircraft's vrconfig file.
//
// The window only shows and edits; every change lives in SpotEditor, so
// closing the window keeps unsaved changes.

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>

#include "ImgWindow.h"

#include "hotspot/SpotEditor.h"
#include "xp/Aircraft.h"

namespace hotspot_vr::hotspot {

class SpotEditWindow : public ImgWindow {
public:
    static constexpr int kWidth  = 520;
    static constexpr int kHeight = 730;

    explicit SpotEditWindow(SpotEditor& editor);
    ~SpotEditWindow() override = default;

    SpotEditWindow(const SpotEditWindow&)            = delete;
    SpotEditWindow& operator=(const SpotEditWindow&) = delete;

    // Reads the user aircraft's vrconfig (keeps unsaved changes of the same file).
    void Reload();
    // One-line note shown in the status strip until the next change.
    void SetNotice(const std::string& text);

protected:
    ImGuiWindowFlags_ beforeBegin() override;
    void buildInterface() override;

private:
    void DrawHeader();
    void DrawNotAvailable();
    void DrawSpotPicker();
    void DrawValueTable();
    void DrawMoveControls();
    void DrawStatusAndActions();
    void DrawPopups();

    void UseHeadPosition();
    void DoSave(bool overwriteOutsideChanges);

    SpotEditor&                  mEditor;
    std::optional<xp::UserAircraft> mAircraft;
    OpenResult                   mOpenResult = OpenResult::NoFile;
    std::size_t                  mSelected   = 0;

    bool                         mRenaming = false;
    bool                         mFocusRename = false;
    std::array<char, 256>        mRenameText{};

    std::string                  mNotice;
    std::string                  mError;          // last save / restore error

    // popups requested this frame (opened inside DrawPopups)
    bool mAskExplain = false, mAskCancel = false, mAskRestore = false, mAskOverwrite = false;
};

} // namespace hotspot_vr::hotspot

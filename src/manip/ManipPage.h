// hpvr - "Extract manipulators to vrconfig" page
//
// Two steps, the second disabled until the first is done:
//   1. Read the aircraft files: the active .acf lists the objects this aircraft
//      uses; they are read in a background thread (progress bar + Abort). The
//      user then confirms which files to use (a list with ticks and counts).
//   2. Pick the manipulators to extract: types (manip_types.ini), Extract to
//      vrconfig, and the result list grouped by type with a tick per row.
// The confirmation is asked again for every aircraft load.
//
// The page only shows and asks; the changes live in ManipEditor, so leaving the
// page or closing the window keeps them until Save or Cancel.

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "manip/ManipEditor.h"
#include "manip/ManipTypes.h"
#include "manip/ObjScanner.h"
#include "xp/Aircraft.h"

namespace hpvr::manip {

class ManipPage {
public:
    using GoHome = std::function<void()>;

    // `pluginFolder`: where manip_types.ini lives.
    ManipPage(ManipEditor& editor, GoHome goHome, const std::filesystem::path& pluginFolder);

    // Reads the user aircraft's vrconfig (keeps unsaved changes of the same file).
    void Reload();
    // An aircraft was loaded (also the same one again): step 1 starts over.
    void OnAircraftLoaded();
    void SetNotice(const std::string& text) { mNotice = text; }

    // Draws the page into the current ImGui window.
    void ui_draw();

private:
    enum class ReadState { NotRead, Reading, Read, Aborted };

    // page parts
    void ui_header();
    void ui_step_files();
    void ui_step_manipulators();
    void ui_type_filters();
    void ui_extract_row();
    void ui_result();
    void ui_status_and_save();

    // popups (opened by flags set during the frame)
    void ui_popups();
    void ui_popup_explain();
    void ui_popup_files();
    void ui_popup_type_list();
    void ui_popup_cancel_changes();
    void ui_popup_restore_backup();
    void ui_popup_overwrite_file();

    // actions
    void ResetFiles();
    void ReadFiles(bool extractWhenDone);
    void PollRead();
    void UseFiles(const std::vector<char>& ticked);
    void OnExtract();
    void ExtractFromSelected();
    void DoSave(bool overwriteOutsideChanges);
    bool TypeChecked(const std::string& type) const;
    std::size_t ControlsFound(const std::string& type) const;
    std::size_t ControlsPicked(const std::string& type) const;  // found minus unticked in its list
    void OpenTypeList(const std::string& type);
    bool FileUsable(std::size_t file) const;          // exists and has manipulators

    ManipEditor&                     mEditor;
    GoHome                           mGoHome;
    std::optional<xp::UserAircraft>  mAircraft;
    OpenResult                       mOpenResult = OpenResult::ReadError;

    // step 1: files
    TypeIniFile                      mTypeIni;
    ManipTypeSet                     mTypes;            // used by the last read
    ObjScanner                       mScanner;
    ReadState                        mReadState = ReadState::NotRead;
    std::optional<AircraftScan>      mAircraftScan;     // last finished read
    std::vector<char>                mFileTicked;       // confirmed choice, per mAircraftScan->files
    std::vector<char>                mPopupTicked;      // being edited in the files popup
    std::set<std::string>            mConfirmedPaths;   // to keep the choice when reading again
    bool                             mFilesConfirmed = false;
    bool                             mExtractWhenDone = false;
    std::optional<ObjScanResult>     mSelected;         // manipulators of the confirmed files

    // step 2: manipulators
    std::map<std::string, bool>      mTypeChecked;      // type -> ticked (default from the ini)
    bool                             mOverride = false;
    std::set<std::string>            mUnpicked;         // manipulator keys unticked in a type's list
    std::string                      mListType;         // type shown in the list popup
    std::vector<std::size_t>         mListItems;        // its manipulators (indexes into mSelected->manips)
    std::vector<char>                mListTicked;       // being edited in the list popup
    std::set<std::string>            mCollapsed;        // result groups folded

    std::string                      mNotice;
    std::string                      mError;            // last save / restore error
    bool                             mExtracted = false;  // a result is shown

    bool mAskExplain = false, mAskFiles = false, mAskTypeList = false, mAskCancel = false,
         mAskRestore = false, mAskOverwrite = false;
};

} // namespace hpvr::manip

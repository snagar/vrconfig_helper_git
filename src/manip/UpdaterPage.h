// hpvr - "VRConfig Manipulator Updater" page
//
// Two steps, the second disabled until the first is done:
//   1. Choose the aircraft: the loaded aircraft's folder first (the loaded one
//      at the top, blue), "List all aircraft" adds every .acf under X-Plane's
//      Aircraft folder (red when outside the loaded aircraft's folder; found in
//      a background thread). Another folder's aircraft gets a pulsing red
//      warning, another variant of the same folder a grey note.
//   2. Read vrconfig: the BEGIN_MANIP blocks of that aircraft's vrconfig file,
//      with a tick per type (all ticked) and a Find field. The aircraft's OBJ
//      files are read in the background (progress + Abort) to find blocks that
//      match no manipulator and to fill in values when converting.
// Each block's name is a button: its popup shows Before / Change / After, can
// change the settings or convert the block with an ACTION, remove it, or - as
// an opt-in - take a block typed by hand (checked when applied).
// Hotspots are not read or shown here. Nothing is written before Save.
//
// The page only shows and asks; the changes live in ManipUpdater, so leaving
// the page or closing the window keeps them until Save or Cancel.

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "manip/AircraftFinder.h"
#include "manip/ManipActions.h"
#include "manip/ManipBlock.h"
#include "manip/ManipTypes.h"
#include "manip/ManipUpdater.h"
#include "manip/ObjScanner.h"
#include "xp/Aircraft.h"

namespace hpvr::manip {

class UpdaterPage {
public:
    using GoHome = std::function<void()>;

    // `pluginFolder`: where manip_types.ini and manip_actions.ini live.
    UpdaterPage(ManipUpdater& updater, GoHome goHome, const std::filesystem::path& pluginFolder);

    // The page is shown: the loaded aircraft and its folder are read again.
    void Reload();
    // An aircraft was loaded; `discarded`: the updater's changes were dropped.
    void OnAircraftLoaded(bool discarded);
    void SetNotice(const std::string& text) { mNotice = text; }

    void ui_draw();

private:
    struct Choice {
        std::filesystem::path acfFile;
        std::string           label;         // file name, or the path under Aircraft/
        bool                  sameFolder = false;
        bool                  loaded     = false;
    };
    enum class ObjState { NotRead, Reading, Read, Aborted };

    // What the list shows about one block (computed from its current lines).
    struct BlockInfo {
        std::size_t index = 0;
        std::string name;
        std::string type;
        std::string action;                  // ACTION name; empty = none
        bool        canEdit     = false;
        bool        changed     = false;
        bool        removed     = false;
        bool        handEdited  = false;
        bool        saved       = false;
        bool        spring      = false;     // HOLD_MANIP
        bool        empty       = false;     // no settings: X-Plane's default behaviour
        bool        prefixed    = false;     // CMND= / DREF= before a reference
        std::size_t duplicateOf = 0;         // line of the first block with the same identity; 0 = none
        bool        notInObj    = false;     // OBJ files read and no such manipulator
    };

    // page parts
    void ui_header();
    void ui_step_aircraft();
    void ui_step_read();
    void ui_type_filters();
    void ui_find_row();
    void ui_block_list();
    void ui_status_and_save();

    // popups
    void ui_popups();
    void ui_popup_explain();
    void ui_popup_notes();
    void ui_popup_edit();
    void ui_edit_structured();
    void ui_edit_action(const std::string& effectiveType);
    void ui_edit_settings(const std::string& effectiveType);
    bool ui_param_rows(const char* id, const std::vector<ParamDef>& params, std::vector<std::string>& values,
                       const std::string& reverseParam);
    void ui_edit_after(const std::vector<std::string>& afterLines);
    void ui_edit_checks(const std::vector<BlockCheck>& checks);
    void ui_edit_buttons(const std::vector<std::string>& afterLines, const std::vector<BlockCheck>& checks);
    void ui_popup_discard_text();
    void ui_popup_cancel_changes();
    void ui_popup_restore_backup();
    void ui_popup_overwrite_file();

    // actions
    void RebuildChoices();
    void PollFinder();
    void PollObjScan();
    void ReadVrconfig();
    void ForgetRead();
    void DoSave(bool overwriteOutsideChanges);
    void OpenEditor(std::size_t block);
    std::vector<std::string> EditAfterLines() const;
    std::vector<std::string> EditPrefill(const ActionDef& action) const;
    std::vector<BlockInfo> Blocks() const;
    const ObjManip* ObjFor(const std::string& type, const std::vector<std::string>& refs,
                           const std::string& tooltip) const;
    CheckContext MakeCheckContext(const std::string& identityBefore) const;
    const Choice* ChosenChoice() const;
    bool ChosenIsLoaded() const;
    bool IsLoaded(const std::filesystem::path& acfFile) const;
    std::string TypeLabelFor(const std::string& type) const;
    std::string TypeHelpFor(const std::string& type) const;

    ManipUpdater&                    mUpdater;
    GoHome                           mGoHome;
    std::optional<xp::UserAircraft>  mLoaded;            // the aircraft X-Plane has loaded

    // step 1: aircraft
    std::vector<Choice>              mChoices;
    std::filesystem::path            mChosenAcf;
    bool                             mListAll = false;
    AircraftFinder                   mFinder;
    std::optional<std::vector<std::filesystem::path>> mAllAcf;   // last full search

    // step 2: vrconfig + OBJ files
    TypeIniFile                      mTypeIni;
    ActionIniFile                    mActionIni;
    ManipTypeSet                     mTypes;
    ActionSet                        mActions;
    bool                             mRead = false;      // vrconfig of mChosenAcf read
    std::filesystem::path            mReadAcf;
    UpdaterOpen                      mOpenResult = UpdaterOpen::NoFile;
    ObjScanner                       mScanner;
    ObjState                         mObjState = ObjState::NotRead;
    std::vector<ObjManip>            mObjManips;
    std::map<std::string, std::size_t> mObjIndex;        // ManipKey -> index into mObjManips
    std::size_t                      mObjFiles = 0;
    std::map<std::string, bool>      mTypeChecked;       // type -> shown
    std::optional<std::string>       mGroupFilter;       // shown group (first reference's path); empty = all
    std::array<char, 128>            mFind{};

    // edit popup
    std::size_t                      mEditIndex = 0;
    std::optional<ManipBlock>        mEditBase;          // current lines when opened
    std::vector<std::string>         mEditBefore;        // saved lines
    std::string                      mEditIdentityBefore;
    BlockSettings                    mEditSettings;
    BlockSettings                    mEditOriginal;      // of mEditBase
    std::string                      mEditNoStructured;  // why the structured editor cannot be used
    bool                             mEditFreeText = false;
    std::vector<char>                mEditText;          // free text buffer
    std::string                      mEditTextStart;     // the text when free editing was switched on

    std::string                      mNotice;
    std::string                      mError;

    bool mAskExplain = false, mAskNotes = false, mAskEdit = false, mAskDiscardText = false,
         mAskCancel = false, mAskRestore = false, mAskOverwrite = false;
};

} // namespace hpvr::manip

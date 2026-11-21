// hpvr - ACTION conversions and block settings (manip_actions.ini)
//
// What the "VRConfig Manipulator Updater" offers inside a BEGIN_MANIP block:
//   * ACTION <name> <params>: makes X-Plane treat the manipulator as another
//     type in VR (e.g. a command knob as a dataref knob).
//   * settings: DEG_PER_ACTION, SWITCH_THRESHOLD, the flags (HOLD_MANIP ...) and
//     the realistic yoke modes (YOKE_MANIP_ROTATE ...).
// Each one has a help text and a help text per parameter, shown by help markers.
// The list lives only in "manip_actions.ini" in the plugin folder (shipped with
// the plugin, replaced by every update - see RuleFile). A user's own list goes
// in "custom_manip_actions.ini", which is used instead when it exists.
//
// File format (comments start with ';'):
//
//   [file]
//   version = 1                            ; the plugin checks it (RuleFile)
//
//   [action axis_knob]                     ; ACTION axis_knob ...
//   label     = Knob on a dataref          ; shown in the ACTION list
//   from      = command_knob axis_knob     ; block types it is offered for (empty = every type)
//   confirmed = command_knob axis_knob     ; types Laminar's own aircraft convert this way (marked
//                                          ; as seen); the others are marked "not confirmed"
//   version   = 12                         ; X-Plane version it needs (empty = any)
//   help      = Turns the manipulator ...  ; several 'help' lines = paragraphs
//   param     = min : number : 0.0 : Lowest value of the dataref
//               ; name : number | dataref | command | text : default value : help
//   prefill   = axis_knob : 2 3 4 6        ; from an OBJ manipulator of that type: which of its
//                                          ; parameters (1 = cursor) fill each param; '-' = none
//   reverse   = step                       ; this param gets a Reverse tick (flips its sign)
//
//   [setting DEG_PER_ACTION]               ; a block line: KEYWORD <params>; no param = a flag
//   label   = Degrees per action           ; checkbox caption
//   for     = knob                         ; types or categories (knob switch lever yoke) it is
//                                          ; offered for; empty = every type
//   group   = Realistic yoke               ; only one setting of a group in a block; the
//                                          ; group is one choice with this caption
//   version / help / param / reverse       ; as above
//
// Every [setting] keyword (and ACTION) is a line that belongs inside a block:
// the vrconfig reader is told on every load. A broken entry is skipped and
// reported. No X-Plane calls here.

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "manip/RuleFile.h"

namespace hpvr::manip {

// The manip_actions.ini version this plugin reads (the [file] version of the shipped file).
inline constexpr int kActionsFileVersion = 1;

enum class ParamKind {
    Number,
    Dataref,
    Command,
    Text,
};

const char* ParamKindName(ParamKind kind);              // "number", "dataref", ...

struct ParamDef {
    std::string name;
    ParamKind   kind = ParamKind::Number;
    std::string defaultValue;                           // may be empty
    std::string help;
};

struct PrefillRule {
    std::string      fromType;                          // OBJ manipulator type
    std::vector<int> objParams;                         // per param: 0-based OBJ parameter, -1 = none
};

struct ActionDef {
    std::string              name;                      // "axis_knob" (also the type it becomes)
    std::string              label;
    std::set<std::string>    from;                      // empty = every type
    std::set<std::string>    confirmed;
    int                      version = 0;               // 0 = any X-Plane version
    std::string              help;
    std::vector<ParamDef>    params;
    std::vector<PrefillRule> prefill;
    std::string              reverseParam;              // gets a Reverse tick; empty = none

    bool OfferedFor(const std::string& type) const { return from.empty() || from.contains(type); }
    bool ConfirmedFor(const std::string& type) const { return confirmed.contains(type); }
    const PrefillRule* PrefillFrom(const std::string& type) const;
};

struct SettingDef {
    std::string           keyword;                      // "DEG_PER_ACTION"
    std::string           label;
    std::set<std::string> forTypes;                     // types or categories; empty = every type
    std::string           group;                        // mutually exclusive settings; also the caption
    int                   version = 0;
    std::string           help;
    std::vector<ParamDef> params;                       // empty = a flag
    std::string           reverseParam;

    bool IsFlag() const { return params.empty(); }
};

struct ActionSet {
    std::vector<ActionDef>   actions;                   // ini order
    std::vector<SettingDef>  settings;                  // ini order
    int                      version = 0;               // [file] version; 0 = none
    std::vector<std::string> problems;

    const ActionDef*  FindAction(std::string_view name) const;
    const SettingDef* FindSetting(std::string_view keyword) const;
};

// Never fails: broken entries are skipped (listed in `problems`).
ActionSet ParseActionsIni(std::string_view text);

// manip_actions.ini (or custom_manip_actions.ini) in a folder; parsed again
// only when it changed. Each parse also tells the vrconfig reader the setting
// keywords.
class ActionIniFile {
public:
    explicit ActionIniFile(std::filesystem::path folder);

    const RuleFile& File() const { return mFile; }
    const ActionSet& Load();
    const ActionSet& Actions() const { return mActions; }
    // Missing file / version mismatch (empty = nothing to say).
    std::string Notice() const { return mFile.Notice(mActions.version); }

private:
    RuleFile  mFile;
    ActionSet mActions;
};

} // namespace hpvr::manip

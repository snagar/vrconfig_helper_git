// hpvr - the manipulator types (manip_types.ini)
//
// The list lives only in "manip_types.ini" in the plugin folder (shipped with
// the plugin, replaced by every update - see RuleFile): a new OBJ manipulator
// type needs a new entry, not a new plugin build. A user's own list goes in
// "custom_manip_types.ini", which is used instead when it exists.
//
// File format (comments start with ';' - '#' belongs to vrconfig template lines):
//
//   [file]
//   version = 1                         ; the plugin checks it (RuleFile)
//
//   [defaults]
//   deg_per_action = 5                  ; {deg} when no rule matches
//   deg_rule = selector : 30            ; words of the tooltip / command names
//   deg_rule = alt altitude* : 10       ; '*' = word starts with; first match wins
//
//   [command_knob]                      ; OBJ type without "ATTR_manip_"
//   category = knob                     ; knob | switch | lever | yoke
//   label    = cmd                      ; checkbox caption (default: the type);
//                                       ; {up_down} / {left_right} are drawn as arrows
//   params   = 3                        ; parameters before the tooltip, cursor = 1
//   refs     = 2 3                      ; which parameters are the commands / datarefs
//   refs_are = commands                 ; commands | datarefs (default datarefs)
//   checked  = yes                      ; ticked when the page opens
//   extract  = yes                      ; no: known (Updater page, help) but not offered by Extract
//   line     = DEG_PER_ACTION {deg}     ; block content, one 'line' per line
//   help     = Knob that runs ...       ; help marker text; several 'help' lines = paragraphs
//
//   [ignore]
//   types = noop none ...               ; known, never read
//
// The number of refs of a type is also how many commands / datarefs its
// BEGIN_MANIP line has (the vrconfig reader is told on every load).
// Template placeholders: {deg} (degrees per step from the rules), {name} (the
// manipulator's name), {file} (its OBJ file). The plugin writes BEGIN_MANIP and
// END_MANIP itself and indents each template line with a tab.
//
// A broken entry is skipped and reported. No X-Plane calls here.

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

// The manip_types.ini version this plugin reads (the [file] version of the shipped file).
inline constexpr int kTypesFileVersion = 1;

enum class ManipCategory {
    Knob,
    Switch,
    Lever,
    Yoke,
};

inline constexpr ManipCategory kAllCategories[] = {
    ManipCategory::Knob, ManipCategory::Switch, ManipCategory::Lever, ManipCategory::Yoke,
};

const char* CategoryLabel(ManipCategory category);       // "Knobs", "Switches", ...
const char* CategoryShortLabel(ManipCategory category);  // "knob", "switch", ... (ini value)
std::optional<ManipCategory> ParseCategory(std::string_view text);

struct ManipTypeDef {
    std::string              type;          // without "ATTR_manip_", e.g. "command_knob"
    ManipCategory            category = ManipCategory::Knob;
    std::string              label;         // checkbox caption
    int                      params = 0;    // parameters before the tooltip (cursor included)
    std::vector<int>         refs;          // 0-based indexes of the commands / datarefs among them
    bool                     commandRefs = false;   // the refs are commands (else datarefs)
    bool                     checked = false;
    bool                     extract = true;   // offered by the Extract page
    std::vector<std::string> lines;         // block content template
    std::string              help;          // help marker text (paragraphs joined by new lines)
};

struct DegreeRule {
    std::vector<std::string> words;         // lower case; "alt*" = starts with "alt"
    int                      degrees = 5;
};

struct ManipTypeSet {
    std::vector<ManipTypeDef> types;        // ini order
    std::set<std::string>     ignored;      // known types that are never read
    int                       defaultDegrees = 5;
    std::vector<DegreeRule>   degreeRules;
    int                       version = 0;  // [file] version; 0 = none
    std::vector<std::string>  problems;     // broken entries (skipped)

    const ManipTypeDef* Find(std::string_view type) const;
    // The set for the Extract page: types with extract = no become ignored ones.
    ManipTypeSet ForExtract() const;
    // {deg} for a manipulator: the first rule whose word is in the tooltip or
    // the command / dataref names, else defaultDegrees.
    int DegreesFor(std::string_view tooltip, const std::vector<std::string>& refs) const;
};

// Parses ini text. Never fails: broken entries are skipped and listed in `problems`.
ManipTypeSet ParseTypeIni(std::string_view text);

// manip_types.ini (or custom_manip_types.ini) in a folder, read again for
// every scan; parsed only when it changed. Each parse also tells the vrconfig
// reader how many commands / datarefs each type has.
class TypeIniFile {
public:
    explicit TypeIniFile(std::filesystem::path folder);

    const RuleFile& File() const { return mFile; }
    const ManipTypeSet& Load();
    const ManipTypeSet& Types() const { return mTypes; }
    bool ParsedLastTime() const { return mParsedLastTime; }   // test support
    // Missing file / version mismatch (empty = nothing to say).
    std::string Notice() const { return mFile.Notice(mTypes.version); }

private:
    RuleFile     mFile;
    ManipTypeSet mTypes;
    bool         mParsedLastTime = false;
};

} // namespace hpvr::manip

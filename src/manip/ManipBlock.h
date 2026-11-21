// hpvr - one BEGIN_MANIP ... END_MANIP block: read it, change its settings,
// check a block typed by the user
//
// Edits keep the block as close to the original as possible: a line whose
// values did not change is written back exactly as it was, comments and lines
// the plugin does not know stay where they are, a removed setting drops its
// line, a new ACTION goes first and other new settings go last. New lines use
// the block's own indent (a tab when it has none).
//
// No X-Plane calls here.

#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "manip/ManipActions.h"
#include "manip/ManipTypes.h"

namespace hpvr::manip {

struct BlockLine {
    std::string              raw;               // as written
    std::string              keyword;           // empty for blank and comment lines
    std::vector<std::string> values;            // the words after the keyword (an end-of-line comment removed)
};

struct ManipBlock {
    std::string              beginRaw;          // the BEGIN_MANIP line as written
    std::string              type;
    std::vector<std::string> refs;              // without a CMND= / DREF= prefix
    std::vector<std::string> rawRefs;           // as written
    std::string              tooltip;           // after the refs (may be empty)
    std::vector<BlockLine>   body;
    std::string              endRaw = "END_MANIP";
    std::string              indent = "\t";     // of the first directive line

    std::vector<std::string> Lines() const;
    std::string              Text() const;      // lines joined by '\n'
    const BlockLine*         Find(std::string_view keyword) const;   // first line with that keyword
    bool                     Has(std::string_view keyword) const { return Find(keyword) != nullptr; }
    // "type ref1 ref2" plus " | tooltip": what tells blocks apart in X-Plane.
    std::string              Identity() const;
    // The type X-Plane uses in VR: the ACTION's name, else the block type.
    std::string              EffectiveType() const;
    bool                     IsEmpty() const;   // no directive: X-Plane's default behaviour
};

// The block's lines; empty + `error` when they are not exactly one block
// (BEGIN_MANIP first, END_MANIP last, nothing else around or nested).
std::optional<ManipBlock> ParseBlock(const std::vector<std::string>& lines, std::string& error);
std::vector<std::string>  SplitLines(std::string_view text);     // '\n' / "\r\n"; no empty last line

// The ACTION and the settings of a block, as the structured editor shows them.
struct BlockSettings {
    bool                                            hasAction = false;
    std::string                                     actionName;
    std::vector<std::string>                        actionParams;
    std::map<std::string, std::vector<std::string>> settings;   // keyword -> values (a flag: none)

    bool Has(const std::string& keyword) const { return settings.contains(keyword); }
    bool operator==(const BlockSettings&) const = default;
};

// Empty + `reason` when the structured editor cannot show the block as it is
// (a directive appears twice).
std::optional<BlockSettings> ReadSettings(const ManipBlock& block, std::string& reason);
// `block` with `settings` applied (see the rules at the top). `order` gives the
// order of new setting lines.
ManipBlock ApplySettings(const ManipBlock& block, const BlockSettings& settings, const ActionSet& order);

// The type's definition in the rules (nullptr = unknown type).
const ManipTypeDef* FindType(const ManipTypeSet& types, std::string_view type);
// Is `setting` meant for blocks whose effective type is `type`?
bool SettingOffered(const SettingDef& setting, const std::string& type, const ManipTypeSet& types);
bool IsNumber(std::string_view text);

// --- checks --------------------------------------------------------------------------------

struct BlockCheck {
    bool        blocking = false;               // the block cannot be applied
    std::string text;
};

struct CheckContext {
    const ManipTypeSet*                     types   = nullptr;
    const ActionSet*                        actions = nullptr;
    std::string                             identityBefore;    // of the saved block; empty = new
    // True when the OBJ files have a manipulator for the block; empty = not read.
    std::function<bool(const ManipBlock&)>  inObj;
};

// Checks the text of one block. Blocking: not exactly one block. Warnings:
// unknown type / directive / ACTION, wrong number of values, text where a
// number belongs, commas, a directive twice, settings of one group together,
// settings not meant for the type, another manipulator than before, not in the
// OBJ files. `parsed` receives the block when it can be read.
std::vector<BlockCheck> CheckBlock(const std::vector<std::string>& lines, const CheckContext& context,
                                   std::optional<ManipBlock>* parsed = nullptr);

} // namespace hpvr::manip

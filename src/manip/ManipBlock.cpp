// hpvr - one BEGIN_MANIP ... END_MANIP block

#include "ManipBlock.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <set>
#include <system_error>

#include "vrconfig/VrConfigFile.h"

namespace hpvr::manip {

namespace {

constexpr std::string_view kBegin  = "BEGIN_MANIP";
constexpr std::string_view kEnd    = "END_MANIP";
constexpr std::string_view kAction = "ACTION";

bool IsSpace(char character) { return character == ' ' || character == '\t'; }

std::string_view TrimLeft(std::string_view text)
{
    while (!text.empty() && IsSpace(text.front()))
        text.remove_prefix(1);
    return text;
}

std::string_view Trim(std::string_view text)
{
    text = TrimLeft(text);
    while (!text.empty() && (IsSpace(text.back()) || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

std::string_view NextToken(std::string_view& rest)
{
    rest = TrimLeft(rest);
    std::size_t i = 0;
    while (i < rest.size() && !IsSpace(rest[i]))
        ++i;
    const std::string_view token = rest.substr(0, i);
    rest = rest.substr(i);
    return token;
}

std::string LeadingSpace(std::string_view line)
{
    std::size_t i = 0;
    while (i < line.size() && IsSpace(line[i]))
        ++i;
    return std::string(line.substr(0, i));
}

// Keyword and values of a line; keyword empty for blank / comment lines.
BlockLine ReadLine(const std::string& raw)
{
    BlockLine line;
    line.raw = raw;
    std::string_view rest = Trim(raw);
    if (rest.empty() || rest.front() == '#')
        return line;
    line.keyword = std::string(NextToken(rest));
    for (std::string_view token = NextToken(rest); !token.empty(); token = NextToken(rest)) {
        if (token.front() == '#')
            break;
        line.values.emplace_back(token);
    }
    return line;
}

std::string Join(const std::vector<std::string>& words)
{
    std::string text;
    for (const std::string& word : words)
        text += (text.empty() ? "" : " ") + word;
    return text;
}

std::string LineText(const std::string& indent, const std::string& keyword, const std::vector<std::string>& values)
{
    std::string text = indent + keyword;
    if (!values.empty())
        text += " " + Join(values);
    return text;
}

std::string ParamNames(const std::vector<ParamDef>& params)
{
    std::vector<std::string> names;
    for (const ParamDef& param : params)
        names.push_back(param.name);
    return Join(names);
}

} // namespace

// --- ManipBlock ---------------------------------------------------------------------------------

std::vector<std::string> ManipBlock::Lines() const
{
    std::vector<std::string> lines;
    lines.push_back(beginRaw);
    for (const BlockLine& line : body)
        lines.push_back(line.raw);
    lines.push_back(endRaw);
    return lines;
}

std::string ManipBlock::Text() const
{
    std::string text;
    for (const std::string& line : Lines())
        text += line + "\n";
    if (!text.empty())
        text.pop_back();
    return text;
}

const BlockLine* ManipBlock::Find(std::string_view keyword) const
{
    for (const BlockLine& line : body) {
        if (line.keyword == keyword)
            return &line;
    }
    return nullptr;
}

std::string ManipBlock::Identity() const
{
    std::string identity = type;
    for (const std::string& ref : refs)
        identity += " " + ref;
    if (!tooltip.empty())
        identity += " | " + tooltip;
    return identity;
}

std::string ManipBlock::EffectiveType() const
{
    const BlockLine* action = Find(kAction);
    return action && !action->values.empty() ? action->values.front() : type;
}

bool ManipBlock::IsEmpty() const
{
    return std::none_of(body.begin(), body.end(), [](const BlockLine& line) { return !line.keyword.empty(); });
}

std::vector<std::string> SplitLines(std::string_view text)
{
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos)
            end = text.size();
        std::string line(text.substr(at, end - at));
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        at = end + 1;
    }
    return lines;
}

std::optional<ManipBlock> ParseBlock(const std::vector<std::string>& lines, std::string& error)
{
    error.clear();
    std::size_t first = 0;
    while (first < lines.size() && Trim(lines[first]).empty())
        ++first;
    std::size_t last = lines.size();
    while (last > first && Trim(lines[last - 1]).empty())
        --last;
    if (first == last) {
        error = "The block is empty: it needs BEGIN_MANIP <type> <commands / datarefs> ... END_MANIP.";
        return std::nullopt;
    }

    ManipBlock block;
    {
        std::string_view rest = lines[first];
        if (NextToken(rest) != kBegin) {
            error = std::format("Line {}: the block must start with BEGIN_MANIP (comments go inside the block).",
                                first + 1);
            return std::nullopt;
        }
        block.beginRaw = lines[first];
        block.type = std::string(NextToken(rest));
        if (block.type.empty() || block.type.front() == '#') {
            error = "BEGIN_MANIP needs the manipulator type, e.g. BEGIN_MANIP command_knob <command> <command>.";
            return std::nullopt;
        }
        const std::size_t wanted = vrconfig::ManipRefCount(block.type);
        while (wanted == 0 || block.refs.size() < wanted) {
            const std::string_view token = NextToken(rest);
            if (token.empty())
                break;
            if (token.front() == '#') {
                rest = {};
                break;
            }
            block.rawRefs.emplace_back(token);
            block.refs.push_back(vrconfig::ManipRefName(token));
        }
        block.tooltip = std::string(Trim(rest));
        if (const std::size_t hash = block.tooltip.find(" #"); hash != std::string::npos)
            block.tooltip = std::string(Trim(std::string_view(block.tooltip).substr(0, hash)));
        if (!block.tooltip.empty() && block.tooltip.front() == '#')
            block.tooltip.clear();
    }
    {
        std::string_view rest = lines[last - 1];
        if (last - 1 == first || NextToken(rest) != kEnd) {
            error = "The block must end with END_MANIP (and nothing may follow it).";
            return std::nullopt;
        }
        block.endRaw = lines[last - 1];
    }
    bool indentSet = false;
    for (std::size_t i = first + 1; i + 1 < last; ++i) {
        BlockLine line = ReadLine(lines[i]);
        if (line.keyword == kBegin || line.keyword == kEnd || line.keyword == "BEGIN_TELEPORT_HOTSPOT" ||
            line.keyword == "END_TELEPORT_HOTSPOT") {
            error = std::format("Line {}: {} inside the block - one block only, from BEGIN_MANIP to END_MANIP.",
                                i + 1, line.keyword);
            return std::nullopt;
        }
        if (!indentSet && !line.keyword.empty()) {
            block.indent = LeadingSpace(line.raw);
            indentSet = true;
        }
        block.body.push_back(std::move(line));
    }
    return block;
}

// --- settings -------------------------------------------------------------------------------------

std::optional<BlockSettings> ReadSettings(const ManipBlock& block, std::string& reason)
{
    reason.clear();
    BlockSettings settings;
    std::set<std::string> seen;
    for (const BlockLine& line : block.body) {
        if (line.keyword.empty() || !vrconfig::IsManipDirective(line.keyword))
            continue;
        if (!seen.insert(line.keyword).second) {
            reason = std::format("{} appears twice in this block", line.keyword);
            return std::nullopt;
        }
        if (line.keyword == kAction) {
            settings.hasAction  = true;
            settings.actionName = line.values.empty() ? std::string() : line.values.front();
            if (!line.values.empty())
                settings.actionParams.assign(line.values.begin() + 1, line.values.end());
        } else {
            settings.settings[line.keyword] = line.values;
        }
    }
    return settings;
}

ManipBlock ApplySettings(const ManipBlock& block, const BlockSettings& settings, const ActionSet& order)
{
    ManipBlock result = block;
    result.body.clear();
    std::set<std::string> written;
    std::vector<std::string> actionValues;
    if (settings.hasAction) {
        actionValues.push_back(settings.actionName);
        actionValues.insert(actionValues.end(), settings.actionParams.begin(), settings.actionParams.end());
    }

    auto lmbda_keepOrRewrite = [&](const BlockLine& line, const std::vector<std::string>& values) {
        if (line.values == values) {
            result.body.push_back(line);                       // unchanged: exactly as written
        } else {
            BlockLine changed;
            changed.keyword = line.keyword;
            changed.values  = values;
            changed.raw     = LineText(LeadingSpace(line.raw), line.keyword, values);
            result.body.push_back(std::move(changed));
        }
        written.insert(line.keyword);
    };

    for (const BlockLine& line : block.body) {
        if (line.keyword.empty() || !vrconfig::IsManipDirective(line.keyword) || written.contains(line.keyword)) {
            if (!line.keyword.empty() && vrconfig::IsManipDirective(line.keyword))
                continue;                                      // a second copy of something already written
            result.body.push_back(line);                       // comment, blank or unknown line: kept
            continue;
        }
        if (line.keyword == kAction) {
            if (settings.hasAction)
                lmbda_keepOrRewrite(line, actionValues);
            continue;                                          // no ACTION any more: line dropped
        }
        if (const auto found = settings.settings.find(line.keyword); found != settings.settings.end())
            lmbda_keepOrRewrite(line, found->second);
    }

    auto lmbda_newLine = [&](const std::string& keyword, const std::vector<std::string>& values) {
        BlockLine line;
        line.keyword = keyword;
        line.values  = values;
        line.raw     = LineText(result.indent, keyword, values);
        return line;
    };
    if (settings.hasAction && !written.contains(std::string(kAction))) {
        std::size_t at = 0;                                    // first, after leading comments
        while (at < result.body.size() && result.body[at].keyword.empty() && !Trim(result.body[at].raw).empty())
            ++at;
        result.body.insert(result.body.begin() + static_cast<std::ptrdiff_t>(at),
                           lmbda_newLine(std::string(kAction), actionValues));
    }
    // New settings at the end, in the order of the action list, then any others.
    std::vector<std::string> keywords;
    for (const SettingDef& def : order.settings)
        keywords.push_back(def.keyword);
    for (const auto& [keyword, values] : settings.settings) {
        if (std::find(keywords.begin(), keywords.end(), keyword) == keywords.end())
            keywords.push_back(keyword);
    }
    // Trailing blank lines stay at the end.
    std::size_t insertAt = result.body.size();
    while (insertAt > 0 && Trim(result.body[insertAt - 1].raw).empty())
        --insertAt;
    for (const std::string& keyword : keywords) {
        const auto found = settings.settings.find(keyword);
        if (found == settings.settings.end() || written.contains(keyword))
            continue;
        result.body.insert(result.body.begin() + static_cast<std::ptrdiff_t>(insertAt++),
                           lmbda_newLine(keyword, found->second));
    }
    return result;
}

const ManipTypeDef* FindType(const ManipTypeSet& types, std::string_view type)
{
    return types.Find(type);
}

bool SettingOffered(const SettingDef& setting, const std::string& type, const ManipTypeSet& types)
{
    if (setting.forTypes.empty() || setting.forTypes.contains(type))
        return true;
    if (const ManipTypeDef* def = FindType(types, type))
        return setting.forTypes.contains(CategoryShortLabel(def->category));
    return false;
}

bool IsNumber(std::string_view text)
{
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);
    if (text.empty())
        return false;
    double value = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

// --- checks ----------------------------------------------------------------------------------------

std::vector<BlockCheck> CheckBlock(const std::vector<std::string>& lines, const CheckContext& context,
                                   std::optional<ManipBlock>* parsed)
{
    std::vector<BlockCheck> checks;
    std::string error;
    std::optional<ManipBlock> block = ParseBlock(lines, error);
    if (parsed)
        *parsed = block;
    if (!block) {
        checks.push_back({true, error});
        return checks;
    }
    auto lmbda_warn = [&checks](std::string text) { checks.push_back({false, std::move(text)}); };

    // The manipulator it targets.
    const ManipTypeDef* typeDef = context.types ? FindType(*context.types, block->type) : nullptr;
    const bool ignoredType = context.types && context.types->ignored.contains(block->type);
    if (context.types && !typeDef && !ignoredType)
        lmbda_warn(std::format("'{}' is not a manipulator type VRConfig Helper knows - X-Plane looks for an "
                               "ATTR_manip_{} in the OBJ files.", block->type, block->type));
    const std::size_t wanted = vrconfig::ManipRefCount(block->type);
    if (wanted > 0 && block->refs.size() < wanted)
        lmbda_warn(std::format("BEGIN_MANIP {} needs {} {} after the type.", block->type, wanted,
                               typeDef && typeDef->commandRefs ? (wanted == 1 ? "command" : "commands")
                                                               : (wanted == 1 ? "dataref" : "datarefs")));
    if (!context.identityBefore.empty() && block->Identity() != context.identityBefore)
        lmbda_warn(std::format("This block now targets another manipulator (was: {}).", context.identityBefore));
    if (context.inObj && !context.inObj(*block))
        lmbda_warn("No manipulator with this type and these commands / datarefs in the aircraft's OBJ files - "
                   "X-Plane may show an error for this block when the aircraft loads.");

    // The lines inside.
    std::map<std::string, int> count;
    std::vector<std::string> groupMembers;
    const std::string effective = block->EffectiveType();
    for (const BlockLine& line : block->body) {
        if (line.keyword.empty())
            continue;
        ++count[line.keyword];
        if (line.raw.find(',') != std::string::npos && line.raw.find('#') == std::string::npos)
            lmbda_warn(std::format("{}: commas between the values - X-Plane expects spaces.", line.keyword));
        if (!vrconfig::IsManipDirective(line.keyword)) {
            lmbda_warn(std::format("'{}' is not a manipulator setting VRConfig Helper knows - kept as written.",
                                   line.keyword));
            continue;
        }
        if (!context.actions)
            continue;
        std::vector<std::string> values = line.values;
        const std::vector<ParamDef>* params = nullptr;
        std::string what = line.keyword;
        if (line.keyword == kAction) {
            if (values.empty()) {
                lmbda_warn("ACTION needs a name, e.g. ACTION axis_knob 0 1 0.1 <dataref>.");
                continue;
            }
            const std::string name = values.front();
            values.erase(values.begin());
            const ActionDef* action = context.actions->FindAction(name);
            if (!action) {
                lmbda_warn(std::format("ACTION {} is not in manip_actions.ini - kept as written.", name));
                continue;
            }
            if (!action->OfferedFor(block->type))
                lmbda_warn(std::format("ACTION {} is not usually used with {}.", name, block->type));
            params = &action->params;
            what = "ACTION " + name;
        } else if (const SettingDef* setting = context.actions->FindSetting(line.keyword)) {
            params = &setting->params;
            if (context.types && !SettingOffered(*setting, effective, *context.types))
                lmbda_warn(std::format("{} is not usually used with {}.", line.keyword, effective));
            if (!setting->group.empty())
                groupMembers.push_back(line.keyword);
        }
        if (!params)
            continue;
        if (values.size() != params->size()) {
            lmbda_warn(params->empty()
                ? std::format("{} takes no values.", what)
                : std::format("{} needs {} values: {}.", what, params->size(), ParamNames(*params)));
            continue;
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            if ((*params)[i].kind == ParamKind::Number && !IsNumber(values[i]))
                lmbda_warn(std::format("{}: '{}' should be a number ({}).", what, values[i], (*params)[i].name));
        }
    }
    for (const auto& [keyword, times] : count) {
        if (times > 1 && vrconfig::IsManipDirective(keyword))
            lmbda_warn(std::format("{} appears {} times - X-Plane uses only one.", keyword, times));
    }
    if (groupMembers.size() > 1)
        lmbda_warn(std::format("{} and {} together: only one realistic yoke mode is used.", groupMembers[0],
                               groupMembers[1]));
    return checks;
}

} // namespace hpvr::manip

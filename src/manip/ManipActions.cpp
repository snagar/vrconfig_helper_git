// hpvr - ACTION conversions and block settings (manip_actions.ini)

#include "ManipActions.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <functional>
#include <map>
#include <system_error>

#include "Log.h"
#include "vrconfig/VrConfigFile.h"

namespace hpvr::manip {

namespace fs = std::filesystem;

namespace {

constexpr const char* kIniName = "manip_actions.ini";

using K = ParamKind;

std::string_view Trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

std::string Lower(std::string_view text)
{
    std::string lower(text);
    for (char& character : lower)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return lower;
}

std::vector<std::string> SplitWords(std::string_view text)
{
    std::vector<std::string> words;
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t'))
            ++at;
        std::size_t end = at;
        while (end < text.size() && text[end] != ' ' && text[end] != '\t')
            ++end;
        if (end > at)
            words.emplace_back(text.substr(at, end - at));
        at = end;
    }
    return words;
}

std::optional<ParamKind> ParseKind(std::string_view text)
{
    const std::string lower = Lower(Trim(text));
    if (lower == "number")  return K::Number;
    if (lower == "dataref") return K::Dataref;
    if (lower == "command") return K::Command;
    if (lower == "text")    return K::Text;
    return std::nullopt;
}

bool ParseInt(std::string_view text, int& value)
{
    text = Trim(text);
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc() && ptr == text.data() + text.size();
}

} // namespace

const char* ParamKindName(ParamKind kind)
{
    switch (kind) {
        case K::Number:  return "number";
        case K::Dataref: return "dataref";
        case K::Command: return "command";
        case K::Text:    return "text";
    }
    return "";
}

const PrefillRule* ActionDef::PrefillFrom(const std::string& type) const
{
    for (const PrefillRule& rule : prefill) {
        if (rule.fromType == type)
            return &rule;
    }
    return nullptr;
}

const ActionDef* ActionSet::FindAction(std::string_view name) const
{
    for (const ActionDef& def : actions) {
        if (def.name == name)
            return &def;
    }
    return nullptr;
}

const SettingDef* ActionSet::FindSetting(std::string_view keyword) const
{
    for (const SettingDef& def : settings) {
        if (def.keyword == keyword)
            return &def;
    }
    return nullptr;
}

ActionSet ParseActionsIni(std::string_view text)
{
    ActionSet set;

    struct Entry {
        std::string                                      kind;     // "action" / "setting"
        std::string                                      name;
        std::size_t                                      lineNumber = 0;
        std::vector<std::pair<std::string, std::string>> keys;
        std::vector<std::size_t>                         keyLines;
    };
    std::vector<Entry> entries;
    Entry* current = nullptr;

    std::size_t lineNumber = 0;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t end = std::min(text.find('\n', at), text.size());
        std::string_view line = Trim(text.substr(at, end - at));
        at = end + 1;
        ++lineNumber;
        if (lineNumber == 1 && line.starts_with("\xEF\xBB\xBF"))
            line.remove_prefix(3);
        if (line.empty() || line.front() == ';')
            continue;
        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            // Only a comment may follow the ']' ("[defaults]: ..." is a broken comment line).
            if (close == std::string_view::npos ||
                (!Trim(line.substr(close + 1)).empty() && Trim(line.substr(close + 1)).front() != ';'))
                set.problems.push_back(std::format("line {}: a section line must be \"[name]\" - is this a comment "
                                                   "without its ';'?", lineNumber));
            const std::vector<std::string> words =
                SplitWords(line.substr(1, close == std::string_view::npos ? line.size() - 1 : close - 1));
            Entry entry;
            entry.lineNumber = lineNumber;
            if (words.size() == 2) {
                entry.kind = Lower(words[0]);
                entry.name = entry.kind == "setting" ? words[1] : Lower(words[1]);
            }
            if (words.size() == 1 && Lower(words[0]) == "file")
                entry.kind = "file";
            else if (entry.kind != "action" && entry.kind != "setting")
                set.problems.push_back(std::format("line {}: section must be [file], [action <name>] or [setting <KEYWORD>]",
                                                   lineNumber));
            entries.push_back(std::move(entry));
            current = &entries.back();
            continue;
        }
        const std::size_t equals = line.find('=');
        if (!current || equals == std::string_view::npos) {
            set.problems.push_back(std::format("line {}: not understood: {}", lineNumber, line));
            continue;
        }
        std::string_view value = line.substr(equals + 1);
        if (const std::size_t comment = value.find(" ;"); comment != std::string_view::npos)
            value = value.substr(0, comment);
        current->keys.emplace_back(Lower(Trim(line.substr(0, equals))), std::string(Trim(value)));
        current->keyLines.push_back(lineNumber);
    }

    // "name : kind : default : help"
    auto lmbda_param = [](const std::string& value, ParamDef& param) {
        std::vector<std::string> parts;
        std::size_t from = 0;
        for (int i = 0; i < 3; ++i) {
            const std::size_t colon = value.find(':', from);
            if (colon == std::string::npos)
                break;
            parts.emplace_back(Trim(std::string_view(value).substr(from, colon - from)));
            from = colon + 1;
        }
        parts.emplace_back(Trim(std::string_view(value).substr(from)));
        if (parts.size() < 2 || parts[0].empty())
            return false;
        const auto kind = ParseKind(parts[1]);
        if (!kind)
            return false;
        param.name = parts[0];
        param.kind = *kind;
        param.defaultValue = parts.size() > 2 ? parts[2] : std::string();
        param.help = parts.size() > 3 ? parts[3] : std::string();
        return true;
    };

    std::size_t fileSections = 0;
    for (const Entry& entry : entries) {
        if (entry.kind == "file" && ++fileSections > 1)
            set.problems.push_back(std::format("line {}: [file] appears again", entry.lineNumber));
    }
    for (const Entry& entry : entries) {
        if (entry.kind == "file") {
            for (std::size_t i = 0; i < entry.keys.size(); ++i) {
                if (entry.keys[i].first == "version" && ParseInt(entry.keys[i].second, set.version))
                    continue;
                set.problems.push_back(std::format("line {}: [file] takes only \"version = <number>\"", entry.keyLines[i]));
            }
            continue;
        }
        if (entry.kind != "action" && entry.kind != "setting")
            continue;
        std::string why;
        if (entry.kind == "action") {
            ActionDef def;
            def.name  = entry.name;
            def.label = entry.name;
            std::vector<std::pair<std::string, std::size_t>> prefills;      // value, line
            for (std::size_t i = 0; i < entry.keys.size() && why.empty(); ++i) {
                const auto& [key, value] = entry.keys[i];
                if (key == "label")          { if (!value.empty()) def.label = value; }
                else if (key == "from")      { for (const std::string& word : SplitWords(value)) def.from.insert(word); }
                else if (key == "confirmed") { for (const std::string& word : SplitWords(value)) def.confirmed.insert(word); }
                else if (key == "version")   { if (!value.empty() && !ParseInt(value, def.version)) why = std::format("line {}: version must be a number", entry.keyLines[i]); }
                else if (key == "help")      { def.help += (def.help.empty() ? "" : "\n") + value; }
                else if (key == "param") {
                    ParamDef param;
                    if (!lmbda_param(value, param))
                        why = std::format("line {}: param needs \"name : kind : default : help\"", entry.keyLines[i]);
                    else
                        def.params.push_back(param);
                } else if (key == "prefill") {
                    prefills.emplace_back(value, entry.keyLines[i]);
                } else if (key == "reverse") {
                    def.reverseParam = value;
                } else {
                    why = std::format("line {}: unknown key '{}'", entry.keyLines[i], key);
                }
            }
            for (const auto& [value, keyLine] : prefills) {
                if (!why.empty())
                    break;
                const std::size_t colon = value.find(':');
                PrefillRule rule;
                rule.fromType = colon == std::string::npos ? std::string() : std::string(Trim(std::string_view(value).substr(0, colon)));
                if (colon != std::string::npos) {
                    for (const std::string& word : SplitWords(std::string_view(value).substr(colon + 1))) {
                        int param = 0;
                        if (word == "-")
                            rule.objParams.push_back(-1);
                        else if (ParseInt(word, param) && param >= 1)
                            rule.objParams.push_back(param - 1);
                        else
                            rule.objParams.clear(), rule.fromType.clear();
                    }
                }
                if (rule.fromType.empty() || rule.objParams.size() != def.params.size())
                    why = std::format("line {}: prefill needs \"<type> : \" and one number or '-' per param", keyLine);
                else
                    def.prefill.push_back(rule);
            }
            if (why.empty()) {
                if (const ActionDef* known = set.FindAction(def.name)) {
                    set.problems.push_back(std::format("line {}: [action {}] appears twice - the last one is used",
                                                       entry.lineNumber, def.name));
                    *const_cast<ActionDef*>(known) = def;
                } else {
                    set.actions.push_back(def);
                }
                continue;
            }
            set.problems.push_back(std::format("[action {}] {} - skipped", entry.name, why));
            continue;
        }

        SettingDef def;
        def.keyword = entry.name;
        def.label   = entry.name;
        for (std::size_t i = 0; i < entry.keys.size() && why.empty(); ++i) {
            const auto& [key, value] = entry.keys[i];
            if (key == "label")        { if (!value.empty()) def.label = value; }
            else if (key == "for")     { for (const std::string& word : SplitWords(value)) def.forTypes.insert(word); }
            else if (key == "group")   { def.group = value; }
            else if (key == "reverse") { def.reverseParam = value; }
            else if (key == "version") { if (!value.empty() && !ParseInt(value, def.version)) why = std::format("line {}: version must be a number", entry.keyLines[i]); }
            else if (key == "help")    { def.help += (def.help.empty() ? "" : "\n") + value; }
            else if (key == "param") {
                ParamDef param;
                if (!lmbda_param(value, param))
                    why = std::format("line {}: param needs \"name : kind : default : help\"", entry.keyLines[i]);
                else
                    def.params.push_back(param);
            } else {
                why = std::format("line {}: unknown key '{}'", entry.keyLines[i], key);
            }
        }
        if (why.empty()) {
            if (const SettingDef* known = set.FindSetting(def.keyword)) {
                set.problems.push_back(std::format("line {}: [setting {}] appears twice - the last one is used",
                                                   entry.lineNumber, def.keyword));
                *const_cast<SettingDef*>(known) = def;
            } else {
                set.settings.push_back(def);
            }
            continue;
        }
        set.problems.push_back(std::format("[setting {}] {} - skipped", entry.name, why));
    }

    // A reverse param must be one of the entry's params.
    auto lmbda_checkReverse = [&set](const std::string& what, std::string& reverse, const std::vector<ParamDef>& params) {
        if (!reverse.empty() && std::none_of(params.begin(), params.end(), [&](const ParamDef& param) { return param.name == reverse; })) {
            set.problems.push_back(std::format("{}: reverse = {} is not one of its params - ignored", what, reverse));
            reverse.clear();
        }
    };
    for (ActionDef& def : set.actions)
        lmbda_checkReverse("[action " + def.name + "]", def.reverseParam, def.params);
    for (SettingDef& def : set.settings)
        lmbda_checkReverse("[setting " + def.keyword + "]", def.reverseParam, def.params);
    if (set.actions.empty() && set.settings.empty())
        set.problems.push_back("no usable entry");
    return set;
}

ActionIniFile::ActionIniFile(fs::path folder)
    : mFile(std::move(folder), kIniName, kActionsFileVersion)
{
}

const ActionSet& ActionIniFile::Load()
{
    std::string text;
    if (!mFile.Read(text))
        return mActions;
    mActions = ParseActionsIni(text);

    // The vrconfig reader: which lines belong inside a block.
    std::set<std::string, std::less<>> keywords;
    for (const SettingDef& def : mActions.settings)
        keywords.insert(def.keyword);
    vrconfig::SetManipDirectives(std::move(keywords));

    if (!mFile.Found()) {
        LOG_ERROR("{}", mFile.Notice(0));
        return mActions;
    }
    LOG_INFO("{}: {} actions, {} settings{}", mFile.UsedName(), mActions.actions.size(), mActions.settings.size(),
             mActions.problems.empty() ? "" : std::format(", {} problems", mActions.problems.size()));
    for (const std::string& problem : mActions.problems)
        LOG_WARN("{}: {}", mFile.UsedName(), problem);
    if (const std::string notice = Notice(); !notice.empty())
        LOG_WARN("{}", notice);
    return mActions;
}

} // namespace hpvr::manip

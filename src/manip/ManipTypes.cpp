// hpvr - the manipulator types that can be extracted (manip_types.ini)

#include "ManipTypes.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <system_error>

#include "Log.h"
#include "vrconfig/VrConfigFile.h"

namespace hpvr::manip {

namespace fs = std::filesystem;

namespace {

constexpr const char* kIniName = "manip_types.ini";

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

// Words of a tooltip / command name, lower case, split at anything that is not a letter or digit.
std::vector<std::string> NameWords(std::string_view text)
{
    std::vector<std::string> words;
    std::string word;
    for (char character : text) {
        if (std::isalnum(static_cast<unsigned char>(character))) {
            word += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        } else if (!word.empty()) {
            words.push_back(word);
            word.clear();
        }
    }
    if (!word.empty())
        words.push_back(word);
    return words;
}

bool ParseInt(std::string_view text, int& value)
{
    text = Trim(text);
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc() && ptr == text.data() + text.size();
}

bool WordMatches(const std::string& ruleWord, const std::string& word)
{
    if (!ruleWord.empty() && ruleWord.back() == '*')
        return word.starts_with(std::string_view(ruleWord).substr(0, ruleWord.size() - 1));
    return word == ruleWord;
}

} // namespace

const char* CategoryLabel(ManipCategory category)
{
    switch (category) {
        case ManipCategory::Knob:   return "Knobs";
        case ManipCategory::Switch: return "Switches";
        case ManipCategory::Lever:  return "Levers";
        case ManipCategory::Yoke:   return "Yoke";
    }
    return "";
}

const char* CategoryShortLabel(ManipCategory category)
{
    switch (category) {
        case ManipCategory::Knob:   return "knob";
        case ManipCategory::Switch: return "switch";
        case ManipCategory::Lever:  return "lever";
        case ManipCategory::Yoke:   return "yoke";
    }
    return "";
}

std::optional<ManipCategory> ParseCategory(std::string_view text)
{
    const std::string lower = Lower(Trim(text));
    for (ManipCategory category : kAllCategories) {
        if (lower == CategoryShortLabel(category))
            return category;
    }
    return std::nullopt;
}

const ManipTypeDef* ManipTypeSet::Find(std::string_view type) const
{
    for (const ManipTypeDef& def : types) {
        if (def.type == type)
            return &def;
    }
    return nullptr;
}

ManipTypeSet ManipTypeSet::ForExtract() const
{
    ManipTypeSet set = *this;
    set.types.clear();
    for (const ManipTypeDef& def : types) {
        if (def.extract)
            set.types.push_back(def);
        else
            set.ignored.insert(def.type);
    }
    return set;
}

int ManipTypeSet::DegreesFor(std::string_view tooltip, const std::vector<std::string>& refs) const
{
    std::string text(tooltip);
    for (const std::string& ref : refs)
        text += " " + ref;
    const std::vector<std::string> words = NameWords(text);
    for (const DegreeRule& rule : degreeRules) {
        for (const std::string& ruleWord : rule.words) {
            for (const std::string& word : words) {
                if (WordMatches(ruleWord, word))
                    return rule.degrees;
            }
        }
    }
    return defaultDegrees;
}

ManipTypeSet ParseTypeIni(std::string_view text)
{
    ManipTypeSet set;
    bool haveDefaults = false;

    struct Entry {
        std::string                                       name;
        std::size_t                                       lineNumber = 0;
        std::vector<std::pair<std::string, std::string>>  keys;          // in file order
        std::vector<std::size_t>                          keyLines;
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
            const std::string name = Lower(Trim(line.substr(1, close == std::string_view::npos ? line.size() - 1 : close - 1)));
            entries.push_back({name, lineNumber, {}, {}});
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
            value = value.substr(0, comment);                  // "value   ; comment"
        current->keys.emplace_back(Lower(Trim(line.substr(0, equals))), std::string(Trim(value)));
        current->keyLines.push_back(lineNumber);
    }

    std::map<std::string, std::size_t> typeIndex;              // type -> index in set.types
    std::map<std::string, std::size_t> specialSeen;            // [file] / [defaults] / [ignore] -> first line
    for (const Entry& entry : entries) {
        if (entry.name != "file" && entry.name != "defaults" && entry.name != "ignore")
            continue;
        if (const auto first = specialSeen.find(entry.name); first != specialSeen.end())
            set.problems.push_back(std::format("line {}: [{}] appears again (first on line {})", entry.lineNumber,
                                               entry.name, first->second));
        else
            specialSeen.emplace(entry.name, entry.lineNumber);
    }

    for (const Entry& entry : entries) {
        if (entry.name == "file") {
            for (std::size_t i = 0; i < entry.keys.size(); ++i) {
                if (entry.keys[i].first == "version" && ParseInt(entry.keys[i].second, set.version))
                    continue;
                set.problems.push_back(std::format("line {}: [file] takes only \"version = <number>\"", entry.keyLines[i]));
            }
            continue;
        }
        if (entry.name == "defaults") {
            haveDefaults = true;
            for (std::size_t i = 0; i < entry.keys.size(); ++i) {
                const auto& [key, value] = entry.keys[i];
                if (key == "deg_per_action") {
                    int degrees = 0;
                    if (ParseInt(value, degrees) && degrees > 0)
                        set.defaultDegrees = degrees;
                    else
                        set.problems.push_back(std::format("line {}: deg_per_action needs a number above 0", entry.keyLines[i]));
                } else if (key == "deg_rule") {
                    const std::size_t colon = value.rfind(':');
                    DegreeRule rule;
                    if (colon != std::string::npos && ParseInt(std::string_view(value).substr(colon + 1), rule.degrees) &&
                        rule.degrees > 0) {
                        for (const std::string& word : SplitWords(Lower(std::string_view(value).substr(0, colon))))
                            rule.words.push_back(word);
                    }
                    if (rule.words.empty())
                        set.problems.push_back(std::format("line {}: deg_rule needs \"words : degrees\"", entry.keyLines[i]));
                    else
                        set.degreeRules.push_back(rule);
                } else {
                    set.problems.push_back(std::format("line {}: unknown key '{}' in [defaults]", entry.keyLines[i], key));
                }
            }
            continue;
        }
        if (entry.name == "ignore") {
            for (std::size_t i = 0; i < entry.keys.size(); ++i) {
                if (entry.keys[i].first != "types") {
                    set.problems.push_back(std::format("line {}: unknown key '{}' in [ignore]", entry.keyLines[i], entry.keys[i].first));
                    continue;
                }
                for (const std::string& type : SplitWords(entry.keys[i].second))
                    set.ignored.insert(type);
            }
            continue;
        }

        // A manipulator type.
        std::string type = entry.name;
        if (type.starts_with("attr_manip_"))
            type = type.substr(11);
        ManipTypeDef def;
        def.type  = type;
        def.label = type;
        std::string why;
        bool haveCategory = false, haveParams = false, haveRefs = false;
        for (std::size_t i = 0; i < entry.keys.size() && why.empty(); ++i) {
            const auto& [key, value] = entry.keys[i];
            const std::size_t keyLine = entry.keyLines[i];
            if (key == "category") {
                const auto category = ParseCategory(value);
                if (!category)
                    why = std::format("line {}: category must be knob, switch, lever or yoke", keyLine);
                else
                    def.category = *category;
                haveCategory = true;
            } else if (key == "label") {
                if (!value.empty())
                    def.label = value;
            } else if (key == "params") {
                if (!ParseInt(value, def.params) || def.params < 2 || def.params > 32)
                    why = std::format("line {}: params must be a number from 2 to 32", keyLine);
                haveParams = true;
            } else if (key == "refs") {
                def.refs.clear();
                for (const std::string& word : SplitWords(value)) {
                    int ref = 0;
                    if (!ParseInt(word, ref)) {
                        why = std::format("line {}: refs must be numbers", keyLine);
                        break;
                    }
                    def.refs.push_back(ref - 1);
                }
                haveRefs = true;
            } else if (key == "checked") {
                const std::string lower = Lower(value);
                def.checked = lower == "yes" || lower == "true" || lower == "1";
            } else if (key == "refs_are") {
                const std::string lower = Lower(value);
                if (lower == "commands" || lower == "command")
                    def.commandRefs = true;
                else if (lower == "datarefs" || lower == "dataref")
                    def.commandRefs = false;
                else
                    why = std::format("line {}: refs_are must be commands or datarefs", keyLine);
            } else if (key == "extract") {
                const std::string lower = Lower(value);
                def.extract = !(lower == "no" || lower == "false" || lower == "0");
            } else if (key == "line") {
                def.lines.push_back(value);
            } else if (key == "help") {
                def.help += (def.help.empty() ? "" : "\n") + value;
            } else {
                why = std::format("line {}: unknown key '{}'", keyLine, key);
            }
        }
        if (why.empty()) {
            if (!haveCategory)    why = "category missing";
            else if (!haveParams) why = "params missing";
            else if (!haveRefs || def.refs.empty() || def.refs.size() > 2)
                why = "refs needs 1 or 2 numbers";
            else {
                for (int ref : def.refs) {
                    if (ref < 1 || ref >= def.params)          // parameter 1 is the cursor
                        why = std::format("refs must be between 2 and params ({})", def.params);
                }
            }
            if (!why.empty())
                why = std::format("line {}: [{}]: {}", entry.lineNumber, entry.name, why);
        } else {
            why = std::format("[{}] {}", entry.name, why);
        }
        if (!why.empty()) {
            set.problems.push_back(why + " - skipped");
            continue;
        }
        if (const auto known = typeIndex.find(type); known != typeIndex.end()) {
            set.problems.push_back(std::format("line {}: [{}] appears twice - the last one is used", entry.lineNumber, entry.name));
            set.types[known->second] = def;
        } else {
            typeIndex[type] = set.types.size();
            set.types.push_back(def);
        }
    }

    if (set.types.empty())
        set.problems.push_back("no usable manipulator type");
    if (!haveDefaults)
        set.problems.push_back(std::format("[defaults] missing - deg_per_action {} used", set.defaultDegrees));
    for (const ManipTypeDef& def : set.types)
        set.ignored.erase(def.type);                           // defined wins over ignored
    return set;
}

TypeIniFile::TypeIniFile(fs::path folder)
    : mFile(std::move(folder), kIniName, kTypesFileVersion)
{
}

const ManipTypeSet& TypeIniFile::Load()
{
    mParsedLastTime = false;
    std::string text;
    if (!mFile.Read(text))
        return mTypes;                                          // unchanged: keep the parsed list
    mTypes = ParseTypeIni(text);
    mParsedLastTime = true;

    // The vrconfig reader: how many commands / datarefs each type's BEGIN_MANIP line has.
    std::map<std::string, std::size_t, std::less<>> refCounts;
    for (const ManipTypeDef& def : mTypes.types)
        refCounts[def.type] = def.refs.size();
    vrconfig::SetManipRefCounts(std::move(refCounts));

    if (!mFile.Found()) {
        LOG_ERROR("{}", mFile.Notice(0));
        return mTypes;
    }
    LOG_INFO("{}: {} manipulator types, {} ignored{}", mFile.UsedName(), mTypes.types.size(), mTypes.ignored.size(),
             mTypes.problems.empty() ? "" : std::format(", {} problems", mTypes.problems.size()));
    for (const std::string& problem : mTypes.problems)
        LOG_WARN("{}: {}", mFile.UsedName(), problem);
    if (const std::string notice = Notice(); !notice.empty())
        LOG_WARN("{}", notice);
    return mTypes;
}

} // namespace hpvr::manip

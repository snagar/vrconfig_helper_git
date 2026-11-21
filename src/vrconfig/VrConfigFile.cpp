// vrconfig_helper - <aircraft>_vrconfig.txt reader / writer

#include "VrConfigFile.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <system_error>

#include "Log.h"
#include "util/FileUtil.h"

namespace hpvr::vrconfig {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kBegin    = "BEGIN_TELEPORT_HOTSPOT";
constexpr std::string_view kEnd      = "END_TELEPORT_HOTSPOT";
constexpr std::string_view kBox      = "AABB";
constexpr std::string_view kPosition = "PRESET_XYZ";
constexpr std::string_view kHeading  = "PRESET_PSI";
constexpr std::string_view kPitch    = "PRESET_THE";
constexpr std::string_view kRoll     = "PRESET_PHI";
constexpr std::string_view kRefPoint = "REF_POINT_ACF";
constexpr std::string_view kUtf8Bom  = "\xEF\xBB\xBF";
// Signature line: "# <Made|Modified> with VRConfig Helper - by Snagar.Dev".
constexpr std::string_view kSignatureMark = "with VRConfig Helper - by Snagar.Dev";
constexpr std::string_view kManipBegin = "BEGIN_MANIP";
constexpr std::string_view kManipEnd   = "END_MANIP";

constexpr double kEpsilon = 0.00005;     // half of the last written decimal

bool IsSpace(char character) { return character == ' ' || character == '\t'; }

std::string_view TrimLeft(std::string_view text)
{
    std::size_t first = 0;
    while (first < text.size() && IsSpace(text[first]))
        ++first;
    return text.substr(first);
}

std::string_view Trim(std::string_view text)
{
    text = TrimLeft(text);
    while (!text.empty() && (IsSpace(text.back()) || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

std::string LeadingSpace(std::string_view line)
{
    std::size_t i = 0;
    while (i < line.size() && IsSpace(line[i]))
        ++i;
    return std::string(line.substr(0, i));
}

// Splits off the first whitespace separated token.
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

bool ParseNumber(std::string_view token, double& out)
{
    if (!token.empty() && token.front() == '+')
        token.remove_prefix(1);
    if (token.empty())
        return false;
    const auto result = std::from_chars(token.data(), token.data() + token.size(), out);
    return result.ec == std::errc() && result.ptr == token.data() + token.size();
}

// Reads exactly `count` numbers from `rest`; extra tokens are an error.
template <std::size_t Count>
bool ParseNumbers(std::string_view rest, std::array<double, Count>& out)
{
    for (std::size_t i = 0; i < Count; ++i) {
        if (!ParseNumber(NextToken(rest), out[i]))
            return false;
    }
    return Trim(rest).empty();
}

bool Same(double first, double second) { return std::fabs(first - second) < kEpsilon; }

bool Same(const Vec3& first, const Vec3& second)
{
    return Same(first.x, second.x) && Same(first.y, second.y) && Same(first.z, second.z);
}

std::string BeginText(const TeleportSpot& spot)
{
    std::string text = std::format("{} {}", kBegin, spot.kind == SpotKind::Standing ? "STANDING" : "SITTING");
    if (!spot.name.empty())
        text += " " + spot.name;
    return text;
}

} // namespace

// --- helpers --------------------------------------------------------------------------

const char* KindKeyword(SpotKind kind)
{
    return kind == SpotKind::Standing ? "STANDING" : "SITTING";
}

fs::path VrConfigPathFor(const fs::path& acfFile)
{
    const std::string wanted = util::Utf8(acfFile.stem()) + "_vrconfig.txt";
    const fs::path exact = acfFile.parent_path() / wanted;
    std::error_code ec;
    if (fs::exists(exact, ec))
        return exact;
    auto lmbda_lower = [](std::string text) {
        for (char& character : text)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        return text;
    };
    const std::string wantedLower = lmbda_lower(wanted);
    for (const auto& entry : fs::directory_iterator(acfFile.parent_path(), ec)) {
        if (entry.is_regular_file(ec) && lmbda_lower(util::Utf8(entry.path().filename())) == wantedLower)
            return entry.path();
    }
    return exact;
}

std::string FormatNumber(double value)
{
    if (std::fabs(value) < kEpsilon)
        value = 0.0;                         // never write "-0.0000"
    return std::format("{:.4f}", value);
}

bool SamePosition(const TeleportSpot& first, const TeleportSpot& second)
{
    return Same(first.position, second.position) && Same(first.boxMin, second.boxMin) &&
           Same(first.boxMax, second.boxMax);
}
bool SameHeading(const TeleportSpot& first, const TeleportSpot& second)
{
    return Same(first.heading, second.heading);
}
bool SameTilt(const TeleportSpot& first, const TeleportSpot& second)
{
    return Same(first.pitch, second.pitch) && Same(first.roll, second.roll);
}
bool SameName(const TeleportSpot& first, const TeleportSpot& second)
{
    return first.name == second.name && first.kind == second.kind;
}
bool SameSpot(const TeleportSpot& first, const TeleportSpot& second)
{
    return SamePosition(first, second) && SameHeading(first, second) && SameTilt(first, second) &&
           SameName(first, second);
}

// --- VrConfigFile -------------------------------------------------------------------

std::string ManipRefName(std::string_view token)
{
    if (token.starts_with("CMND=") || token.starts_with("DREF="))
        token.remove_prefix(5);
    return std::string(token);
}

namespace {

// Told by the rule files when they are read (drawing thread only).
std::map<std::string, std::size_t, std::less<>>& RefCounts()
{
    static std::map<std::string, std::size_t, std::less<>> counts;
    return counts;
}

std::set<std::string, std::less<>>& Directives()
{
    static std::set<std::string, std::less<>> keywords;
    return keywords;
}

} // namespace

std::size_t ManipRefCount(std::string_view type)
{
    const auto found = RefCounts().find(type);
    return found == RefCounts().end() ? 0 : found->second;
}

void SetManipRefCounts(std::map<std::string, std::size_t, std::less<>> counts)
{
    RefCounts() = std::move(counts);
}

bool IsManipDirective(std::string_view keyword)
{
    return keyword == "ACTION" || Directives().contains(keyword);
}

void SetManipDirectives(std::set<std::string, std::less<>> keywords)
{
    Directives() = std::move(keywords);
}

bool VrConfigFile::Load(const fs::path& path)
{
    mFilePath = path;
    mCreatedNew = false;
    mLines.clear();
    mSpots.clear();
    mNotes.clear();
    mReferencePoint.reset();
    mLastError.clear();

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        mLastError = std::format("cannot open {}", util::Utf8(path));
        LOG_WARN("{}", mLastError);
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    mLineEnd = (text.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
    mEndsWithLineEnd = text.empty() || text.back() == '\n';

    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        mLines.push_back(std::move(line));
        start = end + 1;
    }

    ParseLines();
    LOG_INFO("{}: {} teleport spots, {} lines{}", util::Utf8(path.filename()), mSpots.size(),
             mLines.size(), mReferencePoint ? ", uses REF_POINT_ACF" : "");
    return true;
}

void VrConfigFile::ParseLines()
{
    mSpots.clear();
    mNotes.clear();
    mManipNotes.clear();
    mReferencePoint.reset();

    // Header: the first three meaningful lines must be "A", "1100", "VRCONFIG".
    std::size_t headerEnd = 0;                  // first line after the header
    {
        std::array<std::string_view, 3> header{};
        std::size_t found = 0;
        for (std::size_t i = 0; i < mLines.size() && found < 3; ++i) {
            std::string_view content = Trim(mLines[i]);
            if (i == 0 && content.starts_with(kUtf8Bom))
                content.remove_prefix(kUtf8Bom.size());
            if (content.empty() || content.front() == '#')
                continue;
            header[found++] = content;
            headerEnd = i + 1;
        }
        if (found < 3 || header[0] != "A" || header[2] != "VRCONFIG") {
            mNotes.push_back("the file does not start with the A / 1100 / VRCONFIG header");
            mHeaderEnd = 0;
        } else {
            mHeaderEnd = headerEnd;
        }
    }

    SpotRecord current;
    bool inBlock = false;
    bool inManip = false;
    std::size_t commaLine = 0;                  // first value line of the block with commas (1-based)

    auto lmbda_closeBlock = [&](bool ended) {
        if (!ended && current.problem.empty())
            current.problem = "END_TELEPORT_HOTSPOT missing";
        if (current.problem.empty()) {
            if (current.boxIdx == SpotRecord::kNoLine)           current.problem = "AABB missing";
            else if (current.positionIdx == SpotRecord::kNoLine) current.problem = "PRESET_XYZ missing";
            else if (current.headingIdx == SpotRecord::kNoLine)  current.problem = "PRESET_PSI missing";
            else if (current.pitchIdx == SpotRecord::kNoLine)    current.problem = "PRESET_THE missing";
            else if (current.rollIdx == SpotRecord::kNoLine)     current.problem = "PRESET_PHI missing";
        }
        if (current.problem.empty() && commaLine != 0) {
            current.problem    = std::format("line {}: commas between the numbers", commaLine);
            current.commasOnly = true;
        }
        current.editable = current.problem.empty();
        if (!current.editable) {
            mNotes.push_back(std::format("line {}: '{}' cannot be edited: {}",
                                         current.lineNumber, current.spot.name, current.problem));
            LOG_WARN_AS("lmbda_closeBlock", "{}", mNotes.back());
        }
        mSpots.push_back(current);
        current = SpotRecord{};
        commaLine = 0;
        inBlock = false;
    };

    // Records one directive line of the current block; a second copy is an error.
    auto lmbda_setOnce = [&](std::size_t& slot, std::size_t idx, std::string_view keyword) {
        if (slot != SpotRecord::kNoLine && current.problem.empty())
            current.problem = std::format("{} appears twice", keyword);
        slot = idx;
    };

    // Lines outside every block that X-Plane does not expect there. Neighbouring
    // ones (only blank / comment lines between) become one note.
    struct Stray {
        std::size_t first = 0, last = 0;         // 1-based
        std::string text;                        // about the first line
        bool        manip = false;               // a manipulator directive
    };
    std::optional<Stray> stray;
    auto lmbda_flushStray = [&]() {
        if (!stray)
            return;
        const std::string where = stray->first == stray->last ? std::format("line {}", stray->first)
                                                              : std::format("lines {}-{}", stray->first, stray->last);
        mNotes.push_back(std::format("{}: {}", where, stray->text));
        if (stray->manip)
            mManipNotes.push_back(mNotes.back());
        LOG_WARN_AS("lmbda_flushStray", "{}", mNotes.back());
        stray.reset();
    };
    auto lmbda_stray = [&](std::size_t idx, std::string_view keyword) {
        if (stray) {
            stray->last = idx + 1;
            return;
        }
        Stray found;
        found.first = found.last = idx + 1;
        if (keyword == kEnd) {
            found.text = "END_TELEPORT_HOTSPOT without BEGIN_TELEPORT_HOTSPOT";
        } else if (keyword == kManipEnd) {
            found.text  = "END_MANIP without BEGIN_MANIP";
            found.manip = true;
        } else if (keyword == kBox || keyword == kPosition || keyword == kHeading || keyword == kPitch ||
                   keyword == kRoll) {
            found.text = std::format("{} outside a BEGIN_TELEPORT_HOTSPOT block - X-Plane ignores it", keyword);
        } else if (IsManipDirective(keyword)) {
            found.text  = std::format("{} outside a BEGIN_MANIP block - X-Plane ignores it", keyword);
            found.manip = true;
        } else {
            found.text = std::format("'{}' is not a vrconfig keyword VRConfig Helper knows - check the spelling",
                                     keyword);
        }
        stray = found;
    };

    for (std::size_t idx = 0; idx < mLines.size(); ++idx) {
        std::string_view rest = mLines[idx];
        if (idx == 0 && rest.starts_with(kUtf8Bom))
            rest.remove_prefix(kUtf8Bom.size());
        rest = TrimLeft(rest);
        if (rest.empty() || rest.front() == '#')
            continue;

        const std::string_view keyword = NextToken(rest);

        if (keyword == kBegin) {
            lmbda_flushStray();
            if (inBlock)
                lmbda_closeBlock(false);
            inManip = false;
            inBlock = true;
            current.lineNumber = idx + 1;
            current.beginIdx   = idx;
            const std::string_view kind = NextToken(rest);
            current.spot.name = std::string(Trim(rest));
            if (kind == "SITTING")
                current.spot.kind = SpotKind::Sitting;
            else if (kind == "STANDING")
                current.spot.kind = SpotKind::Standing;
            else
                current.problem = std::format("unknown type '{}' (SITTING or STANDING)", kind);
            continue;
        }

        if (keyword == kRefPoint) {
            lmbda_flushStray();
            // REF_POINT_ACF [y [z]] - missing values mean 0.0
            Vec3 ref;
            double value = 0.0;
            if (ParseNumber(NextToken(rest), value)) ref.y = value;
            if (ParseNumber(NextToken(rest), value)) ref.z = value;
            mReferencePoint = ref;
            continue;
        }

        if (!inBlock) {
            // Manipulator blocks are read by ParseManips; here only where they are.
            if (keyword == kManipBegin) {
                lmbda_flushStray();
                inManip = true;
            } else if (inManip) {
                if (keyword == kManipEnd)
                    inManip = false;
            } else if (idx >= headerEnd) {
                lmbda_stray(idx, keyword);
            }
            continue;
        }

        if (keyword == kEnd) {
            current.endIdx = idx;
            lmbda_closeBlock(true);
            continue;
        }

        // Commas between the numbers: read as spaces, reported (FixCommas removes them).
        std::string values(rest);
        if (values.find(',') != std::string::npos) {
            std::replace(values.begin(), values.end(), ',', ' ');
            if ((keyword == kBox || keyword == kPosition || keyword == kHeading || keyword == kPitch ||
                 keyword == kRoll) && commaLine == 0)
                commaLine = idx + 1;
        }
        rest = values;

        if (keyword == kBox) {
            lmbda_setOnce(current.boxIdx, idx, kBox);
            std::array<double, 6> numbers{};
            if (ParseNumbers(rest, numbers)) {
                current.spot.boxMin = {numbers[0], numbers[1], numbers[2]};
                current.spot.boxMax = {numbers[3], numbers[4], numbers[5]};
            } else if (current.problem.empty()) {
                current.problem = std::format("line {}: AABB needs 6 numbers", idx + 1);
            }
        } else if (keyword == kPosition) {
            lmbda_setOnce(current.positionIdx, idx, kPosition);
            std::array<double, 3> numbers{};
            if (ParseNumbers(rest, numbers))
                current.spot.position = {numbers[0], numbers[1], numbers[2]};
            else if (current.problem.empty())
                current.problem = std::format("line {}: PRESET_XYZ needs 3 numbers", idx + 1);
        } else if (keyword == kHeading || keyword == kPitch || keyword == kRoll) {
            std::size_t& slot = keyword == kHeading ? current.headingIdx
                              : keyword == kPitch   ? current.pitchIdx
                                                    : current.rollIdx;
            double& value     = keyword == kHeading ? current.spot.heading
                              : keyword == kPitch   ? current.spot.pitch
                                                    : current.spot.roll;
            lmbda_setOnce(slot, idx, keyword);
            std::array<double, 1> numbers{};
            if (ParseNumbers(rest, numbers))
                value = numbers[0];
            else if (current.problem.empty())
                current.problem = std::format("line {}: {} needs 1 number", idx + 1, keyword);
        }
        // other lines inside a block are kept as they are
    }

    if (inBlock)
        lmbda_closeBlock(false);
    lmbda_flushStray();

    // The same name twice: the user cannot tell them apart in VR. One note per name.
    std::vector<char> reported(mSpots.size(), 0);
    for (std::size_t first = 0; first < mSpots.size(); ++first) {
        if (reported[first] || mSpots[first].spot.name.empty())
            continue;
        std::string lines = std::to_string(mSpots[first].lineNumber);
        std::size_t uses = 1;
        for (std::size_t second = first + 1; second < mSpots.size(); ++second) {
            if (mSpots[second].spot.name == mSpots[first].spot.name) {
                reported[second] = 1;
                lines += ", " + std::to_string(mSpots[second].lineNumber);
                ++uses;
            }
        }
        if (uses > 1)
            mNotes.push_back(std::format("hotspot name '{}' is used {} times (lines {})", mSpots[first].spot.name,
                                         uses, lines));
    }

    ParseManips();
}

void VrConfigFile::ParseManips()
{
    mManips.clear();
    bool inManip = false;
    auto lmbda_note = [this](std::string text) {
        mNotes.push_back(text);
        mManipNotes.push_back(std::move(text));
    };
    for (std::size_t idx = 0; idx < mLines.size(); ++idx) {
        std::string_view rest = mLines[idx];
        if (idx == 0 && rest.starts_with(kUtf8Bom))
            rest.remove_prefix(kUtf8Bom.size());
        rest = TrimLeft(rest);
        if (rest.empty() || rest.front() == '#')
            continue;
        const std::string_view keyword = NextToken(rest);
        if (keyword == kManipBegin) {
            if (inManip)
                lmbda_note(std::format("line {}: END_MANIP missing", mManips.back().lineNumber));
            ManipRecord rec;
            rec.lineNumber = idx + 1;
            rec.beginIdx   = idx;
            rec.type       = std::string(NextToken(rest));
            const std::size_t wanted = ManipRefCount(rec.type);
            while (wanted == 0 || rec.refs.size() < wanted) {
                const std::string_view token = NextToken(rest);
                if (token.empty())
                    break;
                if (token.front() == '#') {              // comment at the end of the line
                    rest = {};
                    break;
                }
                rec.rawRefs.emplace_back(token);
                rec.refs.push_back(ManipRefName(token));
            }
            rec.tooltip = std::string(Trim(rest));
            if (const std::size_t hash = rec.tooltip.find(" #"); hash != std::string::npos)
                rec.tooltip = std::string(Trim(std::string_view(rec.tooltip).substr(0, hash)));
            if (!rec.tooltip.empty() && rec.tooltip.front() == '#')
                rec.tooltip.clear();
            mManips.push_back(std::move(rec));
            inManip = true;
        } else if (keyword == kManipEnd && inManip) {
            mManips.back().endIdx = idx;
            inManip = false;
        } else if (keyword == kBegin && inManip) {
            lmbda_note(std::format("line {}: END_MANIP missing (a hotspot starts on line {})",
                                   mManips.back().lineNumber, idx + 1));
            inManip = false;
        }
    }
    if (inManip)
        lmbda_note(std::format("line {}: END_MANIP missing", mManips.back().lineNumber));
}

namespace {

std::string BoxLine(const TeleportSpot& spot)
{
    return std::format("{} {} {} {} {} {} {}", kBox,
        FormatNumber(spot.boxMin.x), FormatNumber(spot.boxMin.y), FormatNumber(spot.boxMin.z),
        FormatNumber(spot.boxMax.x), FormatNumber(spot.boxMax.y), FormatNumber(spot.boxMax.z));
}
std::string PositionLine(const TeleportSpot& spot)
{
    return std::format("{} {} {} {}", kPosition,
        FormatNumber(spot.position.x), FormatNumber(spot.position.y), FormatNumber(spot.position.z));
}
std::string HeadingLine(const TeleportSpot& spot) { return std::format("{} {}", kHeading, FormatNumber(spot.heading)); }
std::string PitchLine(const TeleportSpot& spot)   { return std::format("{} {}", kPitch,   FormatNumber(spot.pitch)); }
std::string RollLine(const TeleportSpot& spot)    { return std::format("{} {}", kRoll,    FormatNumber(spot.roll)); }

bool IsBlank(const std::string& line) { return Trim(line).empty(); }

} // namespace

void VrConfigFile::CreateNew(const fs::path& path)
{
    mFilePath = path;
    mLines = {"A", "1100", "VRCONFIG", ""};
    mCreatedNew = true;
    mLineEnd = "\n";
    mEndsWithLineEnd = true;
    mLastError.clear();
    ParseLines();
    LOG_INFO("new vrconfig in memory: {}", util::Utf8(path));
}

bool VrConfigFile::UpdateSpot(std::size_t index, const TeleportSpot& spot)
{
    if (index >= mSpots.size() || !mSpots[index].editable) {
        LOG_ERROR("spot {} cannot be updated", index);
        return false;
    }
    SpotRecord& rec = mSpots[index];

    auto lmbda_setLine = [&](std::size_t idx, const std::string& content) {
        mLines[idx] = LeadingSpace(mLines[idx]) + content;
    };

    // Rewrite only the lines whose values changed: smallest possible diff.
    const TeleportSpot& was = rec.spot;
    if (!SameName(spot, was))
        lmbda_setLine(rec.beginIdx, BeginText(spot));
    if (!Same(spot.boxMin, was.boxMin) || !Same(spot.boxMax, was.boxMax))
        lmbda_setLine(rec.boxIdx, BoxLine(spot));
    if (!Same(spot.position, was.position))
        lmbda_setLine(rec.positionIdx, PositionLine(spot));
    if (!Same(spot.heading, was.heading))
        lmbda_setLine(rec.headingIdx, HeadingLine(spot));
    if (!Same(spot.pitch, was.pitch))
        lmbda_setLine(rec.pitchIdx, PitchLine(spot));
    if (!Same(spot.roll, was.roll))
        lmbda_setLine(rec.rollIdx, RollLine(spot));

    rec.spot = spot;
    return true;
}

bool VrConfigFile::RemoveSpot(std::size_t index)
{
    if (index >= mSpots.size() || mSpots[index].endIdx == SpotRecord::kNoLine) {
        LOG_ERROR("spot {} cannot be removed", index);
        return false;
    }
    std::size_t first = mSpots[index].beginIdx;
    std::size_t last  = mSpots[index].endIdx;          // inclusive

    // Also drop one blank separator line, so no double blank lines remain.
    const bool blankBefore = first > 0 && IsBlank(mLines[first - 1]);
    const bool blankAfter  = last + 1 >= mLines.size() || IsBlank(mLines[last + 1]);
    if (blankBefore && blankAfter)
        --first;

    const std::string name = mSpots[index].spot.name;
    mLines.erase(mLines.begin() + static_cast<std::ptrdiff_t>(first),
                 mLines.begin() + static_cast<std::ptrdiff_t>(last + 1));
    ParseLines();
    LOG_DEBUG("removed '{}' (lines {}..{})", name, first + 1, last + 1);
    return true;
}

std::size_t VrConfigFile::AppendSpot(const TeleportSpot& spot)
{
    // Where: after the END line of the last spot, else after the header, else at the end.
    std::size_t at = mLines.size();
    std::string indent = "   ";
    if (!mSpots.empty()) {
        const SpotRecord& lastSpot = mSpots.back();
        if (lastSpot.endIdx != SpotRecord::kNoLine)
            at = lastSpot.endIdx + 1;
        if (lastSpot.boxIdx != SpotRecord::kNoLine)
            indent = LeadingSpace(mLines[lastSpot.boxIdx]);
    } else {
        std::size_t found = 0;
        for (std::size_t i = 0; i < mLines.size() && found < 3; ++i) {
            std::string_view content = Trim(mLines[i]);
            if (i == 0 && content.starts_with(kUtf8Bom))
                content.remove_prefix(kUtf8Bom.size());
            if (content.empty() || content.front() == '#')
                continue;
            if (++found == 3)
                at = i + 1;
        }
    }

    std::vector<std::string> block = {
        "",
        BeginText(spot),
        indent + BoxLine(spot),
        indent + PositionLine(spot),
        indent + HeadingLine(spot),
        indent + PitchLine(spot),
        indent + RollLine(spot),
        std::string(kEnd),
    };
    if (at < mLines.size() && !IsBlank(mLines[at]))
        block.push_back("");                          // keep a blank line before what follows
    mLines.insert(mLines.begin() + static_cast<std::ptrdiff_t>(at), block.begin(), block.end());
    ParseLines();

    for (std::size_t spotIndex = 0; spotIndex < mSpots.size(); ++spotIndex) {
        if (mSpots[spotIndex].beginIdx == at + 1)
            return spotIndex;
    }
    return mSpots.size() - 1;
}

bool VrConfigFile::ReplaceManip(std::size_t index, const std::vector<std::string>& blockLines)
{
    if (index >= mManips.size() || mManips[index].endIdx == ManipRecord::kNoLine) {
        LOG_ERROR("manipulator {} cannot be replaced", index);
        return false;
    }
    const std::size_t first = mManips[index].beginIdx;
    const std::size_t last  = mManips[index].endIdx;          // inclusive
    mLines.erase(mLines.begin() + static_cast<std::ptrdiff_t>(first),
                 mLines.begin() + static_cast<std::ptrdiff_t>(last + 1));
    mLines.insert(mLines.begin() + static_cast<std::ptrdiff_t>(first), blockLines.begin(), blockLines.end());
    ParseLines();
    return true;
}

bool VrConfigFile::RemoveManip(std::size_t index)
{
    if (index >= mManips.size() || mManips[index].endIdx == ManipRecord::kNoLine) {
        LOG_ERROR("manipulator {} cannot be removed", index);
        return false;
    }
    std::size_t first = mManips[index].beginIdx;
    const std::size_t last = mManips[index].endIdx;            // inclusive
    // The comment the Extract page wrote above the block goes with it.
    if (first > 0) {
        const std::string_view above = Trim(mLines[first - 1]);
        if (above.starts_with("#") && above.ends_with("extracted by VRConfig Helper"))
            --first;
    }
    const bool blankBefore = first > 0 && IsBlank(mLines[first - 1]);
    const bool blankAfter  = last + 1 >= mLines.size() || IsBlank(mLines[last + 1]);
    if (blankBefore && blankAfter)
        --first;
    mLines.erase(mLines.begin() + static_cast<std::ptrdiff_t>(first),
                 mLines.begin() + static_cast<std::ptrdiff_t>(last + 1));

    // A group comment the Extract page wrote ("# --- sim/autopilot ---") with
    // nothing left under it (the next line is another group comment or the end)
    // goes too.
    auto lmbda_isGroupComment = [](std::string_view line) {
        line = Trim(line);
        return line.size() > 10 && line.starts_with("# --- ") && line.ends_with(" ---");
    };
    std::size_t above = first;
    while (above > 0 && IsBlank(mLines[above - 1]))
        --above;
    std::size_t below = first;
    while (below < mLines.size() && IsBlank(mLines[below]))
        ++below;
    if (above > 0 && lmbda_isGroupComment(mLines[above - 1]) &&
        (below == mLines.size() || lmbda_isGroupComment(mLines[below]))) {
        std::size_t from = above - 1;
        if (from > 0 && IsBlank(mLines[from - 1]) && (below == mLines.size() || below > first))
            --from;                                           // and one blank line with it
        mLines.erase(mLines.begin() + static_cast<std::ptrdiff_t>(from),
                     mLines.begin() + static_cast<std::ptrdiff_t>(first));
    }
    ParseLines();
    return true;
}

std::vector<std::string> VrConfigFile::ManipLines(std::size_t index) const
{
    if (index >= mManips.size() || mManips[index].endIdx == ManipRecord::kNoLine)
        return {};
    return std::vector<std::string>(mLines.begin() + static_cast<std::ptrdiff_t>(mManips[index].beginIdx),
                                    mLines.begin() + static_cast<std::ptrdiff_t>(mManips[index].endIdx + 1));
}

bool VrConfigFile::FixCommas(std::size_t index)
{
    if (index >= mSpots.size() || !mSpots[index].commasOnly)
        return false;
    const SpotRecord& rec = mSpots[index];
    for (std::size_t idx : {rec.boxIdx, rec.positionIdx, rec.headingIdx, rec.pitchIdx, rec.rollIdx}) {
        if (idx == SpotRecord::kNoLine || mLines[idx].find(',') == std::string::npos)
            continue;
        std::string_view rest = mLines[idx];
        std::string fixed = LeadingSpace(rest);
        std::string cleaned(TrimLeft(rest));
        std::replace(cleaned.begin(), cleaned.end(), ',', ' ');
        std::string_view words = cleaned;
        bool firstWord = true;
        for (std::string_view word = NextToken(words); !word.empty(); word = NextToken(words)) {
            fixed += firstWord ? "" : " ";
            fixed += word;
            firstWord = false;
        }
        LOG_INFO("line {}: commas removed: {}", idx + 1, fixed);
        mLines[idx] = fixed;
    }
    ParseLines();
    return mSpots[index].editable;
}

void VrConfigFile::AppendBlock(const std::vector<std::string>& lines)
{
    while (!mLines.empty() && IsBlank(mLines.back()) && mLines.size() > 1 && IsBlank(mLines[mLines.size() - 2]))
        mLines.pop_back();                                    // at most one blank line at the end
    if (!mLines.empty() && !IsBlank(mLines.back()))
        mLines.push_back("");
    mLines.insert(mLines.end(), lines.begin(), lines.end());
    mEndsWithLineEnd = true;
    ParseLines();
}

std::string VrConfigFile::Text() const
{
    std::string out;
    for (std::size_t i = 0; i < mLines.size(); ++i) {
        out += mLines[i];
        if (i + 1 < mLines.size() || mEndsWithLineEnd)
            out += mLineEnd;
    }
    return out;
}

bool VrConfigFile::HasSignature() const
{
    for (const std::string& line : mLines) {
        const std::string_view content = Trim(line);
        if (content.starts_with('#') && content.find(kSignatureMark) != std::string_view::npos)
            return true;
    }
    return false;
}

bool VrConfigFile::AddSignature()
{
    if (mHeaderEnd == 0 || mHeaderEnd > mLines.size() || HasSignature())
        return false;
    const std::string line = std::format("# {} {}", mCreatedNew ? "Made" : "Modified", kSignatureMark);
    mLines.insert(mLines.begin() + static_cast<std::ptrdiff_t>(mHeaderEnd), line);
    ParseLines();                                             // line numbers moved by one
    LOG_INFO("signature added: {}", line);
    return true;
}

bool VrConfigFile::Save(const fs::path& target)
{
    AddSignature();
    const fs::path dest = target.empty() ? mFilePath : target;
    if (!util::WriteFileSafely(dest, Text(), mLastError)) {
        LOG_ERROR("{}", mLastError);
        return false;
    }
    LOG_INFO("saved {}", util::Utf8(dest));
    return true;
}

} // namespace hpvr::vrconfig

// hpvr - manipulators of the aircraft's OBJ files

#include "ObjManips.h"
#include "util/FileUtil.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <charconv>
#include <set>
#include <system_error>

namespace hpvr::manip {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kManipPrefix = "ATTR_manip_";
constexpr std::size_t      kMaxProblems = 20;        // more are counted, not listed
constexpr std::size_t      kCheckEvery  = 4096;      // lines between abort checks / progress updates

bool IsSpace(char character) { return character == ' ' || character == '\t' || character == '\r'; }

std::string Lower(std::string_view text)
{
    std::string lower(text);
    for (char& character : lower)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return lower;
}

std::string_view NextToken(std::string_view& rest)
{
    std::size_t first = 0;
    while (first < rest.size() && IsSpace(rest[first]))
        ++first;
    std::size_t last = first;
    while (last < rest.size() && !IsSpace(rest[last]))
        ++last;
    const std::string_view token = rest.substr(first, last - first);
    rest = rest.substr(last);
    return token;
}

std::string_view Trim(std::string_view text)
{
    while (!text.empty() && IsSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && IsSpace(text.back()))
        text.remove_suffix(1);
    return text;
}

void ReplaceAll(std::string& text, std::string_view from, std::string_view to)
{
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
}

// The template lines of `def`, with {deg} {name} {file} filled in.
std::vector<std::string> FillTemplate(const ManipTypeDef& def, const ManipTypeSet& types, const ObjManip& manip)
{
    std::vector<std::string> body;
    const std::string degrees = std::to_string(types.DegreesFor(manip.tooltip, manip.refs));
    for (std::string line : def.lines) {
        ReplaceAll(line, "{deg}", degrees);
        ReplaceAll(line, "{name}", manip.DisplayName());
        ReplaceAll(line, "{file}", util::Utf8(manip.objFile.filename()));
        body.push_back(std::move(line));
    }
    return body;
}

} // namespace

std::string ManipGroup(const std::vector<std::string>& refs)
{
    if (refs.empty())
        return {};
    std::string_view ref = refs.front();
    if (ref.starts_with("CMND=") || ref.starts_with("DREF="))
        ref.remove_prefix(5);
    const std::size_t slash = ref.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(ref.substr(0, slash));
}

std::string GroupComment(const std::string& group)
{
    return "# --- " + group + " ---";
}

bool IsGroupComment(std::string_view line)
{
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
        line.remove_suffix(1);
    return line.size() > 10 && line.starts_with("# --- ") && line.ends_with(" ---");
}

std::string ManipKey(const std::string& type, const std::vector<std::string>& refs)
{
    std::string key = type;
    for (const std::string& ref : refs)
        key += " " + ref;
    return key;
}

std::string ObjManip::Key() const
{
    return ManipKey(type, refs);
}

std::string ObjManip::DisplayName() const
{
    if (!tooltip.empty())
        return tooltip;
    if (refs.empty())
        return type;
    // The last part of the command / dataref says the most ("throttle_ratio[0]").
    const std::string& ref = refs.front();
    const std::size_t slash = ref.find_last_of('/');
    return slash == std::string::npos || slash + 1 == ref.size() ? ref : ref.substr(slash + 1);
}

std::vector<FileStamp> StampFiles(const std::vector<fs::path>& files)
{
    std::vector<FileStamp> stamps;
    for (const fs::path& file : files) {
        FileStamp stamp;
        stamp.path = file;
        std::error_code ec;
        stamp.size = fs::file_size(file, ec);
        stamp.time = fs::last_write_time(file, ec);
        stamps.push_back(stamp);
    }
    return stamps;
}

bool StampsChanged(const std::vector<FileStamp>& stamps)
{
    for (const FileStamp& stamp : stamps) {
        std::error_code sizeEc, timeEc;
        const auto size = fs::file_size(stamp.path, sizeEc);
        const auto time = fs::last_write_time(stamp.path, timeEc);
        if (sizeEc || timeEc || size != stamp.size || time != stamp.time)
            return true;
    }
    return false;
}

namespace {

// One file, click areas merged within it. `bytesBefore`: progress of the files before it.
ObjFileScan ScanOneFile(const AcfObject& object, const ManipTypeSet& types, util::StopToken stop,
                        ScanProgress* progress, std::uintmax_t& bytesBefore, bool& aborted)
{
    ObjFileScan scan;
    scan.object = object;
    if (!object.exists)
        return scan;
    scan.stamp = StampFiles({object.path}).front();
    for (const ManipTypeDef& def : types.types)
        scan.controls[def.type] = 0;

    std::ifstream in(object.path, std::ios::binary);
    if (!in) {
        scan.problems.push_back(std::format("cannot read {}", util::Utf8(object.path.filename())));
        return scan;
    }
    std::map<std::string, std::size_t> byKey;          // key -> index in scan.manips
    std::size_t problemCount = 0;
    std::string line;
    std::size_t lineNumber = 0;
    std::uintmax_t bytesInFile = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        bytesInFile += line.size() + 1;
        if (lineNumber % kCheckEvery == 0) {
            if (stop.stop_requested()) {
                aborted = true;
                return scan;
            }
            if (progress)
                progress->bytesDone = bytesBefore + bytesInFile;
        }

        std::string_view rest = line;
        const std::string_view keyword = NextToken(rest);
        if (!keyword.starts_with(kManipPrefix))
            continue;
        const std::string type(keyword.substr(kManipPrefix.size()));
        const ManipTypeDef* def = types.Find(type);
        if (!def) {
            if (!types.ignored.contains(type))
                ++scan.unsupported[type];
            continue;
        }

        std::vector<std::string_view> params;
        for (int param = 0; param < def->params; ++param) {
            const std::string_view token = NextToken(rest);
            if (token.empty())
                break;
            params.push_back(token);
        }
        if (static_cast<int>(params.size()) < def->params) {
            if (++problemCount <= kMaxProblems)
                scan.problems.push_back(std::format("{} line {}: {} needs {} values", util::Utf8(object.path.filename()),
                                                    lineNumber, keyword, def->params));
            continue;
        }

        ObjManip manip;
        manip.type = def->type;
        for (int ref : def->refs)
            manip.refs.emplace_back(params[static_cast<std::size_t>(ref)]);
        manip.tooltip  = std::string(Trim(rest));
        manip.category = def->category;
        for (std::string_view param : params)
            manip.params.emplace_back(param);
        if (!manip.tooltip.empty())
            manip.tooltips.push_back(manip.tooltip);

        const std::string key = manip.Key();
        if (const auto known = byKey.find(key); known != byKey.end()) {
            ObjManip& same = scan.manips[known->second];
            ++same.clickAreas;
            if (!manip.tooltip.empty() &&
                std::find(same.tooltips.begin(), same.tooltips.end(), manip.tooltip) == same.tooltips.end())
                same.tooltips.push_back(manip.tooltip);
            if (same.tooltip.empty() && !manip.tooltip.empty()) {
                same.tooltip = manip.tooltip;
                same.body = FillTemplate(*def, types, same);
            }
            continue;
        }
        manip.objFile    = object.path;
        manip.lineNumber = lineNumber;
        manip.clickAreas = 1;
        manip.body       = FillTemplate(*def, types, manip);
        byKey.emplace(key, scan.manips.size());
        ++scan.controls[manip.type];
        scan.manips.push_back(std::move(manip));
    }
    if (problemCount > kMaxProblems)
        scan.problems.push_back(std::format("{}: ... and {} more", util::Utf8(object.path.filename()),
                                            problemCount - kMaxProblems));
    bytesBefore += bytesInFile;
    return scan;
}

std::string LowerPath(const fs::path& path)
{
    return Lower(path.generic_string());
}

} // namespace

std::vector<AcfObject> ReadAcfObjects(const fs::path& acfFile, std::string& error)
{
    std::vector<AcfObject> objects;
    std::ifstream in(acfFile, std::ios::binary);
    if (!in) {
        error = std::format("cannot read {}", util::Utf8(acfFile));
        return objects;
    }
    constexpr std::string_view kPrefix = "P _obja/";
    constexpr std::string_view kKey    = "/_v10_att_file_stl";
    const fs::path objectsFolder = acfFile.parent_path() / "objects";

    std::vector<std::pair<int, std::string>> listed;    // index, path as written
    std::string line;
    while (std::getline(in, line)) {
        std::string_view view = line;
        if (!view.starts_with(kPrefix))
            continue;
        view.remove_prefix(kPrefix.size());
        const std::size_t keyAt = view.find(kKey);
        if (keyAt == std::string_view::npos)
            continue;
        int index = 0;
        const auto [ptr, ec] = std::from_chars(view.data(), view.data() + keyAt, index);
        if (ec != std::errc() || ptr != view.data() + keyAt)
            continue;
        const std::string_view value = Trim(view.substr(keyAt + kKey.size()));
        if (!value.empty())
            listed.emplace_back(index, std::string(value));
    }
    std::stable_sort(listed.begin(), listed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    std::set<std::string> seen;                         // lower-case normalized paths
    for (const auto& [index, written] : listed) {
        std::string slashes = written;
        std::replace(slashes.begin(), slashes.end(), '\\', '/');
        AcfObject object;
        object.path     = (objectsFolder / util::PathFromUtf8(slashes)).lexically_normal();
        object.listedAs = written;
        std::error_code ec;
        object.exists   = fs::is_regular_file(object.path, ec);
        if (seen.insert(LowerPath(object.path)).second)
            objects.push_back(std::move(object));
    }

    // X-Plane 10/11: the cockpit object is found by its name, not listed.
    AcfObject cockpit;
    cockpit.path     = (acfFile.parent_path() / (acfFile.stem().u8string() + u8"_cockpit.obj")).lexically_normal();
    cockpit.listedAs = util::Utf8(cockpit.path.filename());
    cockpit.byName   = true;
    std::error_code ec;
    cockpit.exists   = fs::is_regular_file(cockpit.path, ec);
    if (cockpit.exists && !seen.contains(LowerPath(cockpit.path)))
        objects.insert(objects.begin(), std::move(cockpit));
    return objects;
}

ObjScanResult CombineScans(const std::vector<const ObjFileScan*>& files, const ManipTypeSet& types)
{
    ObjScanResult result;
    std::map<std::string, std::size_t> byKey;
    for (const ManipTypeDef& def : types.types)
        result.controls[def.type] = 0;
    for (const ObjFileScan* file : files) {
        if (!file->object.exists)
            continue;
        result.files.push_back(file->object.path);
        result.stamps.push_back(file->stamp);
        for (const auto& [type, lines] : file->unsupported)
            result.unsupported[type] += lines;
        result.problems.insert(result.problems.end(), file->problems.begin(), file->problems.end());
        for (const ObjManip& manip : file->manips) {
            const std::string key = manip.Key();
            if (const auto known = byKey.find(key); known != byKey.end()) {
                ObjManip& same = result.manips[known->second];
                same.clickAreas += manip.clickAreas;
                for (const std::string& tooltip : manip.tooltips) {
                    if (std::find(same.tooltips.begin(), same.tooltips.end(), tooltip) == same.tooltips.end())
                        same.tooltips.push_back(tooltip);
                }
                if (same.tooltip.empty() && !manip.tooltip.empty()) {
                    same.tooltip = manip.tooltip;
                    if (const ManipTypeDef* def = types.Find(same.type))
                        same.body = FillTemplate(*def, types, same);
                }
                continue;
            }
            byKey.emplace(key, result.manips.size());
            ++result.controls[manip.type];
            result.manips.push_back(manip);
        }
    }
    return result;
}

ObjScanResult ScanObjFiles(const std::vector<fs::path>& files, const ManipTypeSet& types,
                           util::StopToken stop, ScanProgress* progress)
{
    std::vector<AcfObject> objects;
    for (const fs::path& file : files) {
        AcfObject object;
        object.path = file;
        object.listedAs = util::Utf8(file.filename());
        std::error_code ec;
        object.exists = fs::is_regular_file(file, ec);
        objects.push_back(object);
    }
    AircraftScan scan;
    if (progress) {
        std::uintmax_t total = 0;
        for (const FileStamp& stamp : StampFiles(files))
            total += stamp.size;
        progress->filesTotal = files.size();
        progress->filesDone  = 0;
        progress->bytesTotal = total;
        progress->bytesDone  = 0;
    }
    std::uintmax_t bytesBefore = 0;
    bool aborted = false;
    for (std::size_t i = 0; i < objects.size() && !aborted; ++i) {
        scan.files.push_back(ScanOneFile(objects[i], types, stop, progress, bytesBefore, aborted));
        if (progress) {
            progress->bytesDone = bytesBefore;
            progress->filesDone = i + 1;
        }
    }
    std::vector<const ObjFileScan*> all;
    for (const ObjFileScan& file : scan.files)
        all.push_back(&file);
    ObjScanResult result = CombineScans(all, types);
    for (const AcfObject& object : objects) {
        if (!object.exists)
            result.problems.push_back(std::format("cannot read {}", util::Utf8(object.path.filename())));
    }
    result.aborted = aborted;
    return result;
}

AircraftScan ScanAircraft(const fs::path& acfFile, const ManipTypeSet& types, util::StopToken stop,
                          ScanProgress* progress)
{
    AircraftScan scan;
    scan.acfFile = acfFile;
    const std::vector<AcfObject> objects = ReadAcfObjects(acfFile, scan.error);
    if (progress) {
        std::uintmax_t total = 0;
        for (const AcfObject& object : objects) {
            std::error_code ec;
            if (object.exists)
                total += fs::file_size(object.path, ec);
        }
        progress->filesTotal = objects.size();
        progress->filesDone  = 0;
        progress->bytesTotal = total;
        progress->bytesDone  = 0;
    }
    std::uintmax_t bytesBefore = 0;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        bool aborted = stop.stop_requested();
        if (!aborted)
            scan.files.push_back(ScanOneFile(objects[i], types, stop, progress, bytesBefore, aborted));
        if (aborted) {
            scan.aborted = true;
            return scan;
        }
        if (progress) {
            progress->bytesDone = bytesBefore;
            progress->filesDone = i + 1;
        }
    }
    return scan;
}

} // namespace hpvr::manip

// hpvr - manipulators of the aircraft's OBJ files (OBJ8 ATTR_manip_* directives)
//
// Which types are read, where their commands / datarefs are and what their
// vrconfig block contains comes from the type set (manip_types.ini). A
// manipulator is identified by its type and its commands / datarefs, exactly
// as BEGIN_MANIP identifies it. One control often has several click areas
// (several ATTR_manip_ lines with the same identity); they are merged.
//
// The scan functions only read files: no X-Plane calls and no logging, so they
// can run in a background thread (ObjScanner). Messages are returned instead.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include "util/StopThread.h"
#include <string>
#include <string_view>
#include <vector>

#include "manip/ManipTypes.h"

namespace hpvr::manip {

// One control found in the OBJ files (click areas merged).
struct ObjManip {
    std::string              type;          // e.g. "command_knob"
    std::vector<std::string> refs;          // commands / datarefs (identity)
    std::string              tooltip;       // first non-empty tooltip of its click areas
    std::vector<std::string> tooltips;      // every different non-empty tooltip (pilot / copilot yoke)
    std::vector<std::string> params;        // the OBJ parameters of its first click area (params[0] = cursor)
    std::filesystem::path    objFile;       // first file it was found in
    std::size_t              lineNumber = 0;   // first line (1-based)
    std::size_t              clickAreas = 0;
    ManipCategory            category = ManipCategory::Knob;
    std::vector<std::string> body;          // block content (template filled in), without indent

    // Identity as in a BEGIN_MANIP line: "<type> <ref1> [<ref2>]".
    std::string Key() const;
    // Name for people: the tooltip, else the last part of the first command / dataref.
    std::string DisplayName() const;
};

// The group of a manipulator: its first command / dataref without the last
// part ("sim/autopilot/altitude_up" -> "sim/autopilot"). Empty when there is
// no '/' (or no reference).
std::string ManipGroup(const std::vector<std::string>& refs);

// "# --- <group> ---": the comment the Extract page writes before each group.
std::string GroupComment(const std::string& group);
bool        IsGroupComment(std::string_view line);

// Identity of a BEGIN_MANIP line, comparable with ObjManip::Key().
std::string ManipKey(const std::string& type, const std::vector<std::string>& refs);

// One object listed in the aircraft's .acf ("P _obja/<n>/_v10_att_file_stl <path>",
// relative to the aircraft's objects/ folder), or its cockpit object found by
// the name "<acf name>_cockpit.obj" (X-Plane 10/11 aircraft do not list it).
struct AcfObject {
    std::filesystem::path path;           // resolved, normalized
    std::string           listedAs;       // as written in the .acf (or the file name)
    bool                  exists = false;
    bool                  byName = false; // <acf name>_cockpit.obj, not in the list
};

// The objects of `acfFile` in .acf order (the cockpit object by name first when
// it is not listed). Duplicates removed. Empty + `error` when the file cannot be read.
std::vector<AcfObject> ReadAcfObjects(const std::filesystem::path& acfFile, std::string& error);

// A file's size and time when it was read: Extract re-reads changed files.
struct FileStamp {
    std::filesystem::path           path;
    std::uintmax_t                  size = 0;
    std::filesystem::file_time_type time{};
};
std::vector<FileStamp> StampFiles(const std::vector<std::filesystem::path>& files);
bool StampsChanged(const std::vector<FileStamp>& stamps);

// Progress of a scan, shared with the drawing thread.
struct ScanProgress {
    std::atomic<std::size_t>   filesTotal{0};
    std::atomic<std::size_t>   filesDone{0};
    std::atomic<std::uintmax_t> bytesTotal{0};
    std::atomic<std::uintmax_t> bytesDone{0};
};

// One OBJ file's manipulators (click areas merged within the file).
struct ObjFileScan {
    AcfObject                          object;
    FileStamp                          stamp;
    std::vector<ObjManip>              manips;
    std::map<std::string, std::size_t> controls;       // defined type -> controls in this file
    std::map<std::string, std::size_t> unsupported;    // neither defined nor ignored -> lines
    std::vector<std::string>           problems;

    std::size_t ControlCount() const { return manips.size(); }
};

// Every object of an aircraft, read.
struct AircraftScan {
    std::filesystem::path    acfFile;
    std::vector<ObjFileScan> files;      // .acf order
    std::string              error;      // .acf unreadable
    bool                     aborted = false;
};

struct ObjScanResult {
    std::vector<std::filesystem::path> files;    // matching the filters
    std::vector<FileStamp>     stamps;
    std::vector<ObjManip>      manips;           // every defined type, in file / line order
    std::map<std::string, std::size_t> controls; // defined type -> controls found
    std::map<std::string, std::size_t> unsupported;  // neither defined nor ignored -> lines
    std::vector<std::string>   problems;         // unreadable files, incomplete lines
    bool                       aborted = false;
};

// Reads the ATTR_manip_ lines of `files`, merging click areas of the same control.
ObjScanResult ScanObjFiles(const std::vector<std::filesystem::path>& files, const ManipTypeSet& types,
                           util::StopToken stop = {}, ScanProgress* progress = nullptr);

// Reads the .acf object list, then every listed OBJ file (missing ones are kept, empty).
AircraftScan ScanAircraft(const std::filesystem::path& acfFile, const ManipTypeSet& types,
                          util::StopToken stop = {}, ScanProgress* progress = nullptr);

// The manipulators of the chosen files together (same control in two files = one entry).
ObjScanResult CombineScans(const std::vector<const ObjFileScan*>& files, const ManipTypeSet& types);

} // namespace hpvr::manip

// vrconfig_helper - <aircraft>_vrconfig.txt reader / writer
//
// Keeps the file as a list of lines. Only the lines of an edited teleport
// spot are rewritten; comments, layout, manipulator blocks and directives this
// plugin does not handle are written back exactly as read.
//
// Teleport spot block (X-Plane 11.x / 12, file header "A" / "1100" / "VRCONFIG"):
//   BEGIN_TELEPORT_HOTSPOT <SITTING|STANDING> <name: rest of the line>
//      AABB       <min x> <min y> <min z> <max x> <max y> <max z>   (teleport box)
//      PRESET_XYZ <x> <y> <z>                                     (position)
//      PRESET_PSI <deg>  heading 0..360    (0 forward, 90 right wing)
//      PRESET_THE <deg>  pitch  -90..90
//      PRESET_PHI <deg>  roll  -180..180
//   END_TELEPORT_HOTSPOT
// Manipulator blocks (BEGIN_MANIP ... END_MANIP) are read for their type and
// commands / datarefs only; their content is replaced or appended as whole lines.
// Units: meters, +X right, +Y up, +Z aft, relative to the default CG, or to the
// REF_POINT_ACF origin when that directive is present (X-Plane 12).

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hpvr::vrconfig {

// Aircraft coordinates: x = right, y = up, z = aft (the axis names of the vrconfig format).
struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

enum class SpotKind {
    Sitting,    // position = the user's head
    Standing,   // position = the user's feet
};

struct TeleportSpot {
    SpotKind    kind = SpotKind::Sitting;
    std::string name;
    Vec3        boxMin, boxMax;     // where the teleport beam selects this spot
    Vec3        position;
    double      heading = 0.0;
    double      pitch   = 0.0;
    double      roll    = 0.0;
};

const char* KindKeyword(SpotKind kind);   // "SITTING" / "STANDING"

// Comparisons at the precision written to the file (4 decimals).
bool SamePosition(const TeleportSpot& first, const TeleportSpot& second);   // position + box
bool SameHeading (const TeleportSpot& first, const TeleportSpot& second);
bool SameTilt    (const TeleportSpot& first, const TeleportSpot& second);   // pitch + roll
bool SameName    (const TeleportSpot& first, const TeleportSpot& second);
bool SameSpot    (const TeleportSpot& first, const TeleportSpot& second);

// One parsed block and where its lines are.
struct SpotRecord {
    static constexpr std::size_t kNoLine = static_cast<std::size_t>(-1);

    TeleportSpot spot;
    bool         editable = false;     // block complete and every value readable
    std::string  problem;              // why not editable
    // The only problem is commas between the numbers ("0.39, 0.64, 0.58"): the
    // values in `spot` are read as if they were spaces; FixCommas removes them.
    bool         commasOnly = false;
    std::size_t  lineNumber = 0;       // 1-based line of BEGIN_TELEPORT_HOTSPOT

    // 0-based indexes into the file's lines
    std::size_t beginIdx = kNoLine, boxIdx = kNoLine, positionIdx = kNoLine,
                headingIdx = kNoLine, pitchIdx = kNoLine, rollIdx = kNoLine,
                endIdx = kNoLine;      // END_TELEPORT_HOTSPOT
};

// One manipulator block (BEGIN_MANIP <type> <cmnd/dref 1> [<cmnd/dref 2>] [tooltip] ... END_MANIP).
// It changes how an existing OBJ manipulator behaves with VR controllers.
// The number of commands / datarefs depends on the type (ManipRefCount); the
// rest of the line is the tooltip, which tells apart manipulators with the same
// type and commands (e.g. the pilot's and copilot's yoke).
struct ManipRecord {
    static constexpr std::size_t kNoLine = static_cast<std::size_t>(-1);

    std::string              type;          // OBJ type without "ATTR_manip_", e.g. "command_knob"
    std::vector<std::string> refs;          // commands / datarefs; a "CMND=" / "DREF=" prefix is removed
    std::vector<std::string> rawRefs;       // as written
    std::string              tooltip;       // rest of the line after the refs (may be empty)
    std::size_t              lineNumber = 0;          // 1-based line of BEGIN_MANIP
    std::size_t              beginIdx = kNoLine;      // 0-based
    std::size_t              endIdx   = kNoLine;      // END_MANIP; kNoLine when missing
};

// The command / dataref as written in a BEGIN_MANIP line, without "CMND=" / "DREF=".
std::string ManipRefName(std::string_view token);

// How many commands / datarefs a BEGIN_MANIP line of `type` has. 0 for a type
// the rules don't know: then every word of the line counts as a command / dataref.
// The numbers come from manip_types.ini (each type's refs): SetManipRefCounts.
std::size_t ManipRefCount(std::string_view type);
void SetManipRefCounts(std::map<std::string, std::size_t, std::less<>> counts);

// Is `keyword` a line that belongs inside a BEGIN_MANIP block? ACTION and the
// settings of manip_actions.ini (SetManipDirectives).
bool IsManipDirective(std::string_view keyword);
void SetManipDirectives(std::set<std::string, std::less<>> keywords);

class VrConfigFile {
public:
    // Reads and parses the file. On failure LastError() says why.
    bool Load(const std::filesystem::path& path);

    // Starts an empty file in memory (header only); written by Save().
    void CreateNew(const std::filesystem::path& path);

    const std::filesystem::path&    FilePath()  const { return mFilePath; }
    const std::vector<SpotRecord>&  Spots()     const { return mSpots; }
    const std::vector<ManipRecord>& Manips()    const { return mManips; }
    const std::string&              LastError() const { return mLastError; }
    const std::vector<std::string>& Notes()     const { return mNotes; }   // non-fatal parse remarks
    // The remarks about manipulator blocks only (also in Notes()).
    const std::vector<std::string>& ManipNotes() const { return mManipNotes; }
    const std::vector<std::string>& Lines()     const { return mLines; }

    // REF_POINT_ACF <y> [<z>]: the file's origin relative to the aircraft's
    // authoring point (meters). Empty when the file uses the default CG.
    const std::optional<Vec3>& ReferencePoint() const { return mReferencePoint; }

    // Replaces spot `index` in the in-memory lines. False for an unknown index
    // or a block that is not editable.
    bool UpdateSpot(std::size_t index, const TeleportSpot& spot);

    // Removes the block of spot `index` (BEGIN..END lines). Indexes of later
    // spots shift down by one. False when the block has no END line.
    bool RemoveSpot(std::size_t index);

    // Removes the commas between the numbers of spot `index` (spot.commasOnly):
    // each value line with commas is rewritten with single spaces, the rest of
    // the file stays as it is. True when the spot is editable afterwards.
    bool FixCommas(std::size_t index);

    // Inserts a new block after the last teleport spot (or after the header).
    // Returns the new spot's index.
    std::size_t AppendSpot(const TeleportSpot& spot);

    // Replaces the lines BEGIN_MANIP..END_MANIP of manipulator `index` with
    // `blockLines` (comment lines above the block stay). False when the block
    // has no END_MANIP.
    bool ReplaceManip(std::size_t index, const std::vector<std::string>& blockLines);

    // Removes the lines BEGIN_MANIP..END_MANIP of manipulator `index` (and the
    // "extracted by VRConfig Helper" comment written above it by the Extract
    // page). False when the block has no END_MANIP.
    bool RemoveManip(std::size_t index);

    // The lines BEGIN_MANIP..END_MANIP of manipulator `index` (empty when it has no END_MANIP).
    std::vector<std::string> ManipLines(std::size_t index) const;

    // Adds lines at the end of the file, after one blank line.
    void AppendBlock(const std::vector<std::string>& lines);

    // Writes the lines to `target` (default: the loaded file) through a
    // temporary file and a rename, so the target is never half written.
    // Adds the plugin's signature line after the header first (see AddSignature).
    // After a successful save, a copy goes to <file>.last_save (replaced each time).
    bool Save(const std::filesystem::path& target = {});

    // Adds "# Made with VRConfig Helper - by Snagar.Dev" (file created by the plugin)
    // or "# Modified with ..." (existing file) right after the A / 1100 / VRCONFIG
    // header. Nothing when the file already has a signature line or has no valid header.
    // True when a line was added.
    bool AddSignature();
    bool HasSignature() const;

    // The whole file as text, with the original line endings.
    std::string Text() const;

private:
    void ParseLines();
    void ParseManips();

    std::filesystem::path    mFilePath;
    std::vector<std::string> mLines;            // without line endings
    std::string              mLineEnd = "\n";   // "\r\n" if the file used it
    bool                     mEndsWithLineEnd = true;
    bool                     mCreatedNew = false;   // CreateNew(), not yet loaded from disk
    std::size_t              mHeaderEnd = 0;        // first line after a valid header, 0 = no valid header
    std::vector<SpotRecord>  mSpots;
    std::vector<ManipRecord> mManips;
    std::optional<Vec3>      mReferencePoint;
    std::string              mLastError;
    std::vector<std::string> mNotes;
    std::vector<std::string> mManipNotes;
};

// <acf stem>_vrconfig.txt next to `acfFile`; on case-sensitive file systems a
// file whose name differs only in letter case is used. The path may not exist.
std::filesystem::path VrConfigPathFor(const std::filesystem::path& acfFile);

// Number format used for every value written: 4 decimals, '.' separator,
// independent of the process locale.
std::string FormatNumber(double value);

} // namespace hpvr::vrconfig

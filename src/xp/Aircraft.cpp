// vrconfig_helper - user aircraft and pilot head position (X-Plane side)

#include "Aircraft.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "XPLMDataAccess.h"
#include "XPLMPlanes.h"
#include "XPLMUtilities.h"

#include "Log.h"
#include "util/FileUtil.h"

namespace hpvr::xp {

namespace fs = std::filesystem;

namespace {

constexpr double kFeetToMeters = 0.3048;

struct DataRefs {
    XPLMDataRef headX = nullptr, headY = nullptr, headZ = nullptr;
    XPLMDataRef headPsi = nullptr, headThe = nullptr, headPhi = nullptr;
    XPLMDataRef teleportOutside = nullptr;
    XPLMDataRef refPointY = nullptr, refPointZ = nullptr;
    XPLMDataRef uiName = nullptr;
    XPLMDataRef viewIsExternal = nullptr;
    XPLMDataRef vrEnabled = nullptr;
    bool        looked = false;
};

DataRefs& Refs()
{
    static DataRefs refs;
    if (!refs.looked) {
        refs.looked          = true;
        refs.headX           = XPLMFindDataRef("sim/graphics/view/pilots_head_x");
        refs.headY           = XPLMFindDataRef("sim/graphics/view/pilots_head_y");
        refs.headZ           = XPLMFindDataRef("sim/graphics/view/pilots_head_z");
        refs.headPsi         = XPLMFindDataRef("sim/graphics/view/pilots_head_psi");
        refs.headThe         = XPLMFindDataRef("sim/graphics/view/pilots_head_the");
        refs.headPhi         = XPLMFindDataRef("sim/graphics/view/pilots_head_phi");
        refs.teleportOutside = XPLMFindDataRef("sim/graphics/VR/teleport_on_ground");
        refs.refPointY       = XPLMFindDataRef("sim/aircraft/weight/acf_cgY_original");
        refs.refPointZ       = XPLMFindDataRef("sim/aircraft/weight/acf_cgZ_original");
        refs.uiName          = XPLMFindDataRef("sim/aircraft/view/acf_ui_name");
        refs.viewIsExternal  = XPLMFindDataRef("sim/graphics/view/view_is_external");
        refs.vrEnabled       = XPLMFindDataRef("sim/graphics/VR/enabled");
    }
    return refs;
}


} // namespace

fs::path GetAircraftFolder()
{
    char root[2048] = {};
    XPLMGetSystemPath(root);                          // native path, ends with a separator
    return util::PathFromUtf8(root) / "Aircraft";
}

std::optional<UserAircraft> GetUserAircraft()
{
    char fileName[256] = {};
    char path[512]     = {};
    XPLMGetNthAircraftModel(0, fileName, path);
    if (path[0] == '\0') {
        LOG_WARN("X-Plane returned no path for the user aircraft");
        return std::nullopt;
    }

    UserAircraft aircraft;
    aircraft.acfFile      = util::PathFromUtf8(path);
    aircraft.vrconfigFile = vrconfig::VrConfigPathFor(aircraft.acfFile);

    char ui[251] = {};
    if (Refs().uiName)
        XPLMGetDatab(Refs().uiName, ui, 0, 250);
    aircraft.displayName = ui[0] ? std::string(ui) : util::Utf8(aircraft.acfFile.stem());
    return aircraft;
}

bool IsTeleportedOutside()
{
    return Refs().teleportOutside && XPLMGetDatai(Refs().teleportOutside) != 0;
}

bool IsInVR()
{
    const DataRefs& refs = Refs();
    return refs.vrEnabled && XPLMGetDatai(refs.vrEnabled) != 0;
}

bool IsInCockpitView()
{
    return !Refs().viewIsExternal || XPLMGetDatai(Refs().viewIsExternal) == 0;
}

namespace {

// Offset that turns a head position (relative to the CG) into a position in
// the vrconfig file's coordinates:  file = head + offset,  head = file - offset.
// Zero unless the file uses REF_POINT_ACF, whose origin is a point given
// relative to the aircraft's Plane-Maker reference point instead of the CG:
//   head (from the authoring point) = original CG (from the authoring point) + head (from the CG)
//   file coordinate                 = head (from the authoring point) - REF_POINT_ACF
std::optional<vrconfig::Vec3> FileOffset(const vrconfig::VrConfigFile& file, std::string& reason)
{
    vrconfig::Vec3 offset;
    if (const auto& ref = file.ReferencePoint()) {
        const DataRefs& refs = Refs();
        if (!refs.refPointY || !refs.refPointZ) {
            reason = "This file uses REF_POINT_ACF, and X-Plane does not provide the aircraft reference point";
            return std::nullopt;
        }
        const double cgY = XPLMGetDataf(refs.refPointY) * kFeetToMeters;
        const double cgZ = XPLMGetDataf(refs.refPointZ) * kFeetToMeters;
        offset.y = cgY - ref->y;
        offset.z = cgZ - ref->z;
        LOG_DEBUG("REF_POINT_ACF y={} z={}, original CG y={:.4f} z={:.4f} m -> offset y={:.4f} z={:.4f}",
                  ref->y, ref->z, cgY, cgZ, offset.y, offset.z);
    }
    return offset;
}

} // namespace

std::optional<HeadPose> GetHeadPose(const vrconfig::VrConfigFile& file, std::string& reason)
{
    const DataRefs& refs = Refs();
    if (!refs.headX || !refs.headY || !refs.headZ || !refs.headPsi) {
        reason = "X-Plane does not provide the pilot head position";
        return std::nullopt;
    }
    if (IsTeleportedOutside()) {
        reason = "You are teleported outside the aircraft";
        return std::nullopt;
    }
    const auto offset = FileOffset(file, reason);
    if (!offset)
        return std::nullopt;

    HeadPose pose;
    pose.position = {XPLMGetDataf(refs.headX) + offset->x,
                     XPLMGetDataf(refs.headY) + offset->y,
                     XPLMGetDataf(refs.headZ) + offset->z};
    pose.heading  = XPLMGetDataf(refs.headPsi);
    pose.pitch    = refs.headThe ? XPLMGetDataf(refs.headThe) : 0.0;
    pose.roll     = refs.headPhi ? XPLMGetDataf(refs.headPhi) : 0.0;

    LOG_DEBUG("head (file coordinates) x={:.4f} y={:.4f} z={:.4f} heading={:.1f}",
              pose.position.x, pose.position.y, pose.position.z, pose.heading);
    return pose;
}

std::optional<double> GetCameraHeading()
{
    const DataRefs& refs = Refs();
    if (!refs.headPsi || !IsInCockpitView() || IsTeleportedOutside())
        return std::nullopt;
    return XPLMGetDataf(refs.headPsi);
}

bool MoveHeadTo(const vrconfig::VrConfigFile& file, const vrconfig::Vec3& position, double heading,
                double pitch, double roll, std::string& reason)
{
    const DataRefs& refs = Refs();
    if (!refs.headX || !refs.headY || !refs.headZ || !refs.headPsi ||
        !XPLMCanWriteDataRef(refs.headX) || !XPLMCanWriteDataRef(refs.headPsi)) {
        reason = "X-Plane does not let plugins move the pilot head";
        return false;
    }
    if (!IsInCockpitView()) {
        reason = "the view is outside the cockpit";
        return false;
    }
    if (IsTeleportedOutside()) {
        reason = "you are teleported outside the aircraft";
        return false;
    }
    const auto offset = FileOffset(file, reason);
    if (!offset)
        return false;

    XPLMSetDataf(refs.headX, static_cast<float>(position.x - offset->x));
    XPLMSetDataf(refs.headY, static_cast<float>(position.y - offset->y));
    XPLMSetDataf(refs.headZ, static_cast<float>(position.z - offset->z));
    XPLMSetDataf(refs.headPsi, static_cast<float>(heading));
    if (refs.headThe && XPLMCanWriteDataRef(refs.headThe))
        XPLMSetDataf(refs.headThe, static_cast<float>(pitch));
    if (refs.headPhi && XPLMCanWriteDataRef(refs.headPhi))
        XPLMSetDataf(refs.headPhi, static_cast<float>(roll));
    LOG_DEBUG("camera moved to x={:.4f} y={:.4f} z={:.4f} heading={:.1f} pitch={:.1f} roll={:.1f} (file coordinates)",
              position.x, position.y, position.z, heading, pitch, roll);
    return true;
}

} // namespace hpvr::xp

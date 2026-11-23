// vrconfig_helper - user aircraft and pilot head position (X-Plane side)
//
// Datarefs (Laminar DataRefs.txt):
//   sim/graphics/view/pilots_head_x/y/z      meters, pilot's head relative to the CG
//   sim/graphics/view/pilots_head_psi/the/phi degrees, head heading / pitch / roll
//   sim/graphics/VR/teleport_on_ground       user teleported outside: head position not reliable
//   sim/graphics/view/view_is_external        0 = in the cockpit
//   sim/aircraft/weight/acf_cgY/Z_original    feet, original reference point in Plane-Maker
//   sim/aircraft/view/acf_ui_name             aircraft name as shown in X-Plane

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "vrconfig/VrConfigFile.h"

namespace hpvr::xp {

struct UserAircraft {
    std::filesystem::path acfFile;        // full path of the .acf
    std::string           displayName;    // acf_ui_name, or the .acf file name
    std::filesystem::path vrconfigFile;   // <acf name>_vrconfig.txt next to the .acf (may not exist)
};

// The user's aircraft (index 0). Empty if X-Plane reports no aircraft path.
std::optional<UserAircraft> GetUserAircraft();

// <X-Plane folder>/Aircraft.
std::filesystem::path GetAircraftFolder();

struct HeadPose {
    vrconfig::Vec3 position;              // in the vrconfig file's coordinates
    double         heading = 0.0;
    double         pitch   = 0.0;
    double         roll    = 0.0;
};

// Where the pilot's head is, converted to the coordinates of `file`
// (default CG, or the REF_POINT_ACF origin). Empty with a reason when the
// position cannot be used (teleported outside, datarefs missing).
std::optional<HeadPose> GetHeadPose(const vrconfig::VrConfigFile& file, std::string& reason);

bool IsTeleportedOutside();
bool IsInCockpitView();
// True while X-Plane shows the world in a VR headset (sim/graphics/VR/enabled).
bool IsInVR();

// Heading the camera looks at (degrees, 0 = nose, 90 = right wing), while in
// the cockpit and not teleported outside; empty otherwise.
std::optional<double> GetCameraHeading();

// Live view / Go to: puts the camera (pilot head) at `position` with `heading`,
// `pitch` and `roll`, given in the coordinates of `file`. Only inside the cockpit
// and not while teleported outside; otherwise returns false with a reason.
bool MoveHeadTo(const vrconfig::VrConfigFile& file, const vrconfig::Vec3& position, double heading,
                double pitch, double roll, std::string& reason);

} // namespace hpvr::xp

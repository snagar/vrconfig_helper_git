// hpvr - information about the plugin, shared by all its files
//
// The one place to change the version, the plugin's name and description, the About
// texts, the log file limits and the window size. Nothing here comes from CMake.
//
// Not here on purpose:
//   - the plugin id "vrconfig_helper" (vrconfig_helper.cpp: kPluginId), because it
//     must match the CMake project name; see the note next to project() in
//     CMakeLists.txt;
//   - values stored in users' files or settings, which must never change: the command
//     vrconfig_helper/toggle_main_window, the vrconfig signature and "extracted by"
//     comment, the file and backup names, the rule file names, the custom_ prefix
//     and the rule file versions. They stay next to the code that uses them.
//
// All texts are string literals, so .data() is a valid C string for the X-Plane API.

#pragma once

#include <cstdint>
#include <string_view>

namespace hpvr::info {

// --- The plugin ----------------------------------------------------------------------
inline constexpr std::string_view kVersion     = "1.1";     // change before a release; the
                                                            // GitHub workflow reads it here
inline constexpr std::string_view kPluginName  = "VRConfig Helper";
inline constexpr std::string_view kPluginDesc  = "VR configuration helper: teleport hotspots and manipulators";
inline constexpr std::string_view kMenuToggle  = "Toggle Window";   // the item in Plugins > VRConfig Helper

// --- About box -----------------------------------------------------------------------
inline constexpr std::string_view kDesignedBy  = "Snagar.Dev";
inline constexpr std::string_view kCodedBy     = "Claude.Ai";
inline constexpr std::string_view kLicense     = "Free";
inline constexpr std::string_view kInspiredBy  = "Inspired by the \"VR Tools\" plugin, by jrKok.";
inline constexpr std::string_view kIconCredit  = "Icons: Font Awesome Free 5 (fa-solid-900) by Fonticons, Inc. - "
                                                 "icons CC BY 4.0, font SIL OFL 1.1 (fontawesome.com).";
inline constexpr std::string_view kLibraryCredit = "Built with Dear ImGui by Omar Cornut (MIT licence) and ImgWindow "
                                                   "by Christopher Collins, extended by Steven L. Goldberg (slgoldberg) "
                                                   "(BSD 3-clause licence).";

// --- Log file --------------------------------------------------------------------------
inline constexpr bool           kLogToXPlaneDefault = true;                 // also copy each line to X-Plane's Log.txt
inline constexpr std::uintmax_t kLogMaxFileSize     = 5u * 1024u * 1024u;   // a new file is started past this size
inline constexpr int            kLogKeptCopies      = 2;                    // <plugin>.log.1 .. .2

// --- Window ----------------------------------------------------------------------------
inline constexpr int kWindowWidth  = 520;     // fixed size for every page
inline constexpr int kWindowHeight = 730;

} // namespace hpvr::info

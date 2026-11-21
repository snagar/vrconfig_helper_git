# -----------------------------------------------------------------------------
#  vrconfig_helper - X-Plane plugin (Dear ImGui via ImgWindow)
#
#  Layout:
#     CMakeLists.txt
#     src/                 plugin sources
#     libs/xpsdk/          X-Plane SDK 4.x (CHeaders/XPLM, CHeaders/Widgets, ...)
#     libs/imgui/          Dear ImGui sources (imgui.cpp, imgui.h, ...)
#     libs/ImgWindow/      ImgWindow (kept whole: ImgWindow.*, ImgFontAtlas.*, SystemGL.h, contrib/)
#     libs/loguru/         loguru logging library (loguru.hpp / loguru.cpp)
#     libs/fonts/          IconsFontAwesome5.h + fa-solid-900.inc
#     (libs/fetch_libs.sh downloads xpsdk, imgui and ImgWindow)
#
#  Build (Linux):
#     cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#     cmake --build build -j
#  Result:
#     build/vrconfig_helper/lin_x64/vrconfig_helper.xpl
#  Install into X-Plane:
#     cmake --install build --prefix "<X-Plane>/Resources/plugins"
# -----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)

project(vrconfig_helper
    VERSION 0.9.0
    DESCRIPTION "VRConfig Helper - X-Plane plugin"
    LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This CMakeLists.txt currently targets Linux only.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)                    # std::format
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)       # everything ends up in a shared object
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# --- AddressSanitizer (crash hunting) ---------------------------------------------
# TEMPORARY: finds memory errors (writes past a buffer, use after free) in the
# plugin and in the libraries built here (ImGui, ImgWindow, loguru).
# Applies to Debug builds only; Release builds are never affected.
#
# >>> To ENABLE: change OFF to ON on the option line, or pass -DVRCH_ASAN=ON
# >>> (CLion: Settings > Build, Execution, Deployment > CMake > CMake options),
# >>> then reload CMake and rebuild. Delete this whole block when done.
#
# The plugin then only loads when X-Plane is started with the sanitizer runtime:
#   LD_PRELOAD=$(gcc -print-file-name=libasan.so)
#   ASAN_OPTIONS=detect_leaks=0:protect_shadow_gap=0:log_path=/tmp/xp_asan
# (protect_shadow_gap=0 is needed with NVIDIA drivers.) A report is written to
# /tmp/xp_asan.<pid>. Without LD_PRELOAD X-Plane fails to load the plugin.
# NOTE: X-Plane 12 itself aborts at start-up under LD_PRELOAD=libasan.so
# ("munmap_chunk(): invalid pointer", before any plugin loads), so this is only
# usable with the test hosts, not inside X-Plane. Off by default.
#option(VRCH_ASAN "Debug builds: build with AddressSanitizer (crash hunting)" OFF)
#if(VRCH_ASAN)
#    message(WARNING "VRCH_ASAN is ON: Debug builds use AddressSanitizer. "
#                    "Start X-Plane with LD_PRELOAD=libasan.so, and turn this off when done.")
#    add_compile_options($<$<CONFIG:Debug>:-fsanitize=address>
#                        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
#    add_link_options($<$<CONFIG:Debug>:-fsanitize=address>)
#endif()

# --- Paths ------------------------------------------------------------------------
set(VRCH_LIBS_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/libs"   CACHE PATH "Third-party libraries root")
set(XPSDK_DIR      "${VRCH_LIBS_DIR}/xpsdk"              CACHE PATH "X-Plane SDK root")
set(IMGUI_DIR      "${VRCH_LIBS_DIR}/imgui"              CACHE PATH "Dear ImGui root")
set(IMGWINDOW_DIR  "${VRCH_LIBS_DIR}/ImgWindow"          CACHE PATH "ImgWindow root")
set(LOGURU_DIR     "${VRCH_LIBS_DIR}/loguru"             CACHE PATH "loguru root")
set(FONTS_DIR      "${VRCH_LIBS_DIR}/fonts"              CACHE PATH "Embedded fonts")

foreach(_check
        "${XPSDK_DIR}/CHeaders/XPLM/XPLMDefs.h"
        "${IMGUI_DIR}/imgui.h"
        "${IMGWINDOW_DIR}/ImgWindow.h"
        "${LOGURU_DIR}/loguru.cpp"
        "${FONTS_DIR}/IconsFontAwesome5.h"
        "${FONTS_DIR}/fa-solid-900.inc")
    if(NOT EXISTS "${_check}")
        message(FATAL_ERROR "Missing dependency: ${_check}\nRun libs/fetch_libs.sh first.")
    endif()
endforeach()

# --- X-Plane SDK (headers only on Linux) ----------------------------------------
add_library(xpsdk INTERFACE)
target_include_directories(xpsdk SYSTEM INTERFACE
    "${XPSDK_DIR}/CHeaders/XPLM"
    "${XPSDK_DIR}/CHeaders/Widgets")
# API levels up to 4.3. Do NOT add XPLM440: ImgWindow's Panel Graphics bridge
# relies on it being undefined to keep the OpenGL fallback for X-Plane < 12.4.4.
target_compile_definitions(xpsdk INTERFACE
    XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1
    XPLM400=1 XPLM410=1 XPLM420=1 XPLM430=1
    APL=0 IBM=0 LIN=1)
# XPLM symbols stay undefined; X-Plane resolves them when it loads the plugin.

# --- OpenGL (dynamic: provided by the GPU driver) --------------------------------
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)                  # Ubuntu/Debian: apt install libgl-dev

# --- Dear ImGui (static) ------------------------------------------------------------
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp")
target_include_directories(imgui SYSTEM PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/misc/cpp")

# --- ImgWindow (static) -----------------------------------------------------------
add_library(imgwindow STATIC
    "${IMGWINDOW_DIR}/ImgWindow.cpp"
    "${IMGWINDOW_DIR}/ImgFontAtlas.cpp")
target_include_directories(imgwindow SYSTEM PUBLIC "${IMGWINDOW_DIR}")
# Dynamic bridge: Panel Graphics (Vulkan/Metal) on X-Plane 12.4.4+,
# legacy OpenGL on older versions - one binary for both.
target_compile_definitions(imgwindow PUBLIC IMGWINDOW_USE_PANEL_GRAPHICS)
target_link_libraries(imgwindow PUBLIC imgui xpsdk OpenGL::GL)

# --- loguru (static) -------------------------------------------------------------
add_library(loguru STATIC "${LOGURU_DIR}/loguru.cpp")
target_include_directories(loguru SYSTEM PUBLIC "${LOGURU_DIR}")
target_compile_definitions(loguru PUBLIC LOGURU_WITH_STREAMS=0 LOGURU_USE_FMTLIB=0)
target_compile_options(loguru PRIVATE -w)        # third-party code: no warnings
find_package(Threads REQUIRED)
target_link_libraries(loguru PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# --- Fonts (header-only: IconsFontAwesome5.h, fa-solid-900.inc) -------------------
add_library(fonts INTERFACE)
target_include_directories(fonts SYSTEM INTERFACE "${FONTS_DIR}")

# --- Plugin -------------------------------------------------------------------------
# Copy every log message to X-Plane's Log.txt as well (runtime: log::SetMirrorToXPlane).
option(VRCH_LOG_TO_XPLANE "Default: also write plugin log messages to X-Plane's Log.txt" ON)

add_library(vrconfig_helper MODULE
    src/vrconfig_helper.cpp
    src/PluginWindow.cpp
    src/PluginWindow.h
    src/Fonts.cpp
    src/Fonts.h
    src/Log.cpp
    src/Log.h
    src/ui/hpvr_colors.hpp
    src/ui/Theme.h
    src/ui/Widgets.cpp
    src/ui/Widgets.h
    src/vrconfig/VrConfigFile.cpp
    src/vrconfig/VrConfigFile.h
    src/hotspot/SpotEditor.cpp
    src/hotspot/SpotEditor.h
    src/hotspot/SpotPage.cpp
    src/hotspot/SpotPage.h
    src/manip/ManipTypes.cpp
    src/manip/ManipTypes.h
    src/manip/ObjManips.cpp
    src/manip/ObjManips.h
    src/manip/ObjScanner.cpp
    src/manip/ObjScanner.h
    src/manip/ManipEditor.cpp
    src/manip/ManipEditor.h
    src/manip/ManipPage.cpp
    src/manip/ManipPage.h
    src/manip/ManipActions.cpp
    src/manip/ManipActions.h
    src/manip/ManipBlock.cpp
    src/manip/ManipBlock.h
    src/manip/ManipUpdater.cpp
    src/manip/ManipUpdater.h
    src/manip/RuleFile.cpp
    src/manip/RuleFile.h
    src/manip/AircraftFinder.cpp
    src/manip/AircraftFinder.h
    src/manip/UpdaterPage.cpp
    src/manip/UpdaterPage.h
    src/util/FileUtil.cpp
    src/util/FileUtil.h
    src/xp/Aircraft.cpp
    src/xp/Aircraft.h)

target_include_directories(vrconfig_helper PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(vrconfig_helper PRIVATE
    VRCONFIG_HELPER_VERSION="${PROJECT_VERSION}"
    VRCH_LOG_TO_XPLANE_DEFAULT=$<BOOL:${VRCH_LOG_TO_XPLANE}>)
target_compile_options(vrconfig_helper PRIVATE -Wall -Wextra -Wshadow)
target_link_libraries(vrconfig_helper PRIVATE imgwindow loguru fonts)

# Static C++ runtime; export only the five X-Plane entry points.
set(VRCH_EXPORTS "${CMAKE_CURRENT_SOURCE_DIR}/src/vrconfig_helper.sym")
set_property(TARGET vrconfig_helper APPEND PROPERTY LINK_DEPENDS "${VRCH_EXPORTS}")
target_link_options(vrconfig_helper PRIVATE
    -static-libgcc
    -static-libstdc++
    -Wl,--exclude-libs,ALL
    "-Wl,--version-script=${VRCH_EXPORTS}"
    -Wl,-O1
    -Wl,--as-needed)

set_target_properties(vrconfig_helper PROPERTIES
    PREFIX ""
    SUFFIX ".xpl"
    OUTPUT_NAME "vrconfig_helper"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vrconfig_helper/lin_x64")

install(TARGETS vrconfig_helper LIBRARY DESTINATION "vrconfig_helper/lin_x64")

# --- manip_types.ini and manip_actions.ini (manipulator pages) ---------------------------
# The plugin's rule files - the only place its manipulator types and ACTIONs are defined.
# Shipped next to the plugin: <build>/vrconfig_helper/<name>.ini, refreshed whenever the
# source file changes. They belong to the plugin: every install / deploy replaces them.
# A user's own rules live in custom_<name>.ini, which is never touched.
set(VRCH_MANIP_TYPES_INI "${CMAKE_CURRENT_SOURCE_DIR}/libs/manip_types.ini"
    CACHE FILEPATH "manip_types.ini shipped with the plugin")
set(VRCH_MANIP_ACTIONS_INI "${CMAKE_CURRENT_SOURCE_DIR}/libs/manip_actions.ini"
    CACHE FILEPATH "manip_actions.ini shipped with the plugin")
set(_vrch_ini_outputs "")
set(_vrch_ini_args "")
foreach(_vrch_ini IN ITEMS "${VRCH_MANIP_TYPES_INI}" "${VRCH_MANIP_ACTIONS_INI}")
    get_filename_component(_vrch_ini_name "${_vrch_ini}" NAME)
    if(EXISTS "${_vrch_ini}")
        set(_vrch_ini_out "${CMAKE_BINARY_DIR}/vrconfig_helper/${_vrch_ini_name}")
        add_custom_command(OUTPUT "${_vrch_ini_out}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_vrch_ini}" "${_vrch_ini_out}"
            DEPENDS "${_vrch_ini}"
            COMMENT "Copying ${_vrch_ini_name} next to the plugin"
            VERBATIM)
        list(APPEND _vrch_ini_outputs "${_vrch_ini_out}")
        install(FILES "${_vrch_ini}" DESTINATION "vrconfig_helper")
        list(APPEND _vrch_ini_args "${_vrch_ini}")
    else()
        message(FATAL_ERROR "vrconfig_helper: ${_vrch_ini} not found - the plugin needs it")
    endif()
endforeach()
if(_vrch_ini_outputs)
    add_custom_target(vrconfig_helper_ini ALL DEPENDS ${_vrch_ini_outputs})
    add_dependencies(vrconfig_helper vrconfig_helper_ini)
endif()
string(REPLACE ";" "|" _vrch_ini_arg "${_vrch_ini_args}")

# --- Deploy into local X-Plane installations (after every build) --------------------
# Copies the plugin to <XP root>/Resources/plugins/vrconfig_helper/<platform>/ for each
# X-Plane root in VRCH_DEPLOY_XP_ROOTS, and the rule files (.ini) to
# <XP root>/Resources/plugins/vrconfig_helper/ (only when missing, see below). Roots that don't exist are skipped.
# Disable with -DVRCH_DEPLOY=OFF, or override the list with
# -DVRCH_DEPLOY_XP_ROOTS="/path/one;/path/two".
option(VRCH_DEPLOY "Copy the plugin into local X-Plane installations after each build" ON)
# The rule files (.ini) are replaced on every deploy, like the plugin itself; a user's
# custom_<name>.ini next to them is never touched.

if(WIN32)
    set(VRCH_PLATFORM_DIR "win_x64")
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}" _vrch_home)
elseif(APPLE)
    set(VRCH_PLATFORM_DIR "mac_x64")
    set(_vrch_home "$ENV{HOME}")
else()
    set(VRCH_PLATFORM_DIR "lin_x64")
    set(_vrch_home "$ENV{HOME}")
endif()

set(VRCH_DEPLOY_XP_ROOTS
    "${_vrch_home}/xp12"
    "${_vrch_home}/xp12d"
    "${_vrch_home}/xp12b"
    "${_vrch_home}/xp12steam"
    CACHE STRING "X-Plane root folders to deploy the plugin into (; separated)")

if(VRCH_DEPLOY)
    # Build-time script: checks each folder when the copy runs, and replaces the
    # .xpl via copy + rename so a running X-Plane never sees a half-written file.
    set(_vrch_deploy_script "${CMAKE_BINARY_DIR}/deploy_plugin.cmake")
    file(WRITE "${_vrch_deploy_script}" [=[
string(REPLACE "|" ";" XP_ROOTS "${XP_ROOTS}")
get_filename_component(_name "${PLUGIN_FILE}" NAME)
foreach(_root IN LISTS XP_ROOTS)
    set(_plugins "${_root}/Resources/plugins")
    if(IS_DIRECTORY "${_plugins}")
        set(_dest "${_plugins}/${PLUGIN_NAME}/${PLATFORM_DIR}")
        file(MAKE_DIRECTORY "${_dest}")
        configure_file("${PLUGIN_FILE}" "${_dest}/${_name}.tmp" COPYONLY)
        file(RENAME "${_dest}/${_name}.tmp" "${_dest}/${_name}")
        message(STATUS "vrconfig_helper: deployed to ${_dest}")
        string(REPLACE "|" ";" _inis "${INI_FILES}")
        foreach(_ini_file IN LISTS _inis)
            if(EXISTS "${_ini_file}")
                get_filename_component(_ini_name "${_ini_file}" NAME)
                set(_ini "${_plugins}/${PLUGIN_NAME}/${_ini_name}")
                configure_file("${_ini_file}" "${_ini}.tmp" COPYONLY)
                file(RENAME "${_ini}.tmp" "${_ini}")
                message(STATUS "vrconfig_helper: ${_ini_name} copied to ${_plugins}/${PLUGIN_NAME}")
            endif()
        endforeach()
    else()
        message(STATUS "vrconfig_helper: skipped, not found: ${_plugins}")
    endif()
endforeach()
]=])

    string(REPLACE ";" "|" _vrch_roots_arg "${VRCH_DEPLOY_XP_ROOTS}")
    add_custom_command(TARGET vrconfig_helper POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DXP_ROOTS=${_vrch_roots_arg}"
            "-DPLUGIN_FILE=$<TARGET_FILE:vrconfig_helper>"
            "-DPLUGIN_NAME=vrconfig_helper"
            "-DPLATFORM_DIR=${VRCH_PLATFORM_DIR}"
            "-DINI_FILES=${_vrch_ini_arg}"
            -P "${_vrch_deploy_script}"
        COMMENT "Deploying vrconfig_helper to local X-Plane installations"
        VERBATIM)
endif()

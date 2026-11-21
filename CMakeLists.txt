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
#     libs/XPSDK.zip       the X-Plane SDK as a zip; unpacked into the build folder
#                          when libs/xpsdk/ is not there (see "X-Plane SDK" below)
#
#  Build (Linux, Windows, macOS - the same two commands):
#     cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#     cmake --build build --config Release -j
#  Result: the plugin folder, ready to copy into X-Plane/Resources/plugins:
#     build/vrconfig_helper/<lin_x64|win_x64|mac_x64>/vrconfig_helper.xpl
#     build/vrconfig_helper/manip_types.ini, manip_actions.ini
#  Install into X-Plane:
#     cmake --install build --prefix "<X-Plane>/Resources/plugins"
#  Releases for all three systems are built by GitHub Actions
#  (.github/workflows/release.yml).
# -----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)

# macOS: one plugin file for Apple Silicon and Intel Macs. macOS 13.3 is the first
# release whose C++ library has std::format. Both must be set before project().
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS: architectures in the plugin")
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "macOS: oldest supported macOS version")

# Windows: link the C/C++ runtime into the plugin (/MT), so users need no extra
# Visual C++ redistributable.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(vrconfig_helper
    VERSION 0.9.0
    DESCRIPTION "VRConfig Helper - X-Plane plugin"
    LANGUAGES C CXX)

# X-Plane's folder name for this system's plugin binary.
if(WIN32)
    set(VRCH_PLATFORM_DIR "win_x64")
elseif(APPLE)
    set(VRCH_PLATFORM_DIR "mac_x64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(VRCH_PLATFORM_DIR "lin_x64")
else()
    message(FATAL_ERROR "${PROJECT_NAME} builds on Linux, Windows and macOS only.")
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
set(IMGUI_DIR      "${VRCH_LIBS_DIR}/imgui"              CACHE PATH "Dear ImGui root")
set(IMGWINDOW_DIR  "${VRCH_LIBS_DIR}/ImgWindow"          CACHE PATH "ImgWindow root")
set(LOGURU_DIR     "${VRCH_LIBS_DIR}/loguru"             CACHE PATH "loguru root")
set(FONTS_DIR      "${VRCH_LIBS_DIR}/fonts"              CACHE PATH "Embedded fonts")

# --- X-Plane SDK ---------------------------------------------------------------------
# Used, first found wins:
#   1. -DXPSDK_DIR=<folder>          a folder you name
#   2. libs/xpsdk/                    an unpacked SDK in the source tree - only when it
#                                     holds this system's XPLM library (Windows:
#                                     Libraries/Win/XPLM_64.lib, macOS:
#                                     Libraries/Mac/XPLM.framework); else the zip
#   3. libs/XPSDK.zip                 unpacked into <build>/xpsdk_unpacked/; the folder
#                                     name inside the zip does not matter (SDK, xpsdk,
#                                     xpsdk-master, ...): the folder holding
#                                     CHeaders/XPLM/XPLMDefs.h is used.
set(XPSDK_DIR "" CACHE PATH "X-Plane SDK folder (empty = libs/xpsdk or libs/XPSDK.zip)")
set(VRCH_XPSDK_ZIP "${VRCH_LIBS_DIR}/XPSDK.zip" CACHE FILEPATH "X-Plane SDK as a zip")
if(XPSDK_DIR AND NOT EXISTS "${XPSDK_DIR}/CHeaders/XPLM/XPLMDefs.h")
    message(FATAL_ERROR "XPSDK_DIR=${XPSDK_DIR} has no CHeaders/XPLM/XPLMDefs.h")
endif()
# The file of this system's XPLM library, inside an SDK folder (Linux needs none).
if(WIN32)
    set(_vrch_xplm_needs "Libraries/Win/XPLM_64.lib")
elseif(APPLE)
    set(_vrch_xplm_needs "Libraries/Mac/XPLM.framework")
else()
    set(_vrch_xplm_needs "CHeaders/XPLM/XPLMDefs.h")
endif()
if(NOT XPSDK_DIR)
    if(EXISTS "${VRCH_LIBS_DIR}/xpsdk/CHeaders/XPLM/XPLMDefs.h"
       AND EXISTS "${VRCH_LIBS_DIR}/xpsdk/${_vrch_xplm_needs}")
        set(_vrch_xpsdk "${VRCH_LIBS_DIR}/xpsdk")
    elseif(EXISTS "${VRCH_XPSDK_ZIP}")
        if(EXISTS "${VRCH_LIBS_DIR}/xpsdk")
            message(STATUS "${PROJECT_NAME}: libs/xpsdk has no ${_vrch_xplm_needs} - using ${VRCH_XPSDK_ZIP}")
        endif()
        set(_vrch_unpacked "${CMAKE_BINARY_DIR}/xpsdk_unpacked")
        file(GLOB_RECURSE _vrch_defs "${_vrch_unpacked}/*/XPLMDefs.h")
        if(NOT _vrch_defs OR "${VRCH_XPSDK_ZIP}" IS_NEWER_THAN "${_vrch_unpacked}/.stamp")
            message(STATUS "${PROJECT_NAME}: unpacking ${VRCH_XPSDK_ZIP}")
            file(REMOVE_RECURSE "${_vrch_unpacked}")
            file(ARCHIVE_EXTRACT INPUT "${VRCH_XPSDK_ZIP}" DESTINATION "${_vrch_unpacked}")
            file(TOUCH "${_vrch_unpacked}/.stamp")
            file(GLOB_RECURSE _vrch_defs "${_vrch_unpacked}/*/XPLMDefs.h")
        endif()
        list(FILTER _vrch_defs INCLUDE REGEX "/CHeaders/XPLM/XPLMDefs\\.h$")
        if(NOT _vrch_defs)
            message(FATAL_ERROR "${VRCH_XPSDK_ZIP} has no CHeaders/XPLM/XPLMDefs.h")
        endif()
        list(GET _vrch_defs 0 _vrch_defs)
        get_filename_component(_vrch_xpsdk "${_vrch_defs}/../../.." ABSOLUTE)
    else()
        message(FATAL_ERROR "No X-Plane SDK: put it in libs/xpsdk/, or as libs/XPSDK.zip, "
                            "or pass -DXPSDK_DIR=<folder>.")
    endif()
    set(XPSDK_DIR "${_vrch_xpsdk}" CACHE PATH "X-Plane SDK folder (empty = libs/xpsdk or libs/XPSDK.zip)" FORCE)
endif()
message(STATUS "${PROJECT_NAME}: X-Plane SDK ${XPSDK_DIR}")

foreach(_check
        "${IMGUI_DIR}/imgui.h"
        "${IMGWINDOW_DIR}/ImgWindow.h"
        "${LOGURU_DIR}/loguru.cpp"
        "${FONTS_DIR}/IconsFontAwesome5.h"
        "${FONTS_DIR}/fa-solid-900.inc")
    if(NOT EXISTS "${_check}")
        message(FATAL_ERROR "Missing dependency: ${_check}")
    endif()
endforeach()

add_library(xpsdk INTERFACE)
target_include_directories(xpsdk SYSTEM INTERFACE
    "${XPSDK_DIR}/CHeaders/XPLM"
    "${XPSDK_DIR}/CHeaders/Widgets")
# X-Plane 11 API levels only (XP 11.50+): the plugin uses nothing newer, and it then
# loads in X-Plane 11 and 12. ImgWindow finds X-Plane 12's Panel Graphics functions
# at run time, so it needs no XPLM4xx flag. Do NOT add XPLM440: ImgWindow's Panel
# Graphics bridge relies on it being undefined.
target_compile_definitions(xpsdk INTERFACE
    XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1
    APL=$<BOOL:${APPLE}> IBM=$<BOOL:${WIN32}> LIN=$<STREQUAL:${VRCH_PLATFORM_DIR},lin_x64>)
if(WIN32)
    # Windows: link against X-Plane's import library.
    set(_vrch_xplm_lib "${XPSDK_DIR}/Libraries/Win/XPLM_64.lib")
    if(NOT EXISTS "${_vrch_xplm_lib}")
        message(FATAL_ERROR "Missing ${_vrch_xplm_lib} (is it in the SDK zip?)")
    endif()
    target_link_libraries(xpsdk INTERFACE "${_vrch_xplm_lib}")
elseif(APPLE)
    # macOS: link against the XPLM framework. Linked by its binary, so a zip that
    # stored the framework's symlinks as plain files still works.
    file(GLOB _vrch_xplm_bins "${XPSDK_DIR}/Libraries/Mac/XPLM.framework/Versions/*/XPLM")
    set(_vrch_xplm_lib "")
    foreach(_bin IN LISTS _vrch_xplm_bins)
        if(NOT IS_SYMLINK "${_bin}" AND NOT IS_DIRECTORY "${_bin}")
            file(SIZE "${_bin}" _size)
            if(_size GREATER 1000)
                set(_vrch_xplm_lib "${_bin}")
            endif()
        endif()
    endforeach()
    if(NOT _vrch_xplm_lib)
        message(FATAL_ERROR "No XPLM framework binary under ${XPSDK_DIR}/Libraries/Mac")
    endif()
    target_link_libraries(xpsdk INTERFACE "${_vrch_xplm_lib}")
endif()
# Linux: XPLM symbols stay undefined; X-Plane resolves them when it loads the plugin.

# --- OpenGL (dynamic: provided by the system / GPU driver) -----------------------
if(VRCH_PLATFORM_DIR STREQUAL "lin_x64")
    set(OpenGL_GL_PREFERENCE GLVND)
endif()
find_package(OpenGL REQUIRED)                  # Ubuntu/Debian: apt install libgl-dev

# --- Compiler settings for the third-party libraries -------------------------------
if(MSVC)
    # /utf-8: sources and strings are UTF-8. The defines keep <windows.h> small and
    # stop MSVC's "use the _s functions" warnings in third-party code.
    add_compile_options(/utf-8 /MP)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
if(APPLE)
    # Apple marks all of OpenGL deprecated; X-Plane still provides it, and ImgWindow
    # uses it below X-Plane 12.4.4. Silences ~40 warnings per file.
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

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
target_compile_options(loguru PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)   # third-party code: no warnings
find_package(Threads REQUIRED)
target_link_libraries(loguru PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# --- Fonts (header-only: IconsFontAwesome5.h, fa-solid-900.inc) -------------------
add_library(fonts INTERFACE)
target_include_directories(fonts SYSTEM INTERFACE "${FONTS_DIR}")

# --- Plugin -------------------------------------------------------------------------
# Copy every log message to X-Plane's Log.txt as well (runtime: log::SetMirrorToXPlane).
option(VRCH_LOG_TO_XPLANE "Default: also write plugin log messages to X-Plane's Log.txt" ON)

add_library(${PROJECT_NAME} MODULE
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

target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(${PROJECT_NAME} PRIVATE
    VRCONFIG_HELPER_VERSION="${PROJECT_VERSION}"
    VRCH_LOG_TO_XPLANE_DEFAULT=$<BOOL:${VRCH_LOG_TO_XPLANE}>)
if(MSVC)
    # /Zc:preprocessor: the standard preprocessor (the LOG_* macros use __VA_OPT__).
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wshadow)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE imgwindow loguru fonts)

if(VRCH_PLATFORM_DIR STREQUAL "lin_x64")
    # Static C++ runtime; export only the five X-Plane entry points.
    set(VRCH_EXPORTS "${CMAKE_CURRENT_SOURCE_DIR}/src/vrconfig_helper.sym")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${VRCH_EXPORTS}")
    target_link_options(${PROJECT_NAME} PRIVATE
        -static-libgcc
        -static-libstdc++
        -Wl,--exclude-libs,ALL
        "-Wl,--version-script=${VRCH_EXPORTS}"
        -Wl,-O1
        -Wl,--as-needed)
elseif(APPLE)
    # Only the X-Plane entry points (PLUGIN_API) are visible: the visibility
    # presets above hide everything else. Strip unused code.
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,-dead_strip)
endif()
# Windows: PLUGIN_API marks the entry points dllexport; nothing else is exported.

# "$<0:>" stops multi-configuration generators (Visual Studio, Xcode) from adding a
# Release/ or Debug/ sub-folder: the plugin always lands in the X-Plane layout.
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".xpl"
    OUTPUT_NAME "${PROJECT_NAME}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/${VRCH_PLATFORM_DIR}$<0:>")

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION "${PROJECT_NAME}/${VRCH_PLATFORM_DIR}")

# --- manip_types.ini and manip_actions.ini (manipulator pages) ---------------------------
# The plugin's rule files - the only place its manipulator types and ACTIONs are defined.
# Shipped next to the plugin: <build>/${PROJECT_NAME}/<name>.ini, refreshed whenever the
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
        set(_vrch_ini_out "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/${_vrch_ini_name}")
        add_custom_command(OUTPUT "${_vrch_ini_out}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_vrch_ini}" "${_vrch_ini_out}"
            DEPENDS "${_vrch_ini}"
            COMMENT "Copying ${_vrch_ini_name} next to the plugin"
            VERBATIM)
        list(APPEND _vrch_ini_outputs "${_vrch_ini_out}")
        install(FILES "${_vrch_ini}" DESTINATION "${PROJECT_NAME}")
        list(APPEND _vrch_ini_args "${_vrch_ini}")
    else()
        message(FATAL_ERROR "${PROJECT_NAME}: ${_vrch_ini} not found - the plugin needs it")
    endif()
endforeach()
if(_vrch_ini_outputs)
    add_custom_target(${PROJECT_NAME}_ini ALL DEPENDS ${_vrch_ini_outputs})
    add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_ini)
endif()
string(REPLACE ";" "|" _vrch_ini_arg "${_vrch_ini_args}")

# --- Deploy into local X-Plane installations (after every build) --------------------
# Copies the plugin to <XP root>/Resources/plugins/${PROJECT_NAME}/<platform>/ for each
# X-Plane root in VRCH_DEPLOY_XP_ROOTS, and the rule files (.ini) to
# <XP root>/Resources/plugins/${PROJECT_NAME}/ (only when missing, see below). Roots that don't exist are skipped.
# Disable with -DVRCH_DEPLOY=OFF, or override the list with
# -DVRCH_DEPLOY_XP_ROOTS="/path/one;/path/two".
option(VRCH_DEPLOY "Copy the plugin into local X-Plane installations after each build" ON)
# The rule files (.ini) are replaced on every deploy, like the plugin itself; a user's
# custom_<name>.ini next to them is never touched.

if(WIN32)
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}" _vrch_home)
else()
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
        message(STATUS "${PLUGIN_NAME}: deployed to ${_dest}")
        string(REPLACE "|" ";" _inis "${INI_FILES}")
        foreach(_ini_file IN LISTS _inis)
            if(EXISTS "${_ini_file}")
                get_filename_component(_ini_name "${_ini_file}" NAME)
                set(_ini "${_plugins}/${PLUGIN_NAME}/${_ini_name}")
                configure_file("${_ini_file}" "${_ini}.tmp" COPYONLY)
                file(RENAME "${_ini}.tmp" "${_ini}")
                message(STATUS "${PLUGIN_NAME}: ${_ini_name} copied to ${_plugins}/${PLUGIN_NAME}")
            endif()
        endforeach()
    else()
        message(STATUS "${PLUGIN_NAME}: skipped, not found: ${_plugins}")
    endif()
endforeach()
]=])

    string(REPLACE ";" "|" _vrch_roots_arg "${VRCH_DEPLOY_XP_ROOTS}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DXP_ROOTS=${_vrch_roots_arg}"
            "-DPLUGIN_FILE=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DPLUGIN_NAME=${PROJECT_NAME}"
            "-DPLATFORM_DIR=${VRCH_PLATFORM_DIR}"
            "-DINI_FILES=${_vrch_ini_arg}"
            -P "${_vrch_deploy_script}"
        COMMENT "Deploying ${PROJECT_NAME} to local X-Plane installations"
        VERBATIM)
endif()

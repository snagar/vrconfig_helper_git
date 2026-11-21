#!/usr/bin/env bash
# Fetch the pinned third-party sources into this libs/ folder:
#   libs/xpsdk      X-Plane SDK      https://github.com/X-Plane/xpsdk
#   libs/imgui      Dear ImGui       https://github.com/ocornut/imgui
#   libs/ImgWindow  ImgWindow        https://github.com/slgoldberg/ImgWindow
# A library that is already present is left untouched; delete its folder to re-fetch.
set -euo pipefail

LIBS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

XPSDK_TAG="xpsdk_4.4.0-r2"
IMGUI_TAG="v1.92.8"
IMGWINDOW_COMMIT="52fa0b1"     # ImgWindow v2.4.1

if [ ! -f "$LIBS/xpsdk/CHeaders/XPLM/XPLMDefs.h" ]; then
    git -c advice.detachedHead=false clone --depth 1 --branch "$XPSDK_TAG" \
        https://github.com/X-Plane/xpsdk "$LIBS/xpsdk"
fi

if [ ! -f "$LIBS/imgui/imgui.h" ]; then
    git -c advice.detachedHead=false clone --depth 1 --branch "$IMGUI_TAG" \
        https://github.com/ocornut/imgui "$LIBS/imgui"
fi

if [ ! -f "$LIBS/ImgWindow/ImgWindow.h" ]; then
    git clone https://github.com/slgoldberg/ImgWindow "$LIBS/ImgWindow"
    git -C "$LIBS/ImgWindow" -c advice.detachedHead=false checkout --quiet "$IMGWINDOW_COMMIT"
fi

echo "libs ready in $LIBS"

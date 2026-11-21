// vrconfig_helper - fonts shared by all windows

#include "Fonts.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "XPLMUtilities.h"

#include "ImgWindow.h"
#include "ImgFontAtlas.h"
#include "Log.h"

// Compressed Font Awesome 5 Solid TTF (fa_solid_900_compressed_data / _size).
// Included in this translation unit only.
#include "fa-solid-900.inc"

namespace hpvr::fonts {

ImFont* regular    = nullptr;
ImFont* fontawsome = nullptr;

namespace {

// Glyph range for the icons listed in IconsFontAwesome5.h (must outlive the atlas).
const ImWchar kFontAwesomeRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };

std::string XPlaneFontPath(const char* fileName)
{
    char sysPath[1024] = {};
    XPLMGetSystemPath(sysPath);                       // native path, ends with a separator
    return std::string(sysPath) + "Resources/fonts/" + fileName;
}

} // namespace

void Initialize()
{
    ImgWindow::sFontAtlas = std::make_shared<ImgFontAtlas>();

    // 1. Regular text font (first font added = ImGui default font)
    const std::string dejaVu = XPlaneFontPath("DejaVuSans.ttf");
    std::error_code ec;
    if (std::filesystem::exists(dejaVu, ec))
        regular = ImgWindow::sFontAtlas->AddFontFromFileTTF(dejaVu.c_str(), kFontSize);
    if (!regular) {
        LOG_WARN("{} not found, using the ImGui default font", dejaVu);
        regular = ImgWindow::sFontAtlas->AddFontDefault();
    }

    // 1b. Font Awesome merged into the regular font, so ICON_FA_* symbols can be
    //     used inside ordinary labels ("ICON_FA_SAVE " Save").
    {
        ImFontConfig merge;
        merge.MergeMode            = true;
        merge.PixelSnapH           = true;
        merge.GlyphMinAdvanceX     = kFontSize;
        merge.FontDataOwnedByAtlas = false;
        if (!ImgWindow::sFontAtlas->AddFontFromMemoryCompressedTTF(
                fa_solid_900_compressed_data, static_cast<int>(fa_solid_900_compressed_size),
                kFontSize, &merge, kFontAwesomeRanges))
            LOG_WARN("could not merge the icons into the regular font");
    }

    // 2. Font Awesome icons, as a separate font
    ImFontConfig cfg;
    cfg.PixelSnapH       = true;
    cfg.GlyphMinAdvanceX = kFontSize;                 // square icon cells, icons line up in columns
    cfg.FontDataOwnedByAtlas = false;                 // data is a static array in this file
    std::snprintf(cfg.Name, sizeof(cfg.Name), "fontawsome");
    fontawsome = ImgWindow::sFontAtlas->AddFontFromMemoryCompressedTTF(
        fa_solid_900_compressed_data, static_cast<int>(fa_solid_900_compressed_size),
        kFontSize, &cfg, kFontAwesomeRanges);
    if (!fontawsome)
        LOG_ERROR("failed to load the Font Awesome icon font");

    LOG_INFO("fonts loaded: regular={}, fontawsome={}",
             regular ? "ok" : "missing", fontawsome ? "ok" : "missing");
}

void Shutdown()
{
#if defined(IMGUI_VERSION_NUM) && (IMGUI_VERSION_NUM >= 19200)
    // Detach ImGui from the shared atlas before the atlas is freed.
    if (ImGui::GetCurrentContext() != nullptr)
        ImGui::GetIO().Fonts = nullptr;
#endif
    regular    = nullptr;
    fontawsome = nullptr;
    ImgWindow::sFontAtlas.reset();
    ImgWindow::Shutdown();                            // flush deferred texture deletions
}

} // namespace hpvr::fonts

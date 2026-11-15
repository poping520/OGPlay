#pragma once

#include "ogplay/app/game_run.h"
#include "ogplay/app/library.h"

#include <map>
#include <string>
#include <string_view>

namespace ogplay::app {

// Guest screen size from presets (MODULE.md, 画面尺寸): screen_resolution names the short side (480p…2160p) or
// "custom", which uses screen_width/screen_height as set; screen_aspect gives the long side (a ratio, or "screen":
// the display's, applied by the game process). Presets describe landscape; portrait games swap them.

using SettingValues = std::map<std::string, SettingValue, std::less<>>;

// Configs written before the presets hold only screen_width/screen_height. A game's own size, and a global size
// other than the old 1280×720 default, become "custom" so they keep showing as set; the global default (written by
// every settings save) is dropped so the presets apply.
void MigrateLegacyScreenSize(SettingValues& values, bool global);

struct ResolvedScreenSize final {
    std::uint32_t width{};
    std::uint32_t height{};
    // Presets: limit, or follow for screen_aspect = screen (width is then 16:9 until the display is known).
    SurfaceAdaptation adaptation{SurfaceAdaptation::none};
};

// resolution/aspect are the effective choices; width/height the effective custom size.
[[nodiscard]] ResolvedScreenSize ResolveScreenSize(std::string_view resolution, std::string_view aspect,
                                                   std::uint32_t width, std::uint32_t height);

}  // namespace ogplay::app

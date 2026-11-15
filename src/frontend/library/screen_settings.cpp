#include "screen_settings.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace ogplay::app {
namespace {

struct Resolution final {
    std::string_view key;
    std::uint32_t short_side;
};
constexpr std::array<Resolution, 6> kResolutions{{
    {"480p", 480}, {"540p", 540}, {"720p", 720}, {"1080p", 1080}, {"1440p", 1440}, {"2160p", 2160}}};

struct Aspect final {
    std::string_view key;
    std::uint32_t numerator, denominator;
};
constexpr std::array<Aspect, 6> kAspects{{
    {"4:3", 4, 3}, {"16:10", 16, 10}, {"16:9", 16, 9}, {"18:9", 18, 9}, {"19.5:9", 39, 18}, {"20:9", 20, 9}}};

}  // namespace

void MigrateLegacyScreenSize(SettingValues& values, const bool global) {
    if (values.contains("screen_resolution")) return;
    const auto width = values.find("screen_width"), height = values.find("screen_height");
    if (width == values.end() && height == values.end()) return;
    const auto number = [&](auto found, std::uint32_t fallback) {
        return found == values.end() ? fallback : std::get<std::uint32_t>(found->second);
    };
    if (global && number(width, kDefaultSurfaceWidth) == kDefaultSurfaceWidth &&
        number(height, kDefaultSurfaceHeight) == kDefaultSurfaceHeight) {
        values.erase("screen_width");
        values.erase("screen_height");
        return;
    }
    values["screen_resolution"] = std::string("custom");
}

ResolvedScreenSize ResolveScreenSize(const std::string_view resolution, const std::string_view aspect,
                                     const std::uint32_t width, const std::uint32_t height) {
    if (resolution == "custom") return {width, height, SurfaceAdaptation::none};
    const auto preset = std::find_if(kResolutions.begin(), kResolutions.end(),
                                     [&](const auto& item) { return item.key == resolution; });
    if (preset == kResolutions.end()) throw std::invalid_argument("screen_resolution: unknown preset");
    // The display's aspect is only known to the game process: 16:9 stands in until it fits the surface.
    if (aspect == "screen")
        return {SurfaceLongSide(preset->short_side, 16, 9), preset->short_side, SurfaceAdaptation::follow};
    const auto ratio =
        std::find_if(kAspects.begin(), kAspects.end(), [&](const auto& item) { return item.key == aspect; });
    if (ratio == kAspects.end()) throw std::invalid_argument("screen_aspect: unknown ratio");
    return {SurfaceLongSide(preset->short_side, ratio->numerator, ratio->denominator), preset->short_side,
            SurfaceAdaptation::limit};
}

}  // namespace ogplay::app

#include "android_boundary_gles1.h"

#include <bit>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>

namespace ogplay::runtime::detail {

void AndroidBoundaryGles1State::Reset() noexcept {
    shade_model_ = kGles1SmoothShadeModel;
}

void AndroidBoundaryGles1State::SetShadeModel(const std::uint32_t mode) {
    if (mode != kGles1FlatShadeModel && mode != kGles1SmoothShadeModel) {
        throw std::invalid_argument(
            "glShadeModel mode must be GL_FLAT or GL_SMOOTH");
    }
    shade_model_ = mode;
}

std::uint32_t AndroidBoundaryGles1State::ShadeModel() const noexcept {
    return shade_model_;
}

std::int32_t ScaleAndroidBoundaryViewportComponent(
    const std::int32_t value, const std::uint32_t factor) {
    const auto scaled = static_cast<std::int64_t>(value) * factor;
    if (scaled < std::numeric_limits<std::int32_t>::min() ||
        scaled > std::numeric_limits<std::int32_t>::max()) {
        throw std::overflow_error("supersampled viewport component overflows");
    }
    return static_cast<std::int32_t>(scaled);
}

void BindAndroidBoundaryGles1Core(
    gles::GlesDispatchTable& dispatch, AndroidBoundaryGles1State& state,
    const std::uint32_t supersample_factor,
    AndroidBoundaryFrameResolver require_frame) {
    if (supersample_factor == 0 || !require_frame) {
        throw std::invalid_argument("GLES1 boundary binding is incomplete");
    }
    dispatch.Bind(
        "glViewport",
        [supersample_factor, require_frame](
            const std::span<const std::uint32_t> arguments, const std::uint64_t) {
            require_frame("glViewport")
                .Viewport(ScaleAndroidBoundaryViewportComponent(
                              std::bit_cast<std::int32_t>(arguments[0]),
                              supersample_factor),
                          ScaleAndroidBoundaryViewportComponent(
                              std::bit_cast<std::int32_t>(arguments[1]),
                              supersample_factor),
                          ScaleAndroidBoundaryViewportComponent(
                              std::bit_cast<std::int32_t>(arguments[2]),
                              supersample_factor),
                          ScaleAndroidBoundaryViewportComponent(
                              std::bit_cast<std::int32_t>(arguments[3]),
                              supersample_factor));
            return 0U;
        });
    dispatch.Bind(
        "glScissor",
        [supersample_factor, require_frame](
            const std::span<const std::uint32_t> arguments, const std::uint64_t) {
            require_frame("glScissor")
                .Scissor(ScaleAndroidBoundaryViewportComponent(
                             std::bit_cast<std::int32_t>(arguments[0]),
                             supersample_factor),
                         ScaleAndroidBoundaryViewportComponent(
                             std::bit_cast<std::int32_t>(arguments[1]),
                             supersample_factor),
                         ScaleAndroidBoundaryViewportComponent(
                             std::bit_cast<std::int32_t>(arguments[2]),
                             supersample_factor),
                         ScaleAndroidBoundaryViewportComponent(
                             std::bit_cast<std::int32_t>(arguments[3]),
                             supersample_factor));
            return 0U;
        });
    dispatch.Bind(
        "glShadeModel",
        [&state, require_frame = std::move(require_frame)](
            const std::span<const std::uint32_t> arguments, const std::uint64_t) {
            static_cast<void>(require_frame("glShadeModel"));
            state.SetShadeModel(arguments[0]);
            return 0U;
        });
}

}  // namespace ogplay::runtime::detail

#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/gles_dispatch.h"

namespace ogplay::runtime::detail {

inline constexpr std::uint32_t kGles1FlatShadeModel = 0x1D00U;
inline constexpr std::uint32_t kGles1SmoothShadeModel = 0x1D01U;

class AndroidBoundaryGles1State final {
public:
    void Reset() noexcept;
    void SetShadeModel(std::uint32_t mode);
    [[nodiscard]] std::uint32_t ShadeModel() const noexcept;

private:
    std::uint32_t shade_model_{kGles1SmoothShadeModel};
};

using AndroidBoundaryFrameResolver =
    std::function<gles::AngleFrame&(std::string_view operation)>;

[[nodiscard]] std::int32_t ScaleAndroidBoundaryViewportComponent(
    std::int32_t value, std::uint32_t factor);

void BindAndroidBoundaryGles1Core(
    gles::GlesDispatchTable& dispatch, AndroidBoundaryGles1State& state,
    std::uint32_t supersample_factor,
    AndroidBoundaryFrameResolver require_frame);

}  // namespace ogplay::runtime::detail

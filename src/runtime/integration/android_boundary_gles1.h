#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/gles_dispatch.h"

namespace ogplay::runtime::detail {

using AndroidBoundaryFrameResolver =
    std::function<gles::AngleFrame&(std::string_view operation)>;

[[nodiscard]] std::int32_t ScaleAndroidBoundaryViewportComponent(
    std::int32_t value, std::uint32_t factor);

void BindAndroidBoundaryGles1Core(
    gles::GlesDispatchTable& dispatch, std::uint32_t supersample_factor,
    AndroidBoundaryFrameResolver require_frame);

}  // namespace ogplay::runtime::detail

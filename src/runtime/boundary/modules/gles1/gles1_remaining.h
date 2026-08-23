#pragma once

#include "gles1_draw.h"

namespace ogplay::runtime::detail {

void BindAndroidBoundaryGles1Remaining(gles::GlesDispatchTable& dispatch,
                                       AndroidBoundaryGles1State& core,
                                       AndroidBoundaryGles1LegacyState& legacy,
                                       AndroidBoundaryGles1DrawState& draw,
                                       memory::AddressSpace& address_space,
                                       AndroidBoundaryFrameResolver require_frame);

} // namespace ogplay::runtime::detail

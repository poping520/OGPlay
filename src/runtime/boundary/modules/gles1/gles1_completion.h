#pragma once

#include "gles1_dispatch.h"
#include "gles1_query.h"

namespace ogplay::runtime::detail {

void BindAndroidBoundaryGles1Completion(gles::GlesDispatchTable& dispatch,
                                        AndroidBoundaryGles1State& core,
                                        AndroidBoundaryGles1LegacyState& legacy,
                                        memory::AddressSpace& address_space,
                                        AndroidBoundaryFrameResolver require_frame);

} // namespace ogplay::runtime::detail

#pragma once

#include <optional>

#include "ogplay/hal/window_input.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"

namespace ogplay::session {

[[nodiscard]] std::optional<runtime::AndroidBoundaryInput> MapAndroidInput(
    const hal::InputEvent& event);

}  // namespace ogplay::session

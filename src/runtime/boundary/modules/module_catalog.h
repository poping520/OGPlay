#pragma once

#include <string_view>

#include "runtime/boundary/core/boundary_catalog.h"

namespace ogplay::runtime {

[[nodiscard]] const BoundaryCatalog& AndroidBoundaryCatalog(AndroidApi api);
[[nodiscard]] bool IsAndroidBoundaryLibrary(AndroidApi api,
                                            std::string_view soname) noexcept;

}  // namespace ogplay::runtime

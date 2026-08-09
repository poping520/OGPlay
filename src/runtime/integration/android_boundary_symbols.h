#pragma once

#include <vector>

#include "ogplay/runtime/bionic/bionic_profile.h"

namespace ogplay::runtime::detail {

[[nodiscard]] std::vector<BionicHleSymbol> BuildAndroidBoundarySymbols();

}  // namespace ogplay::runtime::detail

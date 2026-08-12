#pragma once

#include <span>
#include <string>
#include <string_view>

#include "ogplay/runtime/dexvm/class_linker.h"

namespace ogplay::runtime::dexvm {

// Renders a gap survey harvest as a machine-readable work queue (hottest
// first) for the next adaptation batch. The document says out loud that a
// survey run substituted neutral stubs and is therefore not a compatibility
// result — see docs/playbook/NEW-TITLE.md.
[[nodiscard]] std::string RenderGapSurveyJson(std::span<const GapSurveyHit> hits,
                                             std::string_view title);

}  // namespace ogplay::runtime::dexvm

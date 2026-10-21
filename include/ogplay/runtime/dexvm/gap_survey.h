#pragma once

#include <span>
#include <string>
#include <string_view>

#include "ogplay/runtime/dexvm/class_linker.h"

namespace ogplay::runtime::dexvm {

// Renders a gap survey harvest (schema 2) as a machine-readable work queue
// for the next adaptation batch: member records with source, channel, first
// hit position/time/stack, ordered hottest first, plus the same members in
// first-hit order. The document says out loud that a survey run substituted
// neutral answers and is therefore not a compatibility result — see
// docs/playbook/NEW-TITLE.md and docs/tasks/diagnostics/DIAG-5.md.
[[nodiscard]] std::string RenderGapSurveyJson(std::span<const GapSurveyHit> hits,
                                             std::string_view title);

}  // namespace ogplay::runtime::dexvm

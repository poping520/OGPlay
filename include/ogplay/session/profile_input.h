#pragma once

#include <span>
#include <vector>

#include "ogplay/input/template_catalog.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::session {

[[nodiscard]] inline std::vector<hal::InputEvent> ApplyProfileInput(
    const TitleProfile& profile, const input::InputTemplateCatalog& catalog,
    const std::span<const hal::InputEvent> events) {
    const auto selected = profile.input.has_value()
                              ? std::string_view{profile.input->profile}
                              : catalog.DefaultTemplate();
    return catalog.Map(selected, events);
}

}  // namespace ogplay::session

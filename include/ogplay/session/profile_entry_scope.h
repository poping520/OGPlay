#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include "ogplay/session/title_profile.h"

namespace ogplay::core {
class Logger;
}

namespace ogplay::runtime::dexvm {
class Interpreter;
}

namespace ogplay::session {

class ProfileEntryScopeError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Returns an L...; descriptor. Profile entry overrides the manifest fact;
// absence of both is an explicit configuration failure.
[[nodiscard]] std::string ResolveProfileLaunchDescriptor(
    const TitleProfile& profile,
    const std::optional<std::string>& manifest_launcher);

// Initializes each declared guest class, then applies its checked static
// field preset and emits one auditable structured record.
void ApplyProfileStaticPresets(const TitleProfile& profile,
                               runtime::dexvm::Interpreter& vm,
                               core::Logger* logger = nullptr);

}  // namespace ogplay::session

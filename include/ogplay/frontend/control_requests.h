#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "ogplay/agent/control_service.h"
#include "ogplay/core/json.h"
#include "ogplay/input/control_mapper.h"
#include "ogplay/session/control_config.h"

namespace ogplay::agent {
class FrameSnapshotStore;
}

namespace ogplay::frontend {

// Guest main-loop state the key editor may read or change (ADR-0134). Built per request.
struct ControlRequestContext final {
    input::ControlMapper& mapper;
    bool& frozen;
    session::ControlLayer& user;
    const session::ControlLayer* profile_input{};
    const std::filesystem::path& templates_directory;
    const std::optional<std::filesystem::path>& config_path;
    const agent::FrameSnapshotStore* frames{};
    std::uint32_t surface_width{}, surface_height{};
    // Host cancellation when freezing: clear mapper and MCP state, cancel guest input.
    std::function<void()> freeze;
    // Queues mapper outputs (the old layout's CANCEL and key releases) to the guest.
    std::function<void(const std::vector<input::ControlOutput>&)> deliver;
    // Running count of in-game F2 presses handed to a launcher; reported by control.status.
    std::uint64_t edit_requests{};
    // Called for every valid request; true for a launcher poll (control.status with
    // client "launcher"), false for editor traffic, which keeps a freeze alive.
    std::function<void(bool launcher)> seen;
};

// control.get / freeze / resume / frame / apply / status. Configuration errors are JSON-RPC
// error -32010 with the loader's message; the running layout is unchanged on any failure.
[[nodiscard]] agent::ControlResponse HandleControlRequest(std::string_view method,
                                                          core::JsonValue params,
                                                          const ControlRequestContext& context);

}  // namespace ogplay::frontend

#pragma once

#include "ogplay/core/json.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace ogplay::agent {

enum class McpLifecycleState : std::uint8_t {
    ready,
    running,
    suspended,
    stopped,
    failed,
};

struct McpMovieRequestSnapshot final {
    std::uint64_t sequence{};
    std::string name;
};

struct McpSessionSnapshot final {
    McpLifecycleState lifecycle{McpLifecycleState::ready};
    std::uint64_t frame{};
    std::uint64_t guest_ticks{};
    std::optional<std::uint64_t> presented_frame;
    std::optional<McpMovieRequestSnapshot> movie_request;
    bool process_exit{};
    std::optional<std::string> guest_fault;
    bool shutdown_requested{};
};

struct McpSessionCommand final {
    enum class Type : std::uint8_t { step, suspend, resume, shutdown };

    Type type{};
    std::uint64_t request_sequence{};
    std::uint64_t starting_frame{};
    std::uint32_t frames{};
};

class McpSessionControl final {
public:
    static constexpr std::size_t kMaximumPendingCommands = 64U;
    static constexpr std::uint32_t kMaximumStepFrames = 1'000'000U;

    void Publish(McpSessionSnapshot snapshot);
    [[nodiscard]] McpSessionSnapshot Snapshot() const;
    [[nodiscard]] std::optional<McpSessionCommand> TryEnqueue(
        McpSessionCommand::Type type, std::uint32_t frames = 0U);
    [[nodiscard]] std::optional<McpSessionCommand> TakeNextCommand();
    [[nodiscard]] std::size_t PendingCommands() const;
    void SetDiagnosticSnapshotHandler(
        std::function<std::optional<std::string>()> handler);
    [[nodiscard]] std::optional<std::string> RequestDiagnosticSnapshot() const;

private:
    mutable std::mutex mutex_;
    McpSessionSnapshot snapshot_;
    std::deque<McpSessionCommand> commands_;
    std::uint64_t next_request_sequence_{1U};
    std::function<std::optional<std::string>()> diagnostic_snapshot_;
};

[[nodiscard]] std::string_view McpLifecycleStateName(McpLifecycleState state);

void AppendMcpSessionTools(core::JsonWriter& writer,
                           core::JsonWriter::Value tools);
[[nodiscard]] core::JsonWriter::Value CallMcpSessionTool(
    core::JsonWriter& writer, McpSessionControl* control,
    std::string_view name, std::optional<core::JsonValue> arguments);
[[nodiscard]] bool IsMcpSessionTool(std::string_view name);

}  // namespace ogplay::agent

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include "ogplay/runtime/common/guest_cpu_config.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::core { class Logger; }
namespace ogplay::frontend {

struct GameRunOptions final {
    std::filesystem::path apk_path;
    std::optional<std::filesystem::path> external_directory;
    std::optional<std::string> external_guest_directory;
    std::optional<std::filesystem::path> obb_path;
    std::optional<std::filesystem::path> profiles_directory;
    std::optional<std::uint64_t> exit_after_frames;
    std::optional<std::uint16_t> mcp_port;
    std::optional<std::uint16_t> key_editor_port;
    std::optional<runtime::dexvm::InterpreterBackend> dexvm_interpreter;
    std::filesystem::path diagnostic_directory{".local/diagnostics"};
    std::optional<std::chrono::milliseconds> diagnostic_teardown_timeout;
    bool diagnostics_enabled{};
    std::uint32_t supersample_factor{1};
    runtime::GuestCpuConfig cpu_config;
    bool mcp_manual_step{};
    bool preflight{};
    // Diagnostic gap survey (dex_activity only): substitutes recorded neutral
    // stubs for unresolved platform surfaces so one run harvests the whole
    // work queue. Never a compatibility result.
    std::optional<std::filesystem::path> survey_gaps_output;
    // Saves persist by default (ADR-0020); automation opts out so golden
    // frames stay reproducible.
    std::optional<std::filesystem::path> sandbox_directory;
    std::optional<std::string> installation_id;
    bool ephemeral_sandbox{};
    std::optional<std::filesystem::path> input_config_path;
};

// Arguments start at the APK path, independently of the host entry mode.
[[nodiscard]] GameRunOptions ParseGameRunOptions(std::span<const std::string_view> args);
int RunGame(GameRunOptions options, core::Logger& logger);

}  // namespace ogplay::frontend

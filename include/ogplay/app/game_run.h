#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "ogplay/app/export.h"
#include "ogplay/app/logging.h"

namespace ogplay::app {
enum class InterpreterBackend { switch_dispatch, threaded };
struct CpuExecutionConfig final {
    bool limit_parallelism{};
    std::uint32_t max_mticks_per_second{};
    bool operator==(const CpuExecutionConfig&) const = default;
};
struct CpuConfig final {
    std::uint32_t cores{1};
    std::uint32_t frequency_mhz{1000};
    CpuExecutionConfig execution;
    bool operator==(const CpuConfig&) const = default;
};

inline constexpr std::uint32_t kMaximumSurfaceDimension = 4096;

struct GameRunOptions final {
    std::filesystem::path apk_path;
    std::optional<std::filesystem::path> external_directory;
    std::optional<std::string> external_guest_directory;
    std::optional<std::filesystem::path> obb_path;
    std::optional<std::filesystem::path> profiles_directory;
    std::optional<std::uint64_t> exit_after_frames;
    std::optional<std::uint16_t> mcp_port;
    std::optional<std::uint16_t> key_editor_port;
    std::optional<InterpreterBackend> dexvm_interpreter;
    std::filesystem::path diagnostic_directory{".local/diagnostics"};
    std::optional<std::chrono::milliseconds> diagnostic_teardown_timeout;
    bool diagnostics_enabled{};
    std::uint32_t supersample_factor{1};
    std::uint32_t surface_width{800};
    std::uint32_t surface_height{480};
    CpuConfig cpu_config;
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
    std::optional<std::filesystem::path> library_root;
    // ADR-0146: JSON of /sdcard reads that found nothing, written at exit.
    std::optional<std::filesystem::path> missing_data_report;
    // Window caption name; the APK file name when absent (library launches pass the game's name).
    std::optional<std::string> window_title;
    bool operator==(const GameRunOptions&) const = default;
};

// Validates the user-selected mount destination inside the external storage namespace.
OGPLAYCORE_API void ValidateExternalGuestDirectory(std::string_view path);

// Arguments start at the APK path, independently of the host entry mode.
[[nodiscard]] OGPLAYCORE_API GameRunOptions ParseGameRunArgs(std::span<const std::string_view> args);
[[nodiscard]] OGPLAYCORE_API std::vector<std::string> FormatGameRunArgs(const GameRunOptions& options);
OGPLAYCORE_API void ValidateGameRunOptions(const GameRunOptions& options);
OGPLAYCORE_API int RunGame(GameRunOptions options, const LogConfig& logging = {});

}  // namespace ogplay::app

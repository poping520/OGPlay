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
#include "ogplay/app/ui_text.h"
#include "ogplay/app/logging.h"
#include "ogplay/app/running_icon.h"

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
inline constexpr std::uint32_t kDefaultSurfaceWidth = 1280;
inline constexpr std::uint32_t kDefaultSurfaceHeight = 720;
// Guest-visible DisplayMetrics density; 160 dpi is Android's 1.0 baseline.
inline constexpr std::uint32_t kDefaultDensityDpi = 320;
inline constexpr std::uint32_t kMinimumDensityDpi = 72;
inline constexpr std::uint32_t kMaximumDensityDpi = 1280;

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
    // Host frame-rate cap in frames per second; 0 leaves frames unpaced.
    std::uint32_t max_fps{};
    std::uint32_t surface_width{kDefaultSurfaceWidth};
    std::uint32_t surface_height{kDefaultSurfaceHeight};
    std::uint32_t density_dpi{kDefaultDensityDpi};
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
    // Window/Dock icon (library launches pass the entry's icon.png): composed with the OGPlay mark in
    // window_icon_style; any failure keeps the default icon.
    std::optional<std::filesystem::path> window_icon;
    RunningIconStyle window_icon_style{RunningIconStyle::badge};
    // Host UI only; absent means the system language at process startup.
    std::optional<UiLanguage> ui_language;
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

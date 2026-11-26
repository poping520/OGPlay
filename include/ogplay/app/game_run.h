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

// How the game process adapts the surface to the display it opens on. Library presets (screen_resolution) use
// limit, or follow for screen_aspect = screen; sizes typed in by the user, and the CLI, use none.
enum class SurfaceAdaptation : std::uint8_t {
    none,    // the size as given
    limit,   // the short side at most the display's (aspect kept)
    follow,  // limit, then the long side at the display's aspect
};

// The corner of the presented picture that shows the frame-rate counter (library setting fps_overlay_position).
enum class OverlayCorner : std::uint8_t { top_left, top_right, bottom_left, bottom_right };

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
    // GUI-23: last-run.json, marked unfinished at start and replaced with the outcome at exit.
    std::optional<std::filesystem::path> run_report;
    // Window caption name; the APK file name when absent (library launches pass the game's name).
    std::optional<std::string> window_title;
    // Window/Dock icon (library launches pass the entry's icon.png): composed with the OGPlay mark in
    // window_icon_style; any failure keeps the default icon.
    std::optional<std::filesystem::path> window_icon;
    RunningIconStyle window_icon_style{RunningIconStyle::badge};
    // Host UI only; absent means the system language at process startup.
    std::optional<UiLanguage> ui_language;
    // Guest language; absent follows the resolved host UI language at startup.
    std::optional<UiLanguage> app_language;
    // CLI or game-specific dimensions suppress Manifest startup adaptation.
    bool surface_size_explicit{};
    // Applied before the window opens (a 20:9 phone following its display shows no side bars).
    SurfaceAdaptation surface_adaptation{SurfaceAdaptation::none};
    // Frames per second drawn over the presented picture in this corner; absent shows none.
    std::optional<OverlayCorner> fps_overlay;
    // Installed Play Store with no account: LVL fails, billing is unavailable; off removes Google packages.
    bool google_play{true};
    // Launcher network_policy: "disabled" (offline device), "loopback" or "allow" (host egress).
    std::string network{"disabled"};
    // Launcher tls_mode: "host" (host TLS and roots) or "guest" (API 19 libssl and bundled CAs).
    std::string tls{"host"};
    bool operator==(const GameRunOptions&) const = default;
};

// Validates the user-selected mount destination inside the external storage namespace.
OGPLAYCORE_API void ValidateExternalGuestDirectory(std::string_view path);

// Arguments start at the APK path, independently of the host entry mode.
[[nodiscard]] OGPLAYCORE_API GameRunOptions ParseGameRunArgs(std::span<const std::string_view> args);
[[nodiscard]] OGPLAYCORE_API std::vector<std::string> FormatGameRunArgs(const GameRunOptions& options);
OGPLAYCORE_API void ApplyStartupOrientation(GameRunOptions& options, std::int32_t screen_orientation);
// The long side for a short side at the aspect numerator:denominator (>= 1): the nearest length, made even
// (rounded up), at least the short side and at most kMaximumSurfaceDimension. Presets and display fitting share it.
[[nodiscard]] OGPLAYCORE_API std::uint32_t SurfaceLongSide(std::uint32_t short_side, std::uint64_t numerator,
                                                           std::uint64_t denominator);

struct SurfaceSize final {
    std::uint32_t width{};
    std::uint32_t height{};
    bool operator==(const SurfaceSize&) const = default;
};

// The surface adapted to a display (pixels, either orientation) as `mode` says; the orientation of the surface is
// kept. An empty display leaves it unchanged, and follow leaves a square surface or display at limit's result.
[[nodiscard]] OGPLAYCORE_API SurfaceSize AdaptSurfaceToDisplay(SurfaceSize surface, SurfaceSize display,
                                                               SurfaceAdaptation mode);
// "top-left", "top-right", "bottom-left", "bottom-right": the --fps-overlay argument and the library setting.
[[nodiscard]] OGPLAYCORE_API std::string_view OverlayCornerName(OverlayCorner corner) noexcept;
[[nodiscard]] OGPLAYCORE_API std::optional<OverlayCorner> ParseOverlayCorner(std::string_view name) noexcept;
OGPLAYCORE_API void ValidateGameRunOptions(const GameRunOptions& options);
OGPLAYCORE_API int RunGame(GameRunOptions options, const LogConfig& logging = {});

}  // namespace ogplay::app

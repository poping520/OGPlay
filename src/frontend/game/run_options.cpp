#include "ogplay/app/game_run.h"
#include "ogplay/core/text.h"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace ogplay::app {
namespace {
std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

constexpr std::uint16_t kDefaultMcpPort = 15971U;

std::uint64_t ParsePositive(const std::string_view text, const std::string_view option) {
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0) {
        throw std::invalid_argument(std::string(option) + " requires a positive integer");
    }
    return value;
}

constexpr std::uint64_t kMaximumFrameRateCap = 1000;

std::uint32_t ParseSupersampleFactor(const std::string_view text) {
    std::uint32_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < 1 || value > 4) {
        throw std::invalid_argument("--supersample requires an integer in 1..4");
    }
    return value;
}

std::uint16_t ParseMcpPort(const std::string_view text, const std::string_view option = "--mcp-port") {
    std::uint32_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0 || value > 65535U) {
        throw std::invalid_argument(std::string(option) + " requires an integer in 1..65535");
    }
    return static_cast<std::uint16_t>(value);
}

InterpreterBackend ParseDexVmInterpreter(const std::string_view text) {
    if (text == "switch") {
        return InterpreterBackend::switch_dispatch;
    }
    if (text == "threaded") {
        return InterpreterBackend::threaded;
    }
    throw std::invalid_argument("--dexvm-interpreter requires switch or threaded");
}

}  // namespace

void ApplyStartupOrientation(GameRunOptions& options, const std::int32_t screen_orientation) {
    // API19 portrait, sensorPortrait, reversePortrait and userPortrait only.
    const bool portrait =
        screen_orientation == 1 || screen_orientation == 7 || screen_orientation == 9 || screen_orientation == 12;
    if (portrait && !options.surface_size_explicit && options.surface_width > options.surface_height)
        std::swap(options.surface_width, options.surface_height);
}

GameRunOptions ParseGameRunArgs(std::span<const std::string_view> args) {
    if (args.empty()) throw std::invalid_argument("run-apk requires <apk>");
    GameRunOptions options;
    options.apk_path = Utf8Path(args.front());
    bool default_mcp{}, window_icon_style_seen{};
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view option{args[index]};
        if (option == "--profiles-dir" && index + 1 < args.size()) {
            options.profiles_directory = Utf8Path(args[++index]);
        } else if (option == "--external-dir" && index + 1 < args.size()) {
            if (options.external_directory.has_value()) {
                throw std::invalid_argument("run-apk accepts --external-dir only once");
            }
            options.external_directory = Utf8Path(args[++index]);
            if (options.external_directory->empty()) {
                throw std::invalid_argument("--external-dir requires a non-empty host directory");
            }
        } else if (option == "--external-guest-dir" && index + 1 < args.size()) {
            if (options.external_guest_directory.has_value()) {
                throw std::invalid_argument("run-apk accepts --external-guest-dir only once");
            }
            options.external_guest_directory = args[++index];
            if (options.external_guest_directory->empty()) {
                throw std::invalid_argument("--external-guest-dir requires a non-empty guest path");
            }
        } else if (option == "--obb" && index + 1 < args.size()) {
            if (options.obb_path.has_value()) {
                throw std::invalid_argument("run-apk accepts --obb only once");
            }
            options.obb_path = Utf8Path(args[++index]);
            if (options.obb_path->empty()) {
                throw std::invalid_argument("--obb requires a non-empty archive path");
            }
        } else if (option == "--exit-after-frames" && index + 1 < args.size()) {
            options.exit_after_frames = ParsePositive(args[++index], option);
        } else if (option == "--cpu-limit-parallelism") {
            options.cpu_config.execution.limit_parallelism = true;
        } else if (option == "--cpu-max-mticks-per-second" && index + 1 < args.size()) {
            const auto value = ParsePositive(args[++index], option);
            if (value > 10000U) throw std::invalid_argument("--cpu-max-mticks-per-second requires 1..10000");
            options.cpu_config.execution.max_mticks_per_second = static_cast<std::uint32_t>(value);
        } else if ((option == "--cpu-cores" || option == "--cpu-frequency-mhz") && index + 1 < args.size()) {
            const auto value = ParsePositive(args[++index], option);
            const auto maximum = option == "--cpu-cores" ? 32U : 10000U;
            if (value > maximum)
                throw std::invalid_argument(std::string(option) + " exceeds " + std::to_string(maximum));
            if (option == "--cpu-cores")
                options.cpu_config.cores = static_cast<std::uint32_t>(value);
            else
                options.cpu_config.frequency_mhz = static_cast<std::uint32_t>(value);
        } else if ((option == "--surface-width" || option == "--surface-height" ||
                    option == "--inherited-surface-width" || option == "--inherited-surface-height") &&
                   index + 1 < args.size()) {
            const auto value = ParsePositive(args[++index], option);
            if (value > kMaximumSurfaceDimension)
                throw std::invalid_argument(std::string(option) + " exceeds " +
                                            std::to_string(kMaximumSurfaceDimension));
            if (option == "--surface-width" || option == "--surface-height") options.surface_size_explicit = true;
            if (option == "--surface-width" || option == "--inherited-surface-width")
                options.surface_width = static_cast<std::uint32_t>(value);
            else
                options.surface_height = static_cast<std::uint32_t>(value);
        } else if (option == "--density-dpi" && index + 1 < args.size()) {
            const auto value = ParsePositive(args[++index], option);
            if (value < kMinimumDensityDpi || value > kMaximumDensityDpi)
                throw std::invalid_argument("--density-dpi requires an integer in 72..1280");
            options.density_dpi = static_cast<std::uint32_t>(value);
        } else if (option == "--supersample" && index + 1 < args.size()) {
            options.supersample_factor = ParseSupersampleFactor(args[++index]);
        } else if (option == "--max-fps" && index + 1 < args.size()) {
            const auto value = ParsePositive(args[++index], option);
            if (value > kMaximumFrameRateCap) throw std::invalid_argument("--max-fps requires an integer in 1..1000");
            options.max_fps = static_cast<std::uint32_t>(value);
        } else if (option == "--mcp-port" && index + 1 < args.size()) {
            if (options.mcp_port.has_value()) {
                throw std::invalid_argument("run-apk accepts --mcp-port only once");
            }
            options.mcp_port = ParseMcpPort(args[++index]);
        } else if (option == "--key-editor-port" && index + 1 < args.size()) {
            if (options.key_editor_port.has_value()) {
                throw std::invalid_argument("run-apk accepts --key-editor-port only once");
            }
            options.key_editor_port = ParseMcpPort(args[++index], "--key-editor-port");
        } else if (option == "--mcp") {
            if (default_mcp) {
                throw std::invalid_argument("run-apk accepts --mcp only once");
            }
            default_mcp = true;
        } else if (option == "--mcp-manual-step") {
            if (options.mcp_manual_step) {
                throw std::invalid_argument("run-apk accepts --mcp-manual-step only once");
            }
            options.mcp_manual_step = true;
        } else if (option == "--diag") {
            options.diagnostics_enabled = true;
        } else if (option == "--diag-dir" && index + 1 < args.size()) {
            options.diagnostic_directory = Utf8Path(args[++index]);
            if (options.diagnostic_directory.empty()) {
                throw std::invalid_argument("--diag-dir requires a non-empty directory");
            }
            options.diagnostics_enabled = true;
        } else if (option == "--diag-on-teardown-timeout" && index + 1 < args.size()) {
            const auto seconds = ParsePositive(args[++index], option);
            if (seconds > 86'400U) {
                throw std::invalid_argument("--diag-on-teardown-timeout must not exceed 86400 seconds");
            }
            options.diagnostic_teardown_timeout =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(seconds));
            options.diagnostics_enabled = true;
        } else if (option.starts_with("--diag-on-teardown-timeout=")) {
            const auto seconds = ParsePositive(option.substr(std::string_view{"--diag-on-teardown-timeout="}.size()),
                                               "--diag-on-teardown-timeout");
            if (seconds > 86'400U) {
                throw std::invalid_argument("--diag-on-teardown-timeout must not exceed 86400 seconds");
            }
            options.diagnostic_teardown_timeout =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(seconds));
            options.diagnostics_enabled = true;
        } else if (option == "--dexvm-interpreter" && index + 1 < args.size()) {
            if (options.dexvm_interpreter.has_value()) {
                throw std::invalid_argument("run-apk accepts --dexvm-interpreter only once");
            }
            options.dexvm_interpreter = ParseDexVmInterpreter(args[++index]);
        } else if (option == "--sandbox-dir" && index + 1 < args.size()) {
            const std::string_view value{args[++index]};
            if (value.empty()) {
                throw std::invalid_argument("--sandbox-dir requires a non-empty host directory");
            }
            options.sandbox_directory = Utf8Path(value);
        } else if (option == "--ephemeral-sandbox") {
            options.ephemeral_sandbox = true;
        } else if (option == "--library-root" && index + 1 < args.size()) {
            options.library_root = Utf8Path(args[++index]);
        } else if (option == "--installation-id" && index + 1 < args.size()) {
            options.installation_id = args[++index];
        } else if (option == "--preflight") {
            options.preflight = true;
        } else if (option == "--input-config" && index + 1 < args.size()) {
            if (options.input_config_path.has_value()) {
                throw std::invalid_argument("run-apk accepts --input-config only once");
            }
            options.input_config_path = Utf8Path(args[++index]);
            if (options.input_config_path->empty()) throw std::invalid_argument("--input-config requires a file path");
        } else if (option == "--survey-gaps" && index + 1 < args.size()) {
            if (options.survey_gaps_output.has_value()) {
                throw std::invalid_argument("run-apk accepts --survey-gaps only once");
            }
            options.survey_gaps_output = Utf8Path(args[++index]);
        } else if (option == "--missing-data-report" && index + 1 < args.size()) {
            if (options.missing_data_report.has_value())
                throw std::invalid_argument("run-apk accepts --missing-data-report only once");
            options.missing_data_report = Utf8Path(args[++index]);
        } else if (option == "--ui-language" && index + 1 < args.size()) {
            if (options.ui_language) throw std::invalid_argument("run-apk accepts --ui-language only once");
            options.ui_language = ParseUiLanguage(args[++index]);
            if (!options.ui_language) throw std::invalid_argument("--ui-language requires zh-CN or en-US");
        } else if (option == "--window-title" && index + 1 < args.size()) {
            if (options.window_title.has_value())
                throw std::invalid_argument("run-apk accepts --window-title only once");
            options.window_title = std::string(args[++index]);
        } else if (option == "--window-icon" && index + 1 < args.size()) {
            if (options.window_icon.has_value()) throw std::invalid_argument("run-apk accepts --window-icon only once");
            options.window_icon = Utf8Path(args[++index]);
        } else if (option == "--window-icon-style" && index + 1 < args.size()) {
            if (window_icon_style_seen) throw std::invalid_argument("run-apk accepts --window-icon-style only once");
            const auto style = ParseRunningIconStyle(args[++index]);
            if (!style) throw std::invalid_argument("--window-icon-style must be badge or cradle");
            options.window_icon_style = *style;
            window_icon_style_seen = true;
        } else {
            throw std::invalid_argument("unknown or incomplete run-apk option: " + std::string(option));
        }
    }
    if (window_icon_style_seen && !options.window_icon.has_value())
        throw std::invalid_argument("--window-icon-style requires --window-icon");
    if (options.ephemeral_sandbox && options.sandbox_directory.has_value()) {
        throw std::invalid_argument("--ephemeral-sandbox and --sandbox-dir cannot be combined");
    }
    if (options.external_guest_directory.has_value() && !options.external_directory.has_value()) {
        throw std::invalid_argument("--external-guest-dir requires --external-dir");
    }
    if (default_mcp && options.mcp_port.has_value()) {
        throw std::invalid_argument("--mcp and --mcp-port cannot be combined");
    }
    if (options.preflight && default_mcp) {
        throw std::invalid_argument("--mcp cannot be combined with --preflight");
    }
    if (options.preflight && options.mcp_port.has_value()) {
        throw std::invalid_argument("--mcp-port cannot be combined with --preflight");
    }
    if (options.mcp_manual_step && !default_mcp && !options.mcp_port.has_value()) {
        throw std::invalid_argument("--mcp-manual-step requires --mcp or --mcp-port");
    }
    if (options.mcp_manual_step && options.preflight) {
        throw std::invalid_argument("--mcp-manual-step cannot be combined with --preflight");
    }
    if (options.key_editor_port.has_value() && (default_mcp || options.mcp_port.has_value())) {
        throw std::invalid_argument("--key-editor-port cannot be combined with MCP; the editor shares the MCP port");
    }
    if (options.key_editor_port.has_value() && options.preflight) {
        throw std::invalid_argument("--key-editor-port cannot be combined with --preflight");
    }
    if (default_mcp) options.mcp_port = kDefaultMcpPort;

    ValidateGameRunOptions(options);
    return options;
}

void ValidateGameRunOptions(const GameRunOptions& o) {
    if (o.ui_language) static_cast<void>(UiLanguageTag(*o.ui_language));
    if (o.apk_path.empty()) throw std::invalid_argument("run-apk requires <apk>");
    if (o.surface_width == 0 || o.surface_width > kMaximumSurfaceDimension || o.surface_height == 0 ||
        o.surface_height > kMaximumSurfaceDimension)
        throw std::invalid_argument("surface dimensions must be in 1..4096");
    if (o.density_dpi < kMinimumDensityDpi || o.density_dpi > kMaximumDensityDpi)
        throw std::invalid_argument("--density-dpi requires 72..1280");
    if (o.supersample_factor < 1 || o.supersample_factor > 4)
        throw std::invalid_argument("--supersample requires 1..4");
    if (o.max_fps > kMaximumFrameRateCap) throw std::invalid_argument("--max-fps requires 1..1000");
    if (o.cpu_config.cores < 1 || o.cpu_config.cores > 32 || o.cpu_config.frequency_mhz < 1 ||
        o.cpu_config.frequency_mhz > 10000 || o.cpu_config.execution.max_mticks_per_second > 10000)
        throw std::invalid_argument("invalid guest CPU configuration");
    if ((o.mcp_port && *o.mcp_port == 0) || (o.key_editor_port && *o.key_editor_port == 0))
        throw std::invalid_argument("port requires 1..65535");
    if (o.ephemeral_sandbox && o.sandbox_directory)
        throw std::invalid_argument("--ephemeral-sandbox and --sandbox-dir cannot be combined");
    if (o.external_guest_directory && !o.external_directory)
        throw std::invalid_argument("--external-guest-dir requires --external-dir");
    if (o.preflight && (o.mcp_port || o.key_editor_port || o.mcp_manual_step))
        throw std::invalid_argument("transport cannot be combined with --preflight");
    if (o.mcp_manual_step && !o.mcp_port) throw std::invalid_argument("--mcp-manual-step requires --mcp or --mcp-port");
    if (o.key_editor_port && o.mcp_port) throw std::invalid_argument("--key-editor-port cannot be combined with MCP");
    if (o.exit_after_frames && *o.exit_after_frames == 0)
        throw std::invalid_argument("--exit-after-frames requires a positive integer");
    if (o.diagnostic_teardown_timeout &&
        (o.diagnostic_teardown_timeout->count() <= 0 || o.diagnostic_teardown_timeout->count() > 86400000 ||
         o.diagnostic_teardown_timeout->count() % 1000 != 0))
        throw std::invalid_argument("diagnostic timeout requires whole seconds in 1..86400");
    if (o.diagnostic_directory.empty()) throw std::invalid_argument("--diag-dir requires a non-empty directory");
    if (!o.diagnostics_enabled && o.diagnostic_directory != std::filesystem::path(".local/diagnostics"))
        throw std::invalid_argument("custom diagnostic directory requires diagnostics");
    if (o.diagnostic_teardown_timeout && !o.diagnostics_enabled)
        throw std::invalid_argument("diagnostic timeout requires diagnostics");
    if (o.dexvm_interpreter && *o.dexvm_interpreter != InterpreterBackend::switch_dispatch &&
        *o.dexvm_interpreter != InterpreterBackend::threaded)
        throw std::invalid_argument("invalid interpreter");
    for (const auto* path :
         {&o.external_directory, &o.obb_path, &o.profiles_directory, &o.survey_gaps_output, &o.sandbox_directory,
          &o.input_config_path, &o.library_root, &o.missing_data_report, &o.window_icon})
        if (*path && (*path)->empty()) throw std::invalid_argument("option requires a non-empty path");
    if (o.external_guest_directory) ValidateExternalGuestDirectory(*o.external_guest_directory);
    if (o.installation_id && o.installation_id->empty()) throw std::invalid_argument("installation id is empty");
    if (o.library_root && !o.installation_id) throw std::invalid_argument("--library-root requires --installation-id");
    if (o.window_title &&
        (o.window_title->empty() || o.window_title->size() > 256 || !core::IsValidUtf8(*o.window_title) ||
         std::any_of(o.window_title->begin(), o.window_title->end(),
                     [](unsigned char c) { return c < 0x20 || c == 0x7f; })))
        throw std::invalid_argument("--window-title requires 1..256 bytes of printable UTF-8");
}

std::vector<std::string> FormatGameRunArgs(const GameRunOptions& o) {
    ValidateGameRunOptions(o);
    const auto path = [](const std::filesystem::path& value) {
        auto bytes = value.generic_u8string();
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    std::vector<std::string> args{path(o.apk_path)};
    const auto add = [&](std::string key, std::string value) {
        args.push_back(std::move(key));
        args.push_back(std::move(value));
    };
    const auto optional_path = [&](const char* key, const auto& value) {
        if (value) add(key, path(*value));
    };
    optional_path("--profiles-dir", o.profiles_directory);
    optional_path("--external-dir", o.external_directory);
    if (o.external_guest_directory) add("--external-guest-dir", *o.external_guest_directory);
    optional_path("--obb", o.obb_path);
    if (o.exit_after_frames) add("--exit-after-frames", std::to_string(*o.exit_after_frames));
    add("--cpu-cores", std::to_string(o.cpu_config.cores));
    add("--cpu-frequency-mhz", std::to_string(o.cpu_config.frequency_mhz));
    if (o.cpu_config.execution.limit_parallelism) args.emplace_back("--cpu-limit-parallelism");
    if (o.cpu_config.execution.max_mticks_per_second)
        add("--cpu-max-mticks-per-second", std::to_string(o.cpu_config.execution.max_mticks_per_second));
    if (o.surface_size_explicit || o.surface_width != kDefaultSurfaceWidth)
        add(o.surface_size_explicit ? "--surface-width" : "--inherited-surface-width", std::to_string(o.surface_width));
    if (o.surface_size_explicit || o.surface_height != kDefaultSurfaceHeight)
        add(o.surface_size_explicit ? "--surface-height" : "--inherited-surface-height",
            std::to_string(o.surface_height));
    if (o.density_dpi != kDefaultDensityDpi) add("--density-dpi", std::to_string(o.density_dpi));
    if (o.supersample_factor != 1) add("--supersample", std::to_string(o.supersample_factor));
    if (o.max_fps) add("--max-fps", std::to_string(o.max_fps));
    if (o.mcp_port) add("--mcp-port", std::to_string(*o.mcp_port));
    if (o.key_editor_port) add("--key-editor-port", std::to_string(*o.key_editor_port));
    if (o.mcp_manual_step) args.emplace_back("--mcp-manual-step");
    if (o.diagnostics_enabled) {
        args.emplace_back("--diag");
        add("--diag-dir", path(o.diagnostic_directory));
    }
    if (o.diagnostic_teardown_timeout)
        add("--diag-on-teardown-timeout", std::to_string(o.diagnostic_teardown_timeout->count() / 1000));
    if (o.dexvm_interpreter)
        add("--dexvm-interpreter", *o.dexvm_interpreter == InterpreterBackend::threaded ? "threaded" : "switch");
    optional_path("--sandbox-dir", o.sandbox_directory);
    optional_path("--library-root", o.library_root);
    if (o.installation_id) add("--installation-id", *o.installation_id);
    if (o.ephemeral_sandbox) args.emplace_back("--ephemeral-sandbox");
    if (o.preflight) args.emplace_back("--preflight");
    optional_path("--input-config", o.input_config_path);
    optional_path("--survey-gaps", o.survey_gaps_output);
    optional_path("--missing-data-report", o.missing_data_report);
    if (o.ui_language) add("--ui-language", std::string(UiLanguageTag(*o.ui_language)));
    if (o.window_title) add("--window-title", *o.window_title);
    optional_path("--window-icon", o.window_icon);
    if (o.window_icon && o.window_icon_style != RunningIconStyle::badge)
        add("--window-icon-style", std::string(RunningIconStyleName(o.window_icon_style)));
    return args;
}
}  // namespace ogplay::app

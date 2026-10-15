#include "run_game.h"
#include <charconv>
#include <stdexcept>

namespace ogplay::frontend {
namespace {
std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(text.data()), text.size()));
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

std::uint32_t ParseSupersampleFactor(const std::string_view text) {
    std::uint32_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value < 1 || value > 4) {
        throw std::invalid_argument("--supersample requires an integer in 1..4");
    }
    return value;
}

std::uint16_t ParseMcpPort(const std::string_view text, const std::string_view option = "--mcp-port") {
    std::uint32_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value == 0 || value > 65535U) {
        throw std::invalid_argument(std::string(option) + " requires an integer in 1..65535");
    }
    return static_cast<std::uint16_t>(value);
}

runtime::dexvm::InterpreterBackend ParseDexVmInterpreter(
    const std::string_view text) {
    if (text == "switch") {
        return runtime::dexvm::InterpreterBackend::switch_dispatch;
    }
    if (text == "threaded") {
        return runtime::dexvm::InterpreterBackend::threaded;
    }
    throw std::invalid_argument(
        "--dexvm-interpreter requires switch or threaded");
}

}  // namespace

GameRunOptions ParseGameRunOptions(std::span<const std::string_view> args) {
    if (args.empty()) throw std::invalid_argument("run-apk requires <apk>");
    GameRunOptions options;
    options.apk_path = Utf8Path(args.front());
    bool default_mcp{};
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view option{args[index]};
        if (option == "--profiles-dir" && index + 1 < args.size()) {
            options.profiles_directory = Utf8Path(args[++index]);
        } else if (option == "--external-dir" && index + 1 < args.size()) {
            if (options.external_directory.has_value()) {
                throw std::invalid_argument(
                    "run-apk accepts --external-dir only once");
            }
            options.external_directory = Utf8Path(args[++index]);
            if (options.external_directory->empty()) {
                throw std::invalid_argument(
                    "--external-dir requires a non-empty host directory");
            }
        } else if (option == "--external-guest-dir" && index + 1 < args.size()) {
            if (options.external_guest_directory.has_value()) {
                throw std::invalid_argument(
                    "run-apk accepts --external-guest-dir only once");
            }
            options.external_guest_directory = args[++index];
            if (options.external_guest_directory->empty()) {
                throw std::invalid_argument(
                    "--external-guest-dir requires a non-empty guest path");
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
            if (option == "--cpu-cores") options.cpu_config.cores = static_cast<std::uint32_t>(value);
            else options.cpu_config.frequency_mhz = static_cast<std::uint32_t>(value);
        } else if (option == "--supersample" && index + 1 < args.size()) {
            options.supersample_factor = ParseSupersampleFactor(args[++index]);
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
                throw std::invalid_argument(
                    "run-apk accepts --mcp-manual-step only once");
            }
            options.mcp_manual_step = true;
        } else if (option == "--diag") {
            options.diagnostics_enabled = true;
        } else if (option == "--diag-dir" && index + 1 < args.size()) {
            options.diagnostic_directory = Utf8Path(args[++index]);
            if (options.diagnostic_directory.empty()) {
                throw std::invalid_argument(
                    "--diag-dir requires a non-empty directory");
            }
            options.diagnostics_enabled = true;
        } else if (option == "--diag-on-teardown-timeout" &&
                   index + 1 < args.size()) {
            const auto seconds = ParsePositive(args[++index], option);
            if (seconds > 86'400U) {
                throw std::invalid_argument(
                    "--diag-on-teardown-timeout must not exceed 86400 seconds");
            }
            options.diagnostic_teardown_timeout = std::chrono::duration_cast<
                std::chrono::milliseconds>(std::chrono::seconds(seconds));
            options.diagnostics_enabled = true;
        } else if (option.starts_with("--diag-on-teardown-timeout=")) {
            const auto seconds = ParsePositive(
                option.substr(std::string_view{"--diag-on-teardown-timeout="}.size()),
                "--diag-on-teardown-timeout");
            if (seconds > 86'400U) {
                throw std::invalid_argument(
                    "--diag-on-teardown-timeout must not exceed 86400 seconds");
            }
            options.diagnostic_teardown_timeout = std::chrono::duration_cast<
                std::chrono::milliseconds>(std::chrono::seconds(seconds));
            options.diagnostics_enabled = true;
        } else if (option == "--dexvm-interpreter" && index + 1 < args.size()) {
            if (options.dexvm_interpreter.has_value()) {
                throw std::invalid_argument(
                    "run-apk accepts --dexvm-interpreter only once");
            }
            options.dexvm_interpreter = ParseDexVmInterpreter(args[++index]);
        } else if (option == "--sandbox-dir" && index + 1 < args.size()) {
            const std::string_view value{args[++index]};
            if (value.empty()) {
                throw std::invalid_argument(
                    "--sandbox-dir requires a non-empty host directory");
            }
            options.sandbox_directory = Utf8Path(value);
        } else if (option == "--ephemeral-sandbox") {
            options.ephemeral_sandbox = true;
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
                throw std::invalid_argument(
                    "run-apk accepts --survey-gaps only once");
            }
            options.survey_gaps_output = Utf8Path(args[++index]);
        } else {
            throw std::invalid_argument("unknown or incomplete run-apk option: " +
                                        std::string(option));
        }
    }
    if (options.ephemeral_sandbox && options.sandbox_directory.has_value()) {
        throw std::invalid_argument(
            "--ephemeral-sandbox and --sandbox-dir cannot be combined");
    }
    if (options.external_guest_directory.has_value() && !options.external_directory.has_value()) {
        throw std::invalid_argument(
            "--external-guest-dir requires --external-dir");
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
        throw std::invalid_argument(
            "--mcp-manual-step requires --mcp or --mcp-port");
    }
    if (options.mcp_manual_step && options.preflight) {
        throw std::invalid_argument(
            "--mcp-manual-step cannot be combined with --preflight");
    }
    if (options.key_editor_port.has_value() && (default_mcp || options.mcp_port.has_value())) {
        throw std::invalid_argument("--key-editor-port cannot be combined with MCP; the editor shares the MCP port");
    }
    if (options.key_editor_port.has_value() && options.preflight) {
        throw std::invalid_argument("--key-editor-port cannot be combined with --preflight");
    }
    if (default_mcp) options.mcp_port = kDefaultMcpPort;

    return options;
}

}  // namespace ogplay::frontend

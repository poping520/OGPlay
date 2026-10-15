#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ogplay/agent/control_service.h"
#include "ogplay/agent/json_rpc.h"
#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/frontend/gui.h"
#include "ogplay/hal/clock.h"
#include "ogplay/hal/host_environment.h"
#include "ogplay/session/session.h"
#include "ogplay/runtime/debug/stall_diagnostics.h"

#include "../game/run_game.h"
#include "commands.h"
#include <vector>

namespace {

std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

void Write(FILE* stream, const std::string_view text) {
    static_cast<void>(std::fwrite(text.data(), sizeof(char), text.size(), stream));
}

[[nodiscard]] std::string FormatError(const std::string_view message) {
    std::string rendered{"\nerror [ogplay]\n  "};
    for (const char character : message) {
        rendered += character;
        if (character == '\n') rendered += "  ";
    }
    rendered += '\n';
    return rendered;
}

int Usage(FILE* stream = stderr, int code = 2) {
    Write(stream, "usage: ogplay-cli --version | capabilities [path] | agent <method> | agent-stdio\n"
                  "       ogplay-cli diag snapshot --pid <pid> [--diag-dir <dir>]\n"
                  "       ogplay-cli run-apk <apk> [--profiles-dir <dir>] "
                  "[--external-dir <host-dir> [--external-guest-dir </sdcard/...>]] "
                  "[--obb <archive>] "
                  "[--preflight] [--supersample <1..4>] "
                  "[--cpu-cores <1..32>] [--cpu-frequency-mhz <1..10000>] "
                  "[--cpu-limit-parallelism] [--cpu-max-mticks-per-second <1..10000>] "
                  "[--exit-after-frames <count>] [--mcp | --mcp-port <1..65535>] "
                  "[--mcp-manual-step] [--key-editor-port <1..65535>] "
                  "[--diag] [--diag-dir <dir>] "
                  "[--diag-on-teardown-timeout <seconds>] "
                  "[--dexvm-interpreter <switch|threaded>] [--input-config <file>] "
                  "[--sandbox-dir <host-dir> --installation-id <id> | --ephemeral-sandbox]\n");
    return code;
}

}  // namespace

int ogplay::frontend::RunCli(const int argc, const char* const argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        Write(stdout, "OGPlay " OGPLAY_VERSION "\n");
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--help") return Usage(stdout, 0);
    if (argc < 2) return Usage();

    ogplay::core::Logger logger;
    logger.AddSink(std::make_shared<ogplay::core::ConsoleSink>(stderr),
                   ogplay::core::LogLevel::info);
    try {
        if (std::string_view(argv[1]) == "run-apk") {
            std::vector<std::string_view> args(argv + 2, argv + argc);
            return RunGame(ParseGameRunOptions(args), logger);
        }
        if (argc >= 5 && std::string_view(argv[1]) == "diag" &&
            std::string_view(argv[2]) == "snapshot") {
            std::optional<std::uint64_t> pid;
            std::filesystem::path directory{".local/diagnostics"};
            for (int index = 3; index < argc; ++index) {
                const std::string_view option{argv[index]};
                if (option == "--pid" && index + 1 < argc) {
                    const std::string value{argv[++index]};
                    std::size_t consumed{};
                    const auto parsed = std::stoull(value, &consumed);
                    if (consumed != value.size() || parsed == 0U) {
                        throw std::invalid_argument("--pid requires a positive integer");
                    }
                    pid = parsed;
                } else if (option == "--diag-dir" && index + 1 < argc) {
                    directory = Utf8Path(argv[++index]);
                } else {
                    throw std::invalid_argument(
                        "unknown or incomplete diag snapshot option: " +
                        std::string(option));
                }
            }
            if (!pid) throw std::invalid_argument("diag snapshot requires --pid");
            const auto trigger =
                ogplay::runtime::debug::TriggerExternalDiagnostic(*pid, directory);
            Write(stdout, "OGPlay: requested diagnostic snapshot via " + trigger + "\n");
            return 0;
        }
        if (!(std::string_view(argv[1]) == "capabilities" && argc <= 3) &&
            !(std::string_view(argv[1]) == "agent" && argc == 3) &&
            !(std::string_view(argv[1]) == "agent-stdio" && argc == 2)) return Usage();
        const auto ledger_path = argc >= 3 && std::string_view(argv[1]) == "capabilities"
                                     ? Utf8Path(argv[2])
                                     : ogplay::hal::HostExecutableDirectory() / "data" / "capabilities.toml";
        auto ledger = ogplay::core::CapabilityLedger::Load(ledger_path);
        ogplay::hal::FixedStepClock clock(1'000, 60'000);
        ogplay::session::Session session(clock);
        ogplay::agent::ControlService service(ledger, logger, session);

        if (std::string_view(argv[1]) == "capabilities") {
            Write(stdout, service.Request("hle.capabilities").json + "\n");
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "agent") {
            const auto response = service.Request(argv[2]);
            Write(response.ok ? stdout : stderr, response.json + "\n");
            return response.ok ? 0 : 1;
        }
        if (argc == 2 && std::string_view(argv[1]) == "agent-stdio") {
            ogplay::agent::JsonRpcAdapter adapter(service);
            std::string line;
            while (std::getline(std::cin, line)) Write(stdout, adapter.Handle(line) + "\n");
            return 0;
        }
    } catch (const std::exception& error) {
        Write(stderr, FormatError(error.what()));
        return 1;
    }
    return Usage();
}

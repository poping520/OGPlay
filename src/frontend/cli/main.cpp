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
#include "ogplay/session/session.h"
#include "ogplay/runtime/debug/stall_diagnostics.h"

#include "run_apk.h"

namespace {

void Write(FILE* stream, const std::string_view text) {
    static_cast<void>(std::fwrite(text.data(), sizeof(char), text.size(), stream));
}

int Usage() {
    Write(stderr, "usage: ogplay --version | capabilities [path] | agent <method> | agent-stdio\n"
                  "       ogplay gui [--library-root <dir>] [--smoke-frames <count>]\n"
                  "       ogplay diag snapshot --pid <pid> [--diag-dir <dir>]\n"
                  "       ogplay run-apk <apk> [--profiles-dir <dir>] "
                  "[--external-dir <host-dir>] "
                  "[--preflight] [--supersample <1..4>] "
                  "[--exit-after-frames <count>] [--mcp | --mcp-port <1..65535>] "
                  "[--mcp-manual-step] "
                  "[--diag] [--diag-dir <dir>] "
                  "[--diag-on-teardown-timeout <seconds>] "
                  "[--dexvm-interpreter <switch|threaded>] "
                  "[--sandbox-dir <host-dir> | --ephemeral-sandbox]\n");
    return 2;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        Write(stdout, "OGPlay " OGPLAY_VERSION "\n");
        return 0;
    }
    if (argc < 2) return Usage();

    ogplay::core::Logger logger;
    logger.AddSink(std::make_shared<ogplay::core::ConsoleSink>(stderr),
                   ogplay::core::LogLevel::info);
    try {
        if (std::string_view(argv[1]) == "run-apk") {
            return ogplay::frontend::RunApkCommand(argc, argv, logger);
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
                    directory = argv[++index];
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
#if OGPLAY_HAS_GUI
        if (std::string_view(argv[1]) == "gui") {
            return ogplay::frontend::RunGuiCommand(argc, argv, logger);
        }
#endif
        const auto ledger_path = argc >= 3 && std::string_view(argv[1]) == "capabilities"
                                     ? std::filesystem::path(argv[2])
                                     : std::filesystem::path(OGPLAY_SOURCE_DIR) / "capabilities.toml";
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
        Write(stderr, std::string("ogplay: ") + error.what() + "\n");
        return 1;
    }
    return Usage();
}

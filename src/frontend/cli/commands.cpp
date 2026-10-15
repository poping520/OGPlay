#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ogplay/app/agent.h"
#include "ogplay/app/host.h"
#include "ogplay/app/diagnostics.h"

#include "ogplay/app/game_run.h"
#include "library_commands.h"
#include "output.h"
#include "commands.h"
#include <vector>

using ogplay::cli::Write;
using ogplay::cli::FormatError;
namespace {

std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

int Usage(FILE* stream = stderr, int code = 2) {
    Write(stream, "usage: ogplay-cli --version | capabilities [path] | agent <method> | agent-stdio\n"
                  "       ogplay-cli library [--library-root <dir>] <command>\n"
                  "         list | inspect <apk>\n"
                  "         import <apk> [--external-dir <dir>] [--new-instance]\n"
                  "         remove <installation-id> --yes\n"
                  "         settings get <installation-id>\n"
                  "         settings set <installation-id> <key>=<value>... [--unset <key>]...\n"
                  "         run <installation-id> [--mode normal|preflight|diagnostic]\n"
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

int ogplay::cli::RunCli(const int argc, const char* const argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        Write(stdout, "OGPlay " OGPLAY_VERSION "\n");
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--help") return Usage(stdout, 0);
    if (argc < 2) return Usage();

    try {
        if (std::string_view(argv[1]) == "library") return RunLibraryCommand(std::span(argv + 2, static_cast<std::size_t>(argc - 2)));
        if (std::string_view(argv[1]) == "run-apk") {
            std::vector<std::string_view> args(argv + 2, argv + argc);
            return app::RunGame(app::ParseGameRunArgs(args));
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
                ogplay::app::RequestDiagnosticSnapshot(*pid, directory);
            Write(stdout, "OGPlay: requested diagnostic snapshot via " + trigger + "\n");
            return 0;
        }
        if (!(std::string_view(argv[1]) == "capabilities" && argc <= 3) &&
            !(std::string_view(argv[1]) == "agent" && argc == 3) &&
            !(std::string_view(argv[1]) == "agent-stdio" && argc == 2)) return Usage();
        const auto ledger_path = argc >= 3 && std::string_view(argv[1]) == "capabilities"
                                     ? Utf8Path(argv[2])
                                     : ogplay::app::ExecutablePath().parent_path() / "data" / "capabilities.toml";
        ogplay::app::AgentConsole console(ledger_path);

        if (std::string_view(argv[1]) == "capabilities") {
            Write(stdout, console.Request("hle.capabilities").json + "\n");
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "agent") {
            const auto response = console.Request(argv[2]);
            Write(response.ok ? stdout : stderr, response.json + "\n");
            return response.ok ? 0 : 1;
        }
        if (argc == 2 && std::string_view(argv[1]) == "agent-stdio") {
            std::string line;
            while (std::getline(std::cin, line)) Write(stdout, console.HandleJsonRpc(line) + "\n");
            return 0;
        }
    } catch (const std::exception& error) {
        Write(stderr, FormatError(error.what()));
        return 1;
    } catch (...) {
        Write(stderr, FormatError("unknown CLI failure"));
        return 1;
    }
    return Usage();
}

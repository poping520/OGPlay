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
#include "ogplay/app/dex_run.h"

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

constexpr std::string_view kSynopsis =
    "usage: ogplay-cli <command> [arguments]\n"
    "\n"
    "Commands:\n"
    "  dexvm dump-surface [--bootdex <jar>]  Export linked platform JSON\n"
    "  dexvm run-dex <dex|jar> [--class <name>] [--backend switch|threaded]\n"
    "        [--enable-assertions] [--ledger-json <file>] [-- args...]\n"
    "                                Run a standalone main like dalvikvm;\n"
    "                                exit 0/1(uncaught)/System.exit, 70 on VM error\n"
    "  run-apk <apk> [options]       Run an APK directly\n"
    "  library [--library-root <dir>] <subcommand>\n"
    "                                Manage the game library\n"
    "  diag snapshot --pid <pid> [--diag-dir <dir>]\n"
    "                                Request a snapshot from a running game\n"
    "  capabilities [path]           Print the capability ledger as JSON\n"
    "  agent <method>                Send one agent request, print the reply\n"
    "  agent-stdio                   Serve JSON-RPC over stdin/stdout\n"
    "  --version                     Print the version\n"
    "  -h, --help                    Show the full help\n";

constexpr std::string_view kDetails =
    "\n"
    "Library subcommands:\n"
    "  list                          List installations\n"
    "  inspect <apk>                 Inspect an APK without importing it\n"
    "  import <apk> [--external-dir <dir> [--link-external]] [--new-instance]\n"
    "                                Import an APK; the data directory is copied\n"
    "                                into the library unless --link-external;\n"
    "                                --new-instance adds a copy\n"
    "  remove <installation-id> --yes [--delete-saves]\n"
    "                                Remove an installation; saves are kept and\n"
    "                                reused by the next import unless deleted\n"
    "  data-hint <installation-id> [--external-dir <dir>]\n"
    "                                Suggest a data mount from the last run\n"
    "  settings get <installation-id>\n"
    "  settings set <installation-id> <key>=<value>... [--unset <key>]...\n"
    "  run <installation-id> [--mode normal|preflight|diagnostic]\n"
    "\n"
    "run-apk options:\n"
    "  Data\n"
    "    --profiles-dir <dir>        Profile directory\n"
    "    --external-dir <host-dir>   Host directory exposed as external storage\n"
    "    --external-guest-dir </sdcard/...>\n"
    "                                Guest mount point; needs --external-dir\n"
    "    --obb <archive>             OBB expansion archive\n"
    "    --input-config <file>       Control config (input.toml)\n"
    "    --missing-data-report <file>\n"
    "                                Write /sdcard reads that found nothing (JSON)\n"
    "  Window\n"
    "    --window-title <name>       Name shown in the window caption (default: APK file name)\n"
    "  Sandbox\n"
    "    --sandbox-dir <host-dir>    Sandbox root directory\n"
    "    --installation-id <id>      Installation instance to run\n"
    "    --ephemeral-sandbox         Throw-away sandbox; excludes --sandbox-dir\n"
    "  Execution\n"
    "    --preflight                 Check APK and dependencies, then exit\n"
    "    --exit-after-frames <count> Exit after <count> frames\n"
    "    --surface-width <1..4096>  Logical width (default 800)\n"
    "    --surface-height <1..4096> Logical height (default 480)\n"
    "    --supersample <1..4>        Render scale factor (default 1)\n"
    "    --dexvm-interpreter <switch|threaded>\n"
    "                                DexVM interpreter override\n"
    "  Guest CPU\n"
    "    --cpu-cores <1..32>         Core count (default 1)\n"
    "    --cpu-frequency-mhz <1..10000>\n"
    "                                Core frequency (default 1000)\n"
    "    --cpu-limit-parallelism     Run at most --cpu-cores threads at once\n"
    "    --cpu-max-mticks-per-second <1..10000>\n"
    "                                Cap total ticks per second, in millions\n"
    "  Automation (not with --preflight)\n"
    "    --mcp                       Serve MCP on the default port 15971\n"
    "    --mcp-port <1..65535>       Serve MCP on the given port\n"
    "    --mcp-manual-step           Step frames over MCP; needs an MCP option\n"
    "    --key-editor-port <1..65535>\n"
    "                                Serve the key editor; excludes MCP\n"
    "  Diagnostics\n"
    "    --diag                      Enable diagnostics\n"
    "    --diag-dir <dir>            Output directory; implies --diag\n"
    "                                (default .local/diagnostics)\n"
    "    --diag-on-teardown-timeout <seconds>\n"
    "                                Snapshot when teardown stalls; implies --diag\n";

int Help() {
    Write(stdout, kSynopsis);
    Write(stdout, kDetails);
    return 0;
}

int Usage() {
    Write(stderr, kSynopsis);
    Write(stderr, "\nRun 'ogplay-cli --help' for subcommands and run-apk options.\n");
    return 2;
}

}  // namespace

int ogplay::cli::RunCli(const int argc, const char* const argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        Write(stdout, "OGPlay " OGPLAY_VERSION "\n");
        return 0;
    }
    if (argc == 2 && (std::string_view(argv[1]) == "--help" ||
                      std::string_view(argv[1]) == "-h")) return Help();
    if (argc < 2) return Usage();

    try {
        if (argc >= 3 && std::string_view(argv[1]) == "dexvm" &&
            std::string_view(argv[2]) == "dump-surface") {
            std::filesystem::path boot;
            if (argc == 5 && std::string_view(argv[3]) == "--bootdex") boot = Utf8Path(argv[4]);
            else if (argc == 3) boot = app::BundledDataPaths().root / "android/19/framework/bootdex.jar";
            else throw std::invalid_argument("dump-surface accepts only --bootdex <jar>");
            Write(stdout, app::DumpDexSurface(boot) + "\n");
            return 0;
        }
        if (argc >= 3 && std::string_view(argv[1]) == "dexvm" &&
            std::string_view(argv[2]) == "run-dex") {
            std::vector<std::string_view> args(argv + 3, argv + argc);
            const auto options = app::ParseDexRunArgs(args);
            try {
                return app::RunDex(options);
            } catch (const std::exception& error) {
                // Keep VM/setup failures distinct from guest exit statuses.
                Write(stderr, FormatError(error.what()));
                return 70;
            }
        }
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

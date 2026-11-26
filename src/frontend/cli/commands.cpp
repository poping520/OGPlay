#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ogplay/app/json.h"
#include "ogplay/app/host.h"
#include "ogplay/app/diagnostics.h"
#include "ogplay/app/dex_run.h"

#include "ogplay/app/game_run.h"
#include "library_commands.h"
#include "output.h"
#include "commands.h"
#include <vector>

using ogplay::cli::FormatError;
using ogplay::cli::Write;

namespace {

std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

constexpr std::string_view kDexvmHelp =
    "  dexvm dump-surface [--framework <dir>]  Export linked platform JSON\n"
    "  dexvm run-dex <dex|jar> [--class <name>] [--backend switch|threaded] [--native-lib-dir <dir>]\n"
    "                 [--vfs-file <host-file> <guest-path>]...\n"
    "        [--enable-assertions] [--merge-console] [--ledger-json <file>] [-- args...]\n"
    "                                Run a standalone main like dalvikvm;\n"
    "                                exit 0/1(uncaught)/System.exit, 70 on VM error\n";

constexpr std::string_view kDiagHelp = "  diag snapshot --pid <pid> [--diag-dir <dir>] [--human|-h]\n"
                                       "                                Request a snapshot from a running game;\n"
                                       "                                prints one JSON line\n";

std::string Synopsis() {
    return std::string("usage: ogplay-cli <command> [arguments]\n"
                       "\n"
                       "Commands:\n") +
           std::string(kDexvmHelp) +
           "  run-apk <apk> [options]       Run an APK directly\n"
           "  library [--library-root <dir>] <subcommand> [--human|-h]\n"
           "                                Manage the game library\n" +
           std::string(kDiagHelp) +
           "  --version                     Print the version\n"
           "  -h, --help                    Show the full help; <command> --help shows one command\n";
}

constexpr std::string_view kLibraryHelp =
    "Library subcommands:\n"
    "  Output is one JSON line; --human or -h anywhere after `library` indents it.\n"
    "  <installation> is an installation id, or a package that names exactly one installation.\n"
    "  list [--filter all|attention|launchable]\n"
    "                                Summarize installations: id, package, name,\n"
    "                                version, status (ready|running|damaged|\n"
    "                                data_missing|last_run_failed); attention: entry\n"
    "                                problems or a failed last run\n"
    "  show <installation>           Every field of one installation, with its last run\n"
    "  inspect <apk>                 Inspect an APK without importing it\n"
    "  import <apk> [--external-dir <dir> [--link-external]] [--new-instance]\n"
    "                                Import an APK; the data directory is copied\n"
    "                                into the library unless --link-external;\n"
    "                                --new-instance adds a copy\n"
    "  remove <installation-id> --yes [--delete-saves]\n"
    "                                Remove an installation; saves are kept and\n"
    "                                reused by the next import unless deleted\n"
    "  refresh-icons                 Re-extract every library icon from its APK\n"
    "  data-hint <installation> [--external-dir <dir>]\n"
    "                                Suggest a data mount from the last run\n"
    "  settings get <installation>\n"
    "  settings set <installation> <key>=<value>... [--unset <key>]...\n"
    "  run <installation> [--mode normal|preflight|diagnostic]\n";

constexpr std::string_view kRunApkHelp =
    "run-apk <apk> [options]:\n"
    "  Data\n"
    "    --profiles-dir <dir>        Profile directory\n"
    "    --external-dir <host-dir>   Host directory exposed as external storage\n"
    "    --external-guest-dir </sdcard/...>\n"
    "                                Guest mount point; needs --external-dir\n"
    "    --obb <archive>             OBB expansion archive\n"
    "    --input-config <file>       Control config (input.toml)\n"
    "    --missing-data-report <file>\n"
    "                                Write /sdcard reads that found nothing (JSON)\n"
    "    --run-report <file>         Write how the run started and ended (JSON)\n"
    "  Language\n"
    "    --ui-language <tag>         Host UI language (default: system); tag is one of\n"
    "                                zh-CN zh-TW en-US ja-JP ko-KR ru-RU es-ES pt-BR fr-FR\n"
    "    --app-language <ui|tag>     Guest language, same tags (default: follow UI)\n"
    "  Window\n"
    "    --window-title <name>       Name shown in the window caption (default: APK file name)\n"
    "  Sandbox\n"
    "    --sandbox-dir <host-dir>    Sandbox root directory\n"
    "    --installation-id <id>      Installation instance to run\n"
    "    --ephemeral-sandbox         Throw-away sandbox; excludes --sandbox-dir\n"
    "  Execution\n"
    "    --preflight                 Check APK and dependencies, then exit\n"
    "    --exit-after-frames <count> Exit after <count> frames\n"
    "    --surface-width <1..4096>  Logical width (default 1280)\n"
    "    --surface-height <1..4096> Logical height (default 720)\n"
    "    --density-dpi <72..1280>    Guest display density (default 320)\n"
    "    --supersample <1..4>        Render scale factor (default 1)\n"
    "    --max-fps <1..1000>         Cap presented frames per second (default no cap)\n"
    "    --fps-overlay <corner>      Show frames per second in top-left, top-right,\n"
    "                                bottom-left or bottom-right (default hidden)\n"
    "    --dexvm-interpreter <switch|threaded>\n"
    "                                DexVM interpreter override\n"
    "    --no-google-play           No Play Store or Google services framework on the device\n"
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
    Write(stdout, Synopsis() + "\n" + std::string(kLibraryHelp) + "\n" + std::string(kRunApkHelp));
    return 0;
}

int Usage() {
    Write(stderr, Synopsis());
    Write(stderr, "\nRun 'ogplay-cli --help' for everything, or 'ogplay-cli <command> --help' for one command.\n");
    return 2;
}

// `<command> --help` before any `--` (run-dex passes what follows to the guest).
std::optional<std::string_view> CommandHelp(const int argc, const char* const argv[]) {
    const std::string_view command{argv[1]};
    bool asked{};
    for (int index = 2; index < argc && std::string_view(argv[index]) != "--"; ++index)
        asked = asked || std::string_view(argv[index]) == "--help";
    if (!asked) return std::nullopt;
    if (command == "library") return kLibraryHelp;
    if (command == "run-apk") return kRunApkHelp;
    if (command == "dexvm") return kDexvmHelp;
    if (command == "diag") return kDiagHelp;
    return std::nullopt;
}

}  // namespace

int ogplay::cli::ReportCliFailure(std::exception_ptr failure) {
    Write(stderr, FormatError(ogplay::app::DescribeUnhandledException(failure)));
    return 1;
}

int ogplay::cli::RunCli(const int argc, const char* const argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        Write(stdout, "OGPlay " OGPLAY_VERSION "\n");
        return 0;
    }
    if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) return Help();
    if (argc < 2) return Usage();
    if (const auto help = CommandHelp(argc, argv)) {
        Write(stdout, *help);
        return 0;
    }

    try {
        if (argc >= 3 && std::string_view(argv[1]) == "dexvm" && std::string_view(argv[2]) == "dump-surface") {
            std::filesystem::path boot;
            if (argc == 5 && std::string_view(argv[3]) == "--framework")
                boot = Utf8Path(argv[4]);
            else if (argc == 3)
                boot = app::BundledDataPaths().root / "android/19/framework";
            else
                throw std::invalid_argument("dump-surface accepts only --framework <directory>");
            Write(stdout, app::DumpDexSurface(boot) + "\n");
            return 0;
        }
        if (argc >= 3 && std::string_view(argv[1]) == "dexvm" && std::string_view(argv[2]) == "run-dex") {
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
        if (std::string_view(argv[1]) == "library")
            return RunLibraryCommand(std::span(argv + 2, static_cast<std::size_t>(argc - 2)));
        if (std::string_view(argv[1]) == "run-apk") {
            std::vector<std::string_view> args(argv + 2, argv + argc);
            return app::RunGame(app::ParseGameRunArgs(args));
        }
        if (argc >= 3 && std::string_view(argv[1]) == "diag" && std::string_view(argv[2]) == "snapshot") {
            std::optional<std::uint64_t> pid;
            std::filesystem::path directory{".local/diagnostics"};
            bool human{};
            for (int index = 3; index < argc; ++index) {
                const std::string_view option{argv[index]};
                if (option == "--human" || option == "-h") {
                    human = true;
                } else if (option == "--pid" && index + 1 < argc) {
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
                    throw std::invalid_argument("unknown or incomplete diag snapshot option: " + std::string(option));
                }
            }
            if (!pid) throw std::invalid_argument("diag snapshot requires --pid");
            const auto trigger = ogplay::app::RequestDiagnosticSnapshot(*pid, directory);
            // The snapshot is written asynchronously by the game; this only confirms the request.
            ogplay::app::JsonWriter writer;
            const auto result = writer.Object();
            writer.AddUnsignedInteger(result, "pid", *pid);
            writer.AddString(result, "trigger", trigger);
            const auto text = directory.generic_u8string();
            writer.AddString(result, "diag_dir", {reinterpret_cast<const char*>(text.data()), text.size()});
            writer.AddBool(result, "requested", true);
            Write(stdout, writer.Serialize(result, human) + "\n");
            return 0;
        }
    } catch (...) {
        return ReportCliFailure(std::current_exception());
    }
    return Usage();
}

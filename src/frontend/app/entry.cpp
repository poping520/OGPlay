#include "ogplay/app/api.h"
#include "commands.h"
#include "../game/run_game.h"
#include "ogplay/frontend/gui.h"
#include "ogplay/core/logger.h"
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if OGPLAY_HAS_SDL3
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#endif

namespace {
void Error(const char* message) noexcept {
    static_cast<void>(std::fputs("\nerror [ogplay]\n  ", stderr));
    static_cast<void>(std::fputs(message, stderr));
    static_cast<void>(std::fputs("\n", stderr));
}

int DesktopMain(int argc, char* argv[]) {
    ogplay::core::Logger logger;
    logger.AddSink(std::make_shared<ogplay::core::ConsoleSink>(stderr), ogplay::core::LogLevel::info);
    if (argc >= 2 && std::string_view(argv[1]) == "--run-game") {
        std::vector<std::string_view> args(argv + 2, argv + argc);
        return ogplay::app::RunGame(ogplay::frontend::ParseGameRunOptions(args));
    }
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        static_cast<void>(std::fputs("OGPlay " OGPLAY_VERSION "\n", stdout));
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        static_cast<void>(std::fputs("usage: ogplay [--library-root <dir>] [--smoke-frames <count>]\n"
            "       ogplay --run-game <apk> [run-apk options; see ogplay-cli --help]\n", stdout));
        return 0;
    }
#if OGPLAY_HAS_GUI
    return ogplay::frontend::RunGuiCommand(argc, argv, logger);
#else
    throw std::runtime_error("GUI host is not available in this build");
#endif
}

int CliMain(int argc, char* argv[]) { return ogplay::frontend::RunCli(argc, argv); }

// C++ unwinding must not escape through SDL's C bootstrap.
template <int (*Entry)(int, char**)>
int GuardedMain(int argc, char* argv[]) noexcept {
    try { return Entry(argc, argv); }
    catch (const std::exception& error) { Error(error.what()); }
    catch (...) { Error("unknown application failure"); }
    return 1;
}

template <int (*Entry)(int, char**)>
int RunEntry(int argc, const char* const argv[]) noexcept {
    try {
        if (argc < 1 || argv == nullptr) throw std::invalid_argument("invalid application arguments");
        std::vector<std::string> storage;
        storage.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index) {
            if (!argv[index]) throw std::invalid_argument("null application argument");
            storage.emplace_back(argv[index]);
        }
        std::vector<char*> args;
        args.reserve(storage.size() + 1);
        for (auto& argument : storage) args.push_back(argument.data());
        args.push_back(nullptr);
#if OGPLAY_HAS_SDL3
        return SDL_RunApp(argc, args.data(), GuardedMain<Entry>, nullptr);
#else
        return GuardedMain<Entry>(argc, args.data());
#endif
    } catch (const std::exception& error) { Error(error.what()); }
    catch (...) { Error("unknown application bootstrap failure"); }
    return 1;
}
}  // namespace

int ogplay_desktop_main(int argc, const char* const argv[]) noexcept { return RunEntry<DesktopMain>(argc, argv); }
int ogplay_cli_main(int argc, const char* const argv[]) noexcept { return RunEntry<CliMain>(argc, argv); }

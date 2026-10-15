#include "ogplay/app/game_run.h"
#include "ogplay/app/library.h"
#include "ogplay/core/logger.h"
#include "../game/run_game.h"
#include <memory>
#include <exception>
#if OGPLAY_HAS_SDL3
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#endif
namespace ogplay::app {
int RunGame(GameRunOptions options, const LogConfig& config) {
    ValidateGameRunOptions(options);
    std::optional<RunningLease> running;
    if (options.library_root && options.installation_id) running.emplace(*options.library_root, *options.installation_id);
    core::Logger logger;
    if (config.stderr_enabled) logger.AddSink(std::make_shared<core::ConsoleSink>(stderr), static_cast<core::LogLevel>(config.console_level));
    if (config.file) logger.AddSink(std::make_shared<core::FileSink>(*config.file));
#if OGPLAY_HAS_SDL3
    struct Invocation { GameRunOptions options; core::Logger& logger; bool status_to_stderr; std::exception_ptr error; };
    Invocation invocation{std::move(options), logger, config.status_to_stderr, {}};
    // SDL's C callback must never carry a C++ exception.
    static thread_local Invocation* active{};
    if (active) throw std::logic_error("recursive game host invocation");
    active = &invocation;
    const auto callback = +[](int, char**) -> int {
        try { return frontend::RunGame(std::move(active->options), active->logger, active->status_to_stderr); }
        catch (...) { active->error = std::current_exception(); return 1; }
    };
    char name[] = "ogplay";
    char* argv[]{name, nullptr};
    const int result = SDL_RunApp(1, argv, callback, nullptr);
    active = nullptr;
    if (invocation.error) std::rethrow_exception(invocation.error);
    return result;
#else
    return frontend::RunGame(std::move(options), logger, config.status_to_stderr);
#endif
}
}

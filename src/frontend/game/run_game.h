#pragma once
#include "ogplay/app/game_run.h"
namespace ogplay::core { class Logger; }
namespace ogplay::frontend {
using GameRunOptions = app::GameRunOptions;
inline GameRunOptions ParseGameRunOptions(std::span<const std::string_view> args) {
    return app::ParseGameRunArgs(args);
}
int RunGame(GameRunOptions options, core::Logger& logger);
}

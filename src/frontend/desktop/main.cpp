#include "ogplay/app/game_run.h"
#include "ogplay/app/logging.h"
#include "frontend/gui/shell.h"
#include <cstdio>
#include <exception>
#include <string_view>
#include <vector>
namespace {
void Write(FILE* stream, std::string_view text) {
    static_cast<void>(std::fwrite(text.data(), sizeof(char), text.size(), stream));
    static_cast<void>(std::fflush(stream));
}
}
int OgplayMain(int argc, const char* const argv[]) noexcept {
    try {
        if (argc >= 2 && std::string_view(argv[1]) == "--run-game") {
            const std::vector<std::string_view> args(argv + 2, argv + argc);
            return ogplay::app::RunGame(ogplay::app::ParseGameRunArgs(args));
        }
        if (argc == 2 && std::string_view(argv[1]) == "--version") { Write(stdout, "OGPlay " OGPLAY_VERSION "\n"); return 0; }
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            Write(stdout, "usage: ogplay [--library-root <dir>] [--smoke-frames <count>]\n"
                "       ogplay --run-game <apk> [run-apk options; see ogplay-cli --help]\n");
            return 0;
        }
        ogplay::app::Logger logger;
        return ogplay::frontend::RunGuiCommand(argc, argv, logger);
    } catch (const std::exception& error) { Write(stderr, std::string("\nerror [ogplay]\n  ") + error.what() + "\n"); }
    catch (...) { Write(stderr, "\nerror [ogplay]\n  unknown application failure\n"); }
    return 1;
}

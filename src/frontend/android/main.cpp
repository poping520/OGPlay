#include <android/log.h>

#include <exception>
#include <string_view>
#include <vector>

#include "ogplay/app/game_run.h"

namespace {

constexpr const char* kLogTag = "OGPlay";

}  // namespace

// SDLActivity resolves this symbol by name and enters it on the SDL thread.
extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char* argv[]) {
    try {
        const std::vector<std::string_view> args(argv + 1, argv + argc);
        return ogplay::app::RunGame(ogplay::app::ParseGameRunArgs(args));
    } catch (const std::exception& error) {
        __android_log_write(ANDROID_LOG_ERROR, kLogTag, error.what());
    } catch (...) {
        __android_log_write(ANDROID_LOG_ERROR, kLogTag, "unknown application failure");
    }
    return 1;
}

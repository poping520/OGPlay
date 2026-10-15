#include "ogplay/app/api.h"

extern "C" int ogplay_host_main(int argc, const char* const argv[]) noexcept {
    return ogplay_desktop_main(argc, argv);
}

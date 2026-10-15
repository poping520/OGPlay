#include "ogplay/app/api.h"
#if OGPLAY_NATIVE_ENTRY_ADAPTER
extern "C" int ogplay_host_main(int argc, const char* const argv[]) noexcept {
    return ogplay_cli_main(argc, argv);
}
#else
int main(int argc, char* argv[]) { return ogplay_cli_main(argc, argv); }
#endif

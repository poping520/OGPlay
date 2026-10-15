#include "commands.h"
#if OGPLAY_NATIVE_ENTRY_ADAPTER
int OgplayMain(int argc, const char* const argv[]) noexcept {
    return ogplay::cli::RunCli(argc, argv);
}
#else
int main(int argc, char* argv[]) { return ogplay::cli::RunCli(argc, argv); }
#endif

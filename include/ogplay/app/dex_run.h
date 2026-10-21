#pragma once
#include "ogplay/app/export.h"
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace ogplay::app {

// `ogplay-cli dexvm run-dex`: runs a standalone dex/jar `main` on the
// production platform class sources (core/Android intrinsics, BootDex, API19
// skeleton), the way `dalvikvm -cp <file> <class>` would. No APK, Manifest,
// native process, graphics or audio.
struct DexRunOptions final {
    std::filesystem::path input;           // classes.dex or a jar/zip holding it
    std::string main_class{"Main"};        // binary name, '.' or '/' separated
    std::string backend{"switch"};         // switch | threaded
    std::filesystem::path ledger_json;     // optional unimplemented-hit report
    bool enable_assertions{false};         // dalvikvm -ea
    std::vector<std::string> arguments;    // passed to main(String[])
};

// Receives System.out/err bytes exactly as committed by the guest streams.
using DexConsole = std::function<void(std::string_view)>;

[[nodiscard]] OGPLAYCORE_API DexRunOptions ParseDexRunArgs(std::span<const std::string_view> args);

// Exit status follows dalvikvm: 0 when main returns, 1 after an uncaught
// exception is dispatched, n for System.exit(n). Internal VM or setup errors
// throw instead of being folded into a guest status.
[[nodiscard]] OGPLAYCORE_API int RunDex(const DexRunOptions& options, const DexConsole& console = {});

}  // namespace ogplay::app

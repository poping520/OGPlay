#pragma once

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/integration/dexvm_android.h"

namespace ogplay::test {
inline std::vector<std::uint8_t> ReadBootDex() {
    const auto path = std::filesystem::path(OGPLAY_SOURCE_DIR) /
        "data/android/19/framework/bootdex.jar";
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("missing test BootDex: " + path.string());
    const std::vector<char> raw{std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>()};
    std::vector<std::byte> archive(raw.size());
    std::memcpy(archive.data(), raw.data(), raw.size());
    const auto dex = loader::ReadApkEntry(
        archive, loader::ParseApkArchive(archive), "classes.dex");
    std::vector<std::uint8_t> bytes(dex.size());
    std::memcpy(bytes.data(), dex.data(), dex.size());
    return bytes;
}
inline void BindBootDexPlatformNatives(runtime::dexvm::DexClassLinker& linker) {
    // Core-only fixtures still load the full curated artifact. Its Typeface
    // native boundary needs the real built-in backend, even when not exercised.
    if (!linker.FindClass("Landroid/graphics/Typeface;")) {
        const auto context = std::make_shared<runtime::DexVmAndroidContext>();
        for (const auto& declaration : runtime::AndroidIntrinsicCatalog(context)) {
            if (declaration.descriptor == "Landroid/graphics/Typeface;")
                linker.RegisterIntrinsics(std::array{declaration});
        }
    }
}
inline void RegisterBootDex(runtime::dexvm::DexClassLinker& linker) {
    BindBootDexPlatformNatives(linker);
    linker.RegisterBootDex(ReadBootDex());
}
}  // namespace ogplay::test

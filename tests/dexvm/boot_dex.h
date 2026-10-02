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
inline void BindBootDexArchive(runtime::DexVmAndroidContext& context) {
    const auto path = std::filesystem::path(OGPLAY_SOURCE_DIR) /
        "data/android/19/framework/bootdex.jar";
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("missing test BootDex: " + path.string());
    const std::vector<char> raw{std::istreambuf_iterator<char>(stream), {}};
    context.boot_classpath_bytes.resize(raw.size());
    std::memcpy(context.boot_classpath_bytes.data(), raw.data(), raw.size());
    context.boot_classpath_archive = loader::ParseApkArchive(context.boot_classpath_bytes);
}
inline void BindBootDexPlatformNatives(runtime::dexvm::DexClassLinker& linker) {
    // Core-only fixtures load the full curated artifact. Bind its audited
    // Android native owners to real handlers, even when not exercised.
    if (!linker.FindClass("Landroid/graphics/Typeface;")) {
        const auto context = std::make_shared<runtime::DexVmAndroidContext>();
        for (const auto& declaration : runtime::AndroidIntrinsicCatalog(context)) {
            if (declaration.descriptor == "Landroid/graphics/Typeface;" ||
                declaration.descriptor == "Landroid/app/NativeActivity;" ||
                declaration.descriptor == "Landroid/database/CursorWindow;" ||
                declaration.descriptor == "Landroid/database/sqlite/SQLiteConnection;" ||
                declaration.descriptor == "Landroid/database/sqlite/SQLiteDebug;" ||
                declaration.descriptor == "Landroid/database/sqlite/SQLiteGlobal;" ||
                declaration.descriptor == "Landroid/media/AudioTrack;" ||
                declaration.descriptor == "Landroid/media/MediaPlayer;" ||
                declaration.descriptor == "Landroid/media/SoundPool$SoundPoolImpl;" ||
                declaration.descriptor == "Landroid/util/EventLog;" ||
                declaration.descriptor == "Landroid/os/Binder;" ||
                declaration.descriptor == "Landroid/os/Parcel;" ||
                declaration.descriptor == "Landroid/os/SystemProperties;")
                linker.RegisterIntrinsics(std::array{declaration});
            if (declaration.descriptor == "Landroid/view/KeyEvent;" ||
                declaration.descriptor == "Landroid/view/KeyCharacterMap;" ||
                declaration.descriptor == "Landroid/net/wifi/WifiManager;" ||
                declaration.descriptor == "Landroid/net/wifi/WifiManager$WifiLock;" ||
                declaration.descriptor == "Landroid/net/wifi/WifiManager$MulticastLock;")
                linker.RegisterIntrinsics(std::array{declaration});
        }
    }
}
inline void RegisterBootDex(runtime::dexvm::DexClassLinker& linker) {
    BindBootDexPlatformNatives(linker);
    linker.RegisterBootDex(ReadBootDex());
}
}  // namespace ogplay::test

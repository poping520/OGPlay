#pragma once

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"

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
inline void RegisterBootDex(runtime::dexvm::DexClassLinker& linker) {
    linker.RegisterBootDex(ReadBootDex());
}
}  // namespace ogplay::test

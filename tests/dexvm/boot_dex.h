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
// Fill absent Android declarations using the production catalog. Existing test
// declarations remain explicit overrides; callers install Core first.
void BindBootDexPlatformNatives(runtime::dexvm::DexClassLinker& linker);
void RegisterBootDexSkeleton(runtime::dexvm::DexClassLinker& linker);
// View constructors now call Context.getResources. UI fixtures must supply
// attached wrappers, just as the production Activity driver does.
void AttachViewTestContext(runtime::dexvm::Interpreter& vm, runtime::dexvm::VmObjectRef owner);
inline void RegisterBootDex(runtime::dexvm::DexClassLinker& linker) {
    BindBootDexPlatformNatives(linker);
    const auto path = std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/android/19/framework/bootdex.jar";
    std::ifstream stream(path, std::ios::binary);
    const std::vector<char> raw{std::istreambuf_iterator<char>(stream), {}};
    const auto policy = runtime::dexvm::DexClassLinker::BootDexMemberSources(
        {reinterpret_cast<const std::byte*>(raw.data()), raw.size()});
    linker.RegisterBootDex(ReadBootDex(), policy);
}
}  // namespace ogplay::test

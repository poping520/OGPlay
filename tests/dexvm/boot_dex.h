#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/integration/dexvm_android.h"

namespace ogplay::test {
// The payload's bootdex.jar, parsed once per test binary.
inline const std::vector<runtime::BootClasspathJar>& BootClasspathJars() {
    static const auto jars = [] {
        auto result =
            runtime::ReadBootClasspath(std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/android/19/framework");
        for (auto& jar : result)
            jar.archive = loader::ParseApkArchive(jar.bytes);
        return result;
    }();
    return jars;
}

// Boot classpath DEX files in classpath order.
inline std::vector<std::vector<std::uint8_t>> ReadBootDex() {
    return runtime::BootClasspathDexFiles(BootClasspathJars());
}

// The source JAR names in bootdex.jar DEX order.
inline std::vector<std::uint8_t> ReadBootDexFromSource(std::string_view source) {
    constexpr std::array<std::string_view, 9> sources{
        "ogplay.jar",    "core.jar",       "conscrypt.jar",        "okhttp.jar",        "ext.jar",
        "framework.jar", "framework2.jar", "telephony-common.jar", "android.policy.jar"};
    const auto found = std::ranges::find(sources, source);
    if (found == sources.end()) throw std::invalid_argument("unknown boot DEX source: " + std::string(source));
    const auto entry = found == sources.begin() ? std::string("classes.dex")
                                                : "classes" + std::to_string(found - sources.begin() + 1) + ".dex";
    const auto& jar = BootClasspathJars().front();
    const auto dex = loader::ReadApkEntry(jar.bytes, jar.archive, entry);
    std::vector<std::uint8_t> result(dex.size());
    std::memcpy(result.data(), dex.data(), dex.size());
    return result;
}

std::vector<std::byte> ReadTestSystemFont(std::string_view name);

inline void BindBootDexArchive(runtime::DexVmAndroidContext& context) {
    context.framework_resource_apk =
        std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/android/19/framework/framework-res.apk";
    context.boot_classpath = BootClasspathJars();
    context.system_font_source = ReadTestSystemFont;
}

// Fill absent Android declarations using the production catalog. Existing test
// declarations remain explicit overrides; callers install Core first.
void MountResourceFixture(runtime::DexVmAndroidContext& context, runtime::dexvm::Interpreter& vm);
// Explicit facts for VMs that do not start guest Bionic and ICU.
runtime::dexvm::CoreIntrinsicServices CoreBootFixtureServices(runtime::dexvm::CoreIntrinsicServices services = {});
void BindCoreBootFixtureFacts(runtime::dexvm::CoreIntrinsicServices& services);
void BindBootDexPlatformNatives(runtime::dexvm::DexClassLinker& linker);
// View constructors now call Context.getResources. UI fixtures must supply
// attached wrappers, just as the production Activity driver does.
runtime::dexvm::VmObjectRef MakeRomAppContext(runtime::dexvm::Interpreter& vm,
                                              const std::shared_ptr<runtime::DexVmAndroidContext>& context);
void AttachViewTestContext(runtime::dexvm::Interpreter& vm, runtime::dexvm::VmObjectRef owner,
                           const std::shared_ptr<runtime::DexVmAndroidContext>& context);

inline void RegisterBootDex(runtime::dexvm::DexClassLinker& linker) {
    BindBootDexPlatformNatives(linker);
    for (auto& dex : ReadBootDex())
        linker.RegisterBootDex(std::move(dex));
}
}  // namespace ogplay::test

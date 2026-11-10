#pragma once

#include <filesystem>
#include <vector>

#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/integration/dexvm_android.h"

namespace ogplay::test {
// The payload boot classpath (ogplay.jar + ROM jars), parsed once per test binary.
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
void BindBootDexPlatformNatives(runtime::dexvm::DexClassLinker& linker);
// View constructors now call Context.getResources. UI fixtures must supply
// attached wrappers, just as the production Activity driver does.
void AttachViewTestContext(runtime::dexvm::Interpreter& vm, runtime::dexvm::VmObjectRef owner);

inline void RegisterBootDex(runtime::dexvm::DexClassLinker& linker) {
    BindBootDexPlatformNatives(linker);
    for (auto& dex : ReadBootDex())
        linker.RegisterBootDex(std::move(dex));
}
}  // namespace ogplay::test

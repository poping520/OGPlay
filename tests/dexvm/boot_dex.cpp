#include "boot_dex.h"

namespace ogplay::test {
void BindBootDexPlatformNatives(runtime::dexvm::DexClassLinker& linker) {
    const auto context = std::make_shared<runtime::DexVmAndroidContext>();
    for (const auto& declaration : runtime::AndroidIntrinsicCatalog(context)) {
        if (!linker.FindClass(declaration.descriptor))
            linker.RegisterIntrinsics(std::array{declaration});
    }
}

void RegisterBootDexSkeleton(runtime::dexvm::DexClassLinker& linker) {
    const auto path = std::filesystem::path(OGPLAY_SOURCE_DIR) /
        "data/dexvm/api19-skeleton.bin";
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("missing test skeleton: " + path.string());
    linker.RegisterSkeletonSurface({std::istreambuf_iterator<char>(stream), {}});
}
}  // namespace ogplay::test

#include "boot_dex.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace ogplay::test {
void AttachViewTestContext(runtime::dexvm::Interpreter& vm, runtime::dexvm::VmObjectRef owner) {
    using namespace runtime::dexvm;
    const auto roots = vm.ProtectReferences(std::array{owner});
    const auto wrapper = vm.Linker().ResolveDescriptor("Landroid/content/ContextWrapper;");
    for (unsigned depth = 0; owner.IsValid() && depth < 16; ++depth) {
        const auto type = vm.Model().ObjectClass(owner);
        if (!vm.Linker().IsAssignable(wrapper, type)) return;
        const auto field = vm.Linker().FindFieldRecursive(type, "mBase", "Landroid/content/Context;");
        if (!field) throw std::runtime_error("fixture ContextWrapper has no base field");
        const VmObjectRef base(vm.Model().InstanceSlots(owner)[vm.Linker().Field(*field).slot].bits);
        if (base.IsValid()) {
            owner = base;
            continue;
        }
        const auto context = vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto slot = vm.Linker().FindVtableIndex(type, "attachBaseContext", "(Landroid/content/Context;)V");
        if (!slot) throw std::runtime_error("fixture ContextWrapper has no attachBaseContext");
        const auto result =
            vm.Call(vm.Linker().Class(type).vtable[*slot], std::array{VmValue::Ref(owner), VmValue::Ref(context)});
        if (result.exception.IsValid()) throw std::runtime_error(result.exception_message);
        return;
    }
    if (owner.IsValid()) throw std::runtime_error("fixture ContextWrapper cycle or depth limit");
}

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

#include "boot_dex.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"

#include <cstring>
#include <fstream>

namespace ogplay::test {
std::vector<std::byte> ReadTestSystemFont(std::string_view name) {
    std::ifstream stream(std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/android/19/fonts" / name,
                         std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("test font is missing: " + std::string(name));
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg()));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("failed to read test font");
    return bytes;
}

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
    context->system_font_source = ReadTestSystemFont;
    for (const auto& declaration : runtime::AndroidIntrinsicCatalog(context)) {
        if (!linker.FindClass(declaration.descriptor)) linker.RegisterIntrinsics(std::array{declaration});
    }
}
}  // namespace ogplay::test

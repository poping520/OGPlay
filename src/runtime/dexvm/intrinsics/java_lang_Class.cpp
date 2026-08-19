#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    IntrinsicClassDecl Declare_java_lang_Class() {
        auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Class;", "Ljava/lang/Object;");
        builder.FinalMethod("getName", "()Ljava/lang/String;", [](IntrinsicContext& context) {
            auto& vm = context.vm;
            const auto java_class = vm.Model().ClassOfClassObject(context.receiver);
            return VmValue::Ref(vm.NewStringUtf8(DottedName(vm.Linker().Class(java_class).descriptor)));
        });

        builder.FinalMethod("getDeclaredMethods", "()[Ljava/lang/reflect/Method;", [](IntrinsicContext& context) {
            auto& vm = context.vm;
            const auto represented = vm.Model().ClassOfClassObject(context.receiver);
            std::vector<VmMethodId> declared;
            for (const auto method_id: vm.Linker().MethodsOf(represented)) {
                const auto& method = vm.Linker().Method(method_id);
                if (method.name != "<init>" && method.name != "<clinit>") {
                    declared.push_back(method_id);
                }
            }
            const auto method_class = vm.Linker().ResolveDescriptor("Ljava/lang/reflect/Method;");
            const auto array_class = vm.Linker().ResolveDescriptor("[Ljava/lang/reflect/Method;");
            const auto array = vm.Model().NewObjectArray(
                array_class, method_class, static_cast<JniSize>(declared.size())
            );
            for (std::size_t index = 0; index < declared.size(); ++index) {
                const auto reflected = vm.NewIntrinsicInstance("Ljava/lang/reflect/Method;");
                const auto slots = vm.Model().InstanceSlots(reflected);
                slots[0] = {declared[index].Value(), SlotTag::cat1};
                slots[1] = {
                    vm.NewStringUtf8(vm.Linker().Method(declared[index]).name)
                    .Value(),
                    SlotTag::ref
                };
                vm.Model().SetObjectElement(array, static_cast<JniSize>(index), reflected);
            }
            return VmValue::Ref(array);
        });

        auto result = std::move(builder).Build();
        return result;
    }
} // namespace ogplay::runtime::dexvm::intrinsics

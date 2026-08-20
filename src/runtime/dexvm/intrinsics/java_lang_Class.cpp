#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/reflection.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    IntrinsicClassDecl Declare_java_lang_Class() {
        auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Class;", "Ljava/lang/Object;");
        builder.FinalMethod("getName", "()Ljava/lang/String;", [](IntrinsicContext& context) {
            auto& vm = context.vm;
            const auto java_class = vm.Model().ClassOfClassObject(context.receiver);
            return VmValue::Ref(vm.NewStringUtf8(DottedName(vm.Linker().Class(java_class).descriptor)));
        });

        builder.FinalMethod("getClassLoader", "()Ljava/lang/ClassLoader;", [](IntrinsicContext& context) {
            auto& vm = context.vm;
            const auto represented =
                vm.Model().ClassOfClassObject(context.receiver);
            return VmValue::Ref(
                vm.ClassLoaders().LoaderForClass(represented));
        });

        builder.FinalMethod("getDeclaredMethods", "()[Ljava/lang/reflect/Method;", [](IntrinsicContext& context) {
            auto& vm = context.vm;
            const auto represented = vm.Model().ClassOfClassObject(context.receiver);
            return VmValue::Ref(
                vm.Reflection().MaterializeDeclaredMethods(represented));
        });

        auto result = std::move(builder).Build();
        return result;
    }
} // namespace ogplay::runtime::dexvm::intrinsics

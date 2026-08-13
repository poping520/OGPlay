#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_Float() {
    IntrinsicClassBuilder builder("Ljava/lang/Float;");
    builder.Super("Ljava/lang/Number;");
    builder.Field("value", "F", false);
    builder.Field("TYPE", "Ljava/lang/Class;", true);
    builder.Virtual("<init>", "(F)V",
        [](IntrinsicContext& context) {
            SetBoxedBits(context, context.receiver, context.arguments[0].cat1, false);
                return VmValue::Void();
            });
    builder.Static("valueOf", "(F)Ljava/lang/Float;",
        [](IntrinsicContext& context) {
            return MakeBoxed(context, "Ljava/lang/Float;", context.arguments[0].cat1,
                             false);
            });
    builder.Virtual("floatValue", "()F",
        [](IntrinsicContext &context) {
                VmValue out;
                out.kind = VmValue::Kind::cat1;
                out.cat1 = static_cast<std::uint32_t>(BoxedBits(context, false));
                return out;
            });
    builder.Clinit(
        [](IntrinsicContext& context) {
                auto& vm = context.vm;
                vm.SetIntrinsicStaticRef(
                    "Ljava/lang/Float;", "TYPE", "Ljava/lang/Class;",
                    vm.Model().ClassObject(vm.Linker().ResolveDescriptor("F")));
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

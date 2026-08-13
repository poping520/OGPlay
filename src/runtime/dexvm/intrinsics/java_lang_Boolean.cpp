#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_Boolean() {
    IntrinsicClassBuilder builder("Ljava/lang/Boolean;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("value", "Z", false);
    builder.Virtual("<init>", "(Z)V",
        [](IntrinsicContext& context) {
                SetBoxedBits(context, context.receiver,
                             context.arguments[0].AsInt() != 0 ? 1U : 0U, false);
                return VmValue::Void();
            });
    builder.Static("valueOf", "(Z)Ljava/lang/Boolean;",
        [](IntrinsicContext &context) {
                return MakeBoxed(context, "Ljava/lang/Boolean;",
                                 context.arguments[0].AsInt() != 0 ? 1U : 0U, false);
            });
    builder.Virtual("booleanValue", "()Z",
        [](IntrinsicContext &context) {
                return VmValue::Int(BoxedBits(context, false) != 0 ? 1 : 0);
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

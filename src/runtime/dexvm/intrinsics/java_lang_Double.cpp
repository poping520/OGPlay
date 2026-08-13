#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_Double() {
    IntrinsicClassBuilder builder("Ljava/lang/Double;");
    builder.Super("Ljava/lang/Number;");
    builder.Field("value", "D", false);
    builder.Virtual("<init>", "(D)V",
        [](IntrinsicContext& context) {
            SetBoxedBits(context, context.receiver, context.arguments[0].wide, true);
                return VmValue::Void();
            });
    builder.Static("valueOf", "(D)Ljava/lang/Double;",
        [](IntrinsicContext &context) {
            return MakeBoxed(context, "Ljava/lang/Double;", context.arguments[0].wide,
                             true);
            });
    builder.Virtual("doubleValue", "()D",
        [](IntrinsicContext &context) {
                VmValue out;
                out.kind = VmValue::Kind::wide;
                out.wide = BoxedBits(context, true);
                return out;
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

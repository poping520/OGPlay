#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_Long() {
    IntrinsicClassBuilder builder("Ljava/lang/Long;");
    builder.Super("Ljava/lang/Number;");
    builder.Field("value", "J", false);
    builder.Virtual("<init>", "(J)V",
        [](IntrinsicContext& context) {
            SetBoxedBits(context, context.receiver, context.arguments[0].wide, true);
                return VmValue::Void();
            });
    builder.Static("valueOf", "(J)Ljava/lang/Long;",
        [](IntrinsicContext& context) {
            return MakeBoxed(context, "Ljava/lang/Long;", context.arguments[0].wide,
                             true);
            });
    builder.Virtual("longValue", "()J",
        [](IntrinsicContext &context) {
            return VmValue::Long(static_cast<std::int64_t>(BoxedBits(context, true)));
            });
    builder.Static("parseLong", "(Ljava/lang/String;)J",
        [](IntrinsicContext &context) {
                const auto text = Narrow(Value(context, context.arguments[0].ref));
                char* end = nullptr;
                const auto value = std::strtoll(text.c_str(), &end, 10);
                if (end == text.c_str() || *end != '\0') {
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "invalid long: " + text};
                }
                return VmValue::Long(value);
            });
    builder.Virtual("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context, Widen(std::to_string(static_cast<std::int64_t>(
                                         BoxedBits(context, true)))));
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

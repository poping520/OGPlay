#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_Integer() {
    IntrinsicClassBuilder builder("Ljava/lang/Integer;");
    builder.Super("Ljava/lang/Number;");
    builder.Field("value", "I", false);
    builder.Virtual("<init>", "(I)V",
        [](IntrinsicContext& context) {
                SetBoxedBits(context, context.receiver,
                         static_cast<std::uint32_t>(context.arguments[0].AsInt()),
                             false);
                return VmValue::Void();
            });
    builder.Static("valueOf", "(I)Ljava/lang/Integer;",
        [](IntrinsicContext &context) {
                return MakeBoxed(context, "Ljava/lang/Integer;",
                             static_cast<std::uint32_t>(context.arguments[0].AsInt()),
                                 false);
            });
    builder.Virtual("intValue", "()I",
        [](IntrinsicContext &context) {
            return VmValue::Int(static_cast<std::int32_t>(BoxedBits(context, false)));
            });
    builder.Static("parseInt", "(Ljava/lang/String;)I",
        [](IntrinsicContext &context) {
                const auto text = Narrow(Value(context, context.arguments[0].ref));
                char* end = nullptr;
                const auto value = std::strtol(text.c_str(), &end, 10);
                if (end == text.c_str() || *end != '\0') {
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "invalid int: " + text};
                }
                return VmValue::Int(static_cast<std::int32_t>(value));
            });
    builder.Virtual("toString", "()Ljava/lang/String;",
        [](IntrinsicContext &context) {
                return Make(context, Widen(std::to_string(static_cast<std::int32_t>(
                                         BoxedBits(context, false)))));
            });
    builder.Static("toString", "(I)Ljava/lang/String;",
        [](IntrinsicContext &context) {
                return Make(context,
                            Widen(std::to_string(context.arguments[0].AsInt())));
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

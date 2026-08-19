#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_io_PrintStream() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/io/PrintStream;", "Ljava/lang/Object;");
    builder.FinalMethod("println", "(Ljava/lang/String;)V",
        [](IntrinsicContext &context) {
                const auto argument = context.arguments[0].ref;
                GuestLine(context, argument.IsValid() ? Narrow(Value(context, argument))
                                       : std::string("null"));
                return VmValue::Void();
            });
    builder.FinalMethod("println", "(I)V",
        [](IntrinsicContext &context) {
                GuestLine(context, std::to_string(context.arguments[0].AsInt()));
                return VmValue::Void();
            });
    builder.FinalMethod("println", "()V",
        [](IntrinsicContext& context) {
                GuestLine(context, "");
                return VmValue::Void();
            });
    builder.FinalMethod("print", "(Ljava/lang/String;)V",
        [](IntrinsicContext &context) {
                const auto argument = context.arguments[0].ref;
                GuestLine(context, argument.IsValid() ? Narrow(Value(context, argument))
                                       : std::string("null"));
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

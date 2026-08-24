// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_util_regex_PatternSyntaxException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_util_regex_PatternSyntaxException {
using namespace detail;

IntrinsicClassDecl Declare_java_util_regex_PatternSyntaxException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/regex/PatternSyntaxException;", "Ljava/lang/IllegalArgumentException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_util_regex_PatternSyntaxException() {
    return dvm80_java_util_regex_PatternSyntaxException::Declare_java_util_regex_PatternSyntaxException();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

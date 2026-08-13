#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_util_NoSuchElementException() {
    IntrinsicClassBuilder builder("Ljava/util/NoSuchElementException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

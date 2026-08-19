#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_io_EOFException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/io/EOFException;", "Ljava/io/IOException;");
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

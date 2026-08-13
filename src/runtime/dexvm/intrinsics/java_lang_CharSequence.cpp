#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_CharSequence() {
    IntrinsicClassBuilder builder("Ljava/lang/CharSequence;");
    builder.MarkInterface();
    builder.Virtual("length", "()I",
        [](IntrinsicContext& context) {
                return VmValue::Int(static_cast<std::int32_t>(
                    context.vm.Model().StringValue(context.receiver).size()));
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

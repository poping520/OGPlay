#include "catalog.h"
#include "shared.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_Modifier() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/reflect/Modifier;", "Ljava/lang/Object;");
    builder.StaticMethod("toString", "(I)Ljava/lang/String;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.NewStringUtf8(
                detail::ModifierString(
                    static_cast<std::uint32_t>(context.arguments[0].AsInt()))));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics

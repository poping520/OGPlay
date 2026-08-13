#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_lang_Enum(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljava/lang/Enum;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

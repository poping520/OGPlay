#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_nio_charset_Charset(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/nio/charset/Charset;", "Ljava/lang/Object;");
    builder.StaticMethod("forName", "(Ljava/lang/String;)Ljava/nio/charset/Charset;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Ljava/nio/charset/Charset;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

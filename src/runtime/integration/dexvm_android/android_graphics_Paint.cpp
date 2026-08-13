#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Paint(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/Paint;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", GraphicsNoopHandler());
    builder.Virtual("<init>", "(I)V", GraphicsNoopHandler());
    builder.Virtual("setColor", "(I)V", GraphicsNoopHandler());
    builder.Virtual("setAntiAlias", "(Z)V", GraphicsNoopHandler());
    builder.Virtual("setTextSize", "(F)V", GraphicsNoopHandler());
    builder.Virtual("setTypeface",
        "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;",
        [](dx::IntrinsicContext& call) {
            // Returns the typeface that was set, per the platform contract.
            return dx::VmValue::Ref(call.arguments[0].ref);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

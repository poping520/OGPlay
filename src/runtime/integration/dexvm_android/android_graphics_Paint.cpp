#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Paint(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Paint;", "Ljava/lang/Object;");
    builder.Constructor("()V", GraphicsNoopHandler());
    builder.Constructor("(I)V", GraphicsNoopHandler());
    builder.FinalMethod("setColor", "(I)V", GraphicsNoopHandler());
    builder.FinalMethod("setAntiAlias", "(Z)V", GraphicsNoopHandler());
    builder.FinalMethod("setTextSize", "(F)V", GraphicsNoopHandler());
    builder.FinalMethod("setTypeface",
        "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;",
        [](dx::IntrinsicContext& call) {
            // Returns the typeface that was set, per the platform contract.
            return dx::VmValue::Ref(call.arguments[0].ref);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

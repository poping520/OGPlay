#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_Editable(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/text/Editable;");
    builder.MarkInterface();
    builder.Virtual("clear", "()V", handlers.handler_android_editable_clear);
    builder.Virtual("length", "()I", handlers.handler_android_editable_length);
    builder.Virtual("replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;", handlers.handler_android_editable_replace);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

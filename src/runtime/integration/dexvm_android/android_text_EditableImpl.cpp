#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_EditableImpl(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/text/EditableImpl;");
    builder.Super("Ljava/lang/Object;");
    builder.Implements("Landroid/text/Editable;");
    builder.Virtual("clear", "()V", EditableClearHandler(context));
    builder.Virtual("length", "()I", EditableLengthHandler(context));
    builder.Virtual("replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;", EditableReplaceHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

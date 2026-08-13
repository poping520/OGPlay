#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_Editable(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/text/Editable;");
    builder.MarkInterface();
    builder.Virtual("clear", "()V", EditableClearHandler(context));
    builder.Virtual("length", "()I", EditableLengthHandler(context));
    builder.Virtual("replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;", EditableReplaceHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

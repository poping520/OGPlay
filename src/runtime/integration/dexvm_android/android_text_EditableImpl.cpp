#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_EditableImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/text/EditableImpl;", "Ljava/lang/Object;", {"Landroid/text/Editable;"});
    builder.FinalMethod("clear", "()V", EditableClearHandler(context));
    builder.FinalMethod("length", "()I", EditableLengthHandler(context));
    builder.FinalMethod("replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;", EditableReplaceHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

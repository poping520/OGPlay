#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_EditText(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/widget/EditText;");
    builder.Super("Landroid/widget/TextView;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.Virtual("getText", "()Landroid/text/Editable;",
        [context](dx::IntrinsicContext& call) {
            const auto key =
                "editable:" + std::to_string(call.receiver.Value());
            const auto editable = Singleton(call, context, key,
                                            "Landroid/text/EditableImpl;");
            context->editable_owner[editable.Value()] = call.receiver.Value();
            return dx::VmValue::Ref(editable);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

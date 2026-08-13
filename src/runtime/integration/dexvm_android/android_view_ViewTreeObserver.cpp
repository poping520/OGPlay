#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewTreeObserver(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/view/ViewTreeObserver;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("addOnGlobalLayoutListener",
        "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V",
        [context](dx::IntrinsicContext& call) {
            const auto listener = call.arguments[0].ref;
            if (!listener.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                    "global layout listener is null"};
            }
            context->global_layout_listeners[call.receiver.Value()] = listener;
            return dx::VmValue::Void();
        });
    const auto remove_global_listener = dx::IntrinsicHandler(
        [context](dx::IntrinsicContext& call) {
            const auto found = context->global_layout_listeners.find(
                call.receiver.Value());
            if (found != context->global_layout_listeners.end() &&
                found->second == call.arguments[0].ref) {
                context->global_layout_listeners.erase(found);
            }
            return dx::VmValue::Void();
        });
    builder.Virtual("removeGlobalOnLayoutListener",
        "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V",
        remove_global_listener);
    builder.Virtual("removeOnGlobalLayoutListener",
        "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V",
        remove_global_listener);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

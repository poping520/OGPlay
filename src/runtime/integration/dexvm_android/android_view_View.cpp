#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_View(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/View;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", ViewInitHandler());
    builder.Overridable("onSizeChanged", "(IIII)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Overridable("onWindowFocusChanged", "(Z)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    const auto noop_flag = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Virtual("setFocusable", "(Z)V", noop_flag);
    builder.Virtual("setFocusableInTouchMode", "(Z)V", noop_flag);
    builder.Virtual("requestFocus", "()Z",
        [](dx::IntrinsicContext&) {
            // The single fullscreen view always holds focus.
            return dx::VmValue::Int(1);
        });
    builder.Virtual("invalidate", "()V", noop_flag);
    builder.Virtual("postInvalidate", "()V", noop_flag);
    builder.Virtual("getId", "()I",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Int(-1);  // View.NO_ID: no id was assigned
        });
    builder.Virtual("setVisibility", "(I)V", handlers.handler_android_view_set_visibility);
    builder.Virtual("getVisibility", "()I", handlers.handler_android_view_get_visibility);
    builder.Virtual("setBackgroundColor", "(I)V", WidgetNoopHandler());
    builder.Virtual("setBackgroundResource", "(I)V", WidgetNoopHandler());
    builder.Virtual("setBackgroundDrawable", "(Landroid/graphics/drawable/Drawable;)V", WidgetNoopHandler());
    builder.Virtual("setOnClickListener", "(Landroid/view/View$OnClickListener;)V", handlers.handler_android_view_set_on_click_listener);
    builder.Virtual("setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V", WidgetNoopHandler());
    builder.Virtual("clearFocus", "()V", WidgetNoopHandler());
    builder.Virtual("getWindowToken", "()Landroid/os/IBinder;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.Virtual("getViewTreeObserver", "()Landroid/view/ViewTreeObserver;",
        [context](dx::IntrinsicContext& call) {
            auto& observer =
                context->view_tree_observers[call.receiver.Value()];
            if (!observer.IsValid()) {
                observer = call.vm.NewIntrinsicInstance(
                    "Landroid/view/ViewTreeObserver;");
            }
            return dx::VmValue::Ref(observer);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

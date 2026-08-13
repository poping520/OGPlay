#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_View(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/View;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", handlers.handler_android_view_init);
    builder.Overridable("onSizeChanged", "(IIII)V", handlers.handler_android_view_noop_size);
    builder.Overridable("onWindowFocusChanged", "(Z)V", handlers.handler_android_view_noop_focus);
    builder.Virtual("setFocusable", "(Z)V", handlers.handler_android_view_noop_flag);
    builder.Virtual("setFocusableInTouchMode", "(Z)V", handlers.handler_android_view_noop_flag);
    builder.Virtual("requestFocus", "()Z", handlers.handler_android_view_request_focus);
    builder.Virtual("invalidate", "()V", handlers.handler_android_view_noop_flag);
    builder.Virtual("postInvalidate", "()V", handlers.handler_android_view_noop_flag);
    builder.Virtual("getId", "()I", handlers.handler_android_view_get_id);
    builder.Virtual("setVisibility", "(I)V", handlers.handler_android_view_set_visibility);
    builder.Virtual("getVisibility", "()I", handlers.handler_android_view_get_visibility);
    builder.Virtual("setBackgroundColor", "(I)V", handlers.handler_android_widget_noop);
    builder.Virtual("setBackgroundResource", "(I)V", handlers.handler_android_widget_noop);
    builder.Virtual("setBackgroundDrawable", "(Landroid/graphics/drawable/Drawable;)V", handlers.handler_android_widget_noop);
    builder.Virtual("setOnClickListener", "(Landroid/view/View$OnClickListener;)V", handlers.handler_android_view_set_on_click_listener);
    builder.Virtual("setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V", handlers.handler_android_widget_noop);
    builder.Virtual("clearFocus", "()V", handlers.handler_android_widget_noop);
    builder.Virtual("getWindowToken", "()Landroid/os/IBinder;", handlers.handler_android_widget_null);
    builder.Virtual("getViewTreeObserver", "()Landroid/view/ViewTreeObserver;", handlers.handler_android_view_get_tree_observer);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

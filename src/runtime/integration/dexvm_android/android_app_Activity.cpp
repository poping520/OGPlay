#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_Activity(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/app/Activity;");
    builder.Super("Landroid/content/Context;");
    builder.Virtual("<init>", "()V", handlers.handler_android_activity_init);
    builder.Overridable("onCreate", "(Landroid/os/Bundle;)V", handlers.handler_android_activity_lifecycle_noop);
    builder.Overridable("onStart", "()V", handlers.handler_android_activity_lifecycle_noop);
    builder.Overridable("onRestart", "()V", handlers.handler_android_activity_lifecycle_noop);
    builder.Overridable("onResume", "()V", handlers.handler_android_activity_lifecycle_noop);
    builder.Overridable("onPause", "()V", handlers.handler_android_activity_lifecycle_noop);
    builder.Overridable("onStop", "()V", handlers.handler_android_activity_lifecycle_noop);
    builder.Overridable("onDestroy", "()V", handlers.handler_android_activity_lifecycle_noop);
    builder.Overridable("onConfigurationChanged", "(Landroid/content/res/Configuration;)V", handlers.handler_android_activity_lifecycle_noop);
    builder.Virtual("getWindow", "()Landroid/view/Window;", handlers.handler_android_activity_get_window);
    builder.Virtual("requestWindowFeature", "(I)Z", handlers.handler_android_activity_request_window_feature);
    builder.Virtual("setContentView", "(Landroid/view/View;)V", handlers.handler_android_activity_set_content_view);
    builder.Virtual("setContentView", "(I)V", handlers.handler_android_activity_set_content_view_id);
    builder.Virtual("findViewById", "(I)Landroid/view/View;", handlers.handler_android_activity_find_view_by_id);
    builder.Virtual("getIntent", "()Landroid/content/Intent;", handlers.handler_android_activity_get_intent);
    builder.Virtual("runOnUiThread", "(Ljava/lang/Runnable;)V", handlers.handler_android_activity_run_on_ui_thread);
    builder.Virtual("setVolumeControlStream", "(I)V", handlers.handler_android_activity_set_volume_control_stream);
    builder.Overridable("onKeyDown", "(ILandroid/view/KeyEvent;)Z", handlers.handler_android_activity_on_key_false);
    builder.Overridable("onKeyUp", "(ILandroid/view/KeyEvent;)Z", handlers.handler_android_activity_on_key_false);
    builder.Overridable("onTouchEvent", "(Landroid/view/MotionEvent;)Z", handlers.handler_android_activity_on_touch_false);
    builder.Overridable("finish", "()V", handlers.handler_android_activity_finish);
    builder.Virtual("getWindowManager", "()Landroid/view/WindowManager;", handlers.handler_android_activity_get_window_manager);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

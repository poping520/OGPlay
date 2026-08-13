#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_MotionEvent(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/MotionEvent;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("action", "I", false);
    builder.Field("x", "F", false);
    builder.Field("y", "F", false);
    builder.Field("pointer", "I", false);
    builder.Virtual("getAction", "()I", handlers.handler_android_motion_event_get_action);
    builder.Virtual("getX", "()F", handlers.handler_android_motion_event_get_x);
    builder.Virtual("getY", "()F", handlers.handler_android_motion_event_get_y);
    builder.Virtual("getX", "(I)F", handlers.handler_android_motion_event_get_x_indexed);
    builder.Virtual("getY", "(I)F", handlers.handler_android_motion_event_get_y_indexed);
    builder.Virtual("getPointerCount", "()I", handlers.handler_android_motion_event_get_pointer_count);
    builder.Virtual("getPointerId", "(I)I", handlers.handler_android_motion_event_get_pointer_id);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/opengl/GLSurfaceView;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Virtual("setRenderer",
        "(Landroid/opengl/GLSurfaceView$Renderer;)V",
        [context](dx::IntrinsicContext& call) {
            context->renderer = call.arguments[0].ref;
            return dx::VmValue::Void();
        });
    builder.Virtual("requestRender", "()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    // Render pause/resume is owned by the lifecycle driver.
    const auto lifecycle_noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Virtual("onPause", "()V", lifecycle_noop);
    builder.Virtual("onResume", "()V", lifecycle_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

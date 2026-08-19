#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/opengl/GLSurfaceView;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("setRenderer",
        "(Landroid/opengl/GLSurfaceView$Renderer;)V",
        [context](dx::IntrinsicContext& call) {
            context->renderer = call.arguments[0].ref;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("requestRender", "()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    // Render pause/resume is owned by the lifecycle driver.
    const auto lifecycle_noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("onPause", "()V", lifecycle_noop);
    builder.FinalMethod("onResume", "()V", lifecycle_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

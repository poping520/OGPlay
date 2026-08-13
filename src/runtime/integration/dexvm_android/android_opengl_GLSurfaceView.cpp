#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/opengl/GLSurfaceView;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", handlers.handler_android_glsurfaceview_init);
    builder.Virtual("setRenderer", "(Landroid/opengl/GLSurfaceView$Renderer;)V", handlers.handler_android_glsurfaceview_set_renderer);
    builder.Virtual("requestRender", "()V", handlers.handler_android_glsurfaceview_request_render);
    builder.Virtual("onPause", "()V", handlers.handler_android_glsurfaceview_lifecycle_noop);
    builder.Virtual("onResume", "()V", handlers.handler_android_glsurfaceview_lifecycle_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

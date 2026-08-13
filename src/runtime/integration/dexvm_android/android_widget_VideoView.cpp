#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_VideoView(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/VideoView;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", handlers.handler_android_view_init);
    builder.Virtual("setVideoPath", "(Ljava/lang/String;)V", handlers.handler_android_videoview_set_path);
    builder.Virtual("start", "()V", handlers.handler_android_videoview_start);
    builder.Virtual("pause", "()V", handlers.handler_android_videoview_pause);
    builder.Virtual("seekTo", "(I)V", handlers.handler_android_videoview_seek_to);
    builder.Virtual("stopPlayback", "()V", handlers.handler_android_videoview_stop_playback);
    builder.Virtual("getDuration", "()I", handlers.handler_android_videoview_get_duration);
    builder.Virtual("getCurrentPosition", "()I", handlers.handler_android_videoview_get_current_position);
    builder.Virtual("setOnCompletionListener", "(Landroid/media/MediaPlayer$OnCompletionListener;)V", handlers.handler_android_videoview_set_completion);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

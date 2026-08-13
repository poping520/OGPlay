#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_MediaPlayer(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/media/MediaPlayer;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", handlers.handler_android_media_player_init);
    builder.Virtual("setDataSource", "(Ljava/lang/String;)V", handlers.handler_android_media_player_set_data_source);
    builder.Virtual("isLooping", "()Z", handlers.handler_android_media_player_is_looping);
    builder.Static("create", "(Landroid/content/Context;I)Landroid/media/MediaPlayer;", handlers.handler_android_media_player_create);
    builder.Virtual("isPlaying", "()Z", handlers.handler_android_media_player_is_playing);
    builder.Virtual("start", "()V", handlers.handler_android_media_player_start);
    builder.Virtual("pause", "()V", handlers.handler_android_media_player_pause);
    builder.Virtual("stop", "()V", handlers.handler_android_media_player_stop);
    builder.Virtual("release", "()V", handlers.handler_android_media_player_release);
    builder.Virtual("prepare", "()V", handlers.handler_android_media_player_prepare);
    builder.Virtual("seekTo", "(I)V", handlers.handler_android_media_player_seek_to);
    builder.Virtual("setLooping", "(Z)V", handlers.handler_android_media_player_set_looping);
    builder.Virtual("setVolume", "(FF)V", handlers.handler_android_media_player_set_volume);
    builder.Virtual("setOnCompletionListener", "(Landroid/media/MediaPlayer$OnCompletionListener;)V", handlers.handler_android_media_player_set_completion_listener);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

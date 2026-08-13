#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_AudioManager(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/media/AudioManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getRingerMode", "()I", handlers.handler_android_audio_manager_get_ringer_mode);
    builder.Virtual("isMusicActive", "()Z", handlers.handler_android_audio_manager_is_music_active);
    builder.Virtual("getStreamMaxVolume", "(I)I", handlers.handler_android_audio_manager_get_stream_max_volume);
    builder.Virtual("setStreamVolume", "(III)V", handlers.handler_android_audio_manager_set_stream_volume);
    builder.Virtual("getStreamVolume", "(I)I", handlers.handler_android_audio_manager_get_stream_max_volume);
    builder.Virtual("setStreamMute", "(IZ)V", handlers.handler_android_audio_manager_set_stream_volume);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

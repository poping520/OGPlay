#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_SoundPool(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/media/SoundPool;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(III)V", handlers.handler_android_sound_pool_init);
    builder.Virtual("load", "(Landroid/content/Context;II)I", handlers.handler_android_sound_pool_load);
    builder.Virtual("play", "(IFFIIF)I", handlers.handler_android_sound_pool_play);
    builder.Virtual("pause", "(I)V", handlers.handler_android_sound_pool_pause);
    builder.Virtual("resume", "(I)V", handlers.handler_android_sound_pool_resume);
    builder.Virtual("stop", "(I)V", handlers.handler_android_sound_pool_stop);
    builder.Virtual("unload", "(I)Z", handlers.handler_android_sound_pool_unload);
    builder.Virtual("release", "()V", handlers.handler_android_sound_pool_release);
    builder.Virtual("setVolume", "(IFF)V", handlers.handler_android_sound_pool_set_volume);
    builder.Virtual("setRate", "(IF)V", handlers.handler_android_sound_pool_set_rate);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

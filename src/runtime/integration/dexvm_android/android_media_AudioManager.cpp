#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_AudioManager(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/media/AudioManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getRingerMode", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(2);  // RINGER_MODE_NORMAL
    });
    builder.Virtual("isMusicActive", "()Z", [](dx::IntrinsicContext&) {
        // OGPlay owns the session mixer and has no external media session.
        return dx::VmValue::Int(0);
    });
    const auto max_volume = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(15); });
    const auto set_volume = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Virtual("getStreamMaxVolume", "(I)I", max_volume);
    builder.Virtual("setStreamVolume", "(III)V", set_volume);
    builder.Virtual("getStreamVolume", "(I)I", max_volume);
    builder.Virtual("setStreamMute", "(IZ)V", set_volume);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

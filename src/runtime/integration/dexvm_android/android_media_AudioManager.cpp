#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_AudioManager(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/media/AudioManager;", "Ljava/lang/Object;");
    builder.FinalMethod("getRingerMode", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(2);  // RINGER_MODE_NORMAL
    });
    builder.FinalMethod("isMusicActive", "()Z", [](dx::IntrinsicContext&) {
        // OGPlay owns the session mixer and has no external media session.
        return dx::VmValue::Int(0);
    });
    const auto max_volume = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(15); });
    const auto set_volume = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("getStreamMaxVolume", "(I)I", max_volume);
    builder.FinalMethod("setStreamVolume", "(III)V", set_volume);
    builder.FinalMethod("getStreamVolume", "(I)I", max_volume);
    builder.FinalMethod("setStreamMute", "(IZ)V", set_volume);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_MediaPlayer_OnPreparedListener(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/media/MediaPlayer$OnPreparedListener;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

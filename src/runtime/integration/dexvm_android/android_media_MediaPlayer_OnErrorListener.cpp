#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_MediaPlayer_OnErrorListener(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/media/MediaPlayer$OnErrorListener;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

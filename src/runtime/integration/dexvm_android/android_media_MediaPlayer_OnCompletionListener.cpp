#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_MediaPlayer_OnCompletionListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/media/MediaPlayer$OnCompletionListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

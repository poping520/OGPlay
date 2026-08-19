#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_MediaPlayer_OnPreparedListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/media/MediaPlayer$OnPreparedListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

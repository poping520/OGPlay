#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_MediaPlayer_OnPreparedListener(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/media/MediaPlayer$OnPreparedListener;");
}

}  // namespace ogplay::runtime::android_intrinsics

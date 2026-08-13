#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_MediaPlayer_OnErrorListener(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/media/MediaPlayer$OnErrorListener;");
}

}  // namespace ogplay::runtime::android_intrinsics

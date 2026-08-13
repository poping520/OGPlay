#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_MediaPlayer(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/media/MediaPlayer;");
}

}  // namespace ogplay::runtime::android_intrinsics

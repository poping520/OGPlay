#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_MediaPlayer_OnCompletionListener(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/media/MediaPlayer$OnCompletionListener;");
}

}  // namespace ogplay::runtime::android_intrinsics

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_AudioManager(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/media/AudioManager;");
}

}  // namespace ogplay::runtime::android_intrinsics

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_media_SoundPool(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/media/SoundPool;");
}

}  // namespace ogplay::runtime::android_intrinsics

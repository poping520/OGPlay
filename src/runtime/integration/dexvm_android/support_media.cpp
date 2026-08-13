// Migrated: SoundPool/MediaPlayer handler bodies now live in their
// per-class declaration files (android_media_SoundPool.cpp /
// android_media_MediaPlayer.cpp). This empty batch keeps the assembly
// linking until the AndroidHandlers scaffold is removed.

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

void PopulateAudioVideo(AndroidHandlers& handlers, const Context& context) {
    static_cast<void>(handlers);
    static_cast<void>(context);
}

}  // namespace ogplay::runtime::android_intrinsics

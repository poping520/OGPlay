#pragma once

// name, stable module-local id, A32 word parameter count, concrete method
#define OGPLAY_ANDROID_BOUNDARY_EXPORTS(X)                                      \
    X("AConfiguration_new", 0, 0, ConfigurationNew)                            \
    X("AConfiguration_delete", 1, 1, ConfigurationDelete)                     \
    X("AConfiguration_fromAssetManager", 2, 2, ConfigurationFromAssetManager) \
    X("AConfiguration_getLanguage", 3, 2, ConfigurationGetLanguage)            \
    X("AConfiguration_getCountry", 4, 2, ConfigurationGetCountry)              \
    X("ALooper_prepare", 5, 1, LooperPrepare)                                  \
    X("ALooper_addFd", 6, 6, LooperAddFd)                                      \
    X("ALooper_pollAll", 7, 4, LooperPollAll)                                  \
    X("AInputQueue_attachLooper", 8, 5, InputQueueAttachLooper)                 \
    X("AInputQueue_detachLooper", 9, 1, InputQueueDetachLooper)                \
    X("AInputQueue_getEvent", 10, 2, InputQueueGetEvent)                       \
    X("AInputQueue_preDispatchEvent", 11, 2, InputQueuePreDispatchEvent)       \
    X("AInputQueue_finishEvent", 12, 3, InputQueueFinishEvent)                 \
    X("AInputEvent_getType", 13, 1, InputEventGetType)                         \
    X("AKeyEvent_getAction", 14, 1, KeyEventGetAction)                         \
    X("AKeyEvent_getKeyCode", 15, 1, KeyEventGetKeyCode)                       \
    X("AMotionEvent_getAction", 16, 1, MotionEventGetAction)                   \
    X("AMotionEvent_getX", 17, 2, MotionEventGetX)                             \
    X("AMotionEvent_getY", 18, 2, MotionEventGetY)                             \
    X("ANativeWindow_setBuffersGeometry", 19, 4, NativeWindowSetGeometry) \
    X("AAssetManager_open", 20, 3, AAssetManager_open) \
    X("AAsset_read", 21, 3, AAsset_read) \
    X("AAsset_close", 22, 1, AAsset_close) \
    X("AAsset_getLength", 23, 1, AAsset_getLength) \
    X("AAsset_getRemainingLength", 24, 1, AAsset_getRemainingLength) \
    X("AAsset_seek", 25, 3, AAsset_seek) \
    X("ANativeWindow_getWidth", 26, 1, ANativeWindow_getWidth) \
    X("ANativeWindow_getHeight", 27, 1, ANativeWindow_getHeight) \
    X("ANativeWindow_getFormat", 28, 1, ANativeWindow_getFormat) \
    X("ANativeWindow_acquire", 29, 1, ANativeWindow_acquire) \
    X("ANativeWindow_release", 30, 1, ANativeWindow_release) \
    X("ALooper_forThread", 31, 0, LooperForThread) \
    X("ALooper_acquire", 32, 1, LooperAcquire) \
    X("ALooper_release", 33, 1, LooperRelease) \
    X("ALooper_pollOnce", 34, 4, LooperPollOnce) \
    X("ALooper_wake", 35, 1, LooperWake) \
    X("ALooper_removeFd", 36, 2, LooperRemoveFd)

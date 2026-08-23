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
    X("ANativeWindow_setBuffersGeometry", 19, 4, NativeWindowSetGeometry)

#pragma once

// name, stable module-local id, A32 word parameter count, concrete method
#define OGPLAY_EGL_BOUNDARY_EXPORTS(X)                                          \
    X("eglGetDisplay", 0, 1, GetDisplay)                                       \
    X("eglInitialize", 1, 3, Initialize)                                       \
    X("eglChooseConfig", 2, 5, ChooseConfig)                                   \
    X("eglGetConfigAttrib", 3, 4, GetConfigAttrib)                             \
    X("eglCreateWindowSurface", 4, 4, CreateWindowSurface)                     \
    X("eglCreateContext", 5, 4, CreateContext)                                 \
    X("eglMakeCurrent", 6, 4, MakeCurrent)                                     \
    X("eglQuerySurface", 7, 4, QuerySurface)                                   \
    X("eglSwapBuffers", 8, 2, SwapBuffers)                                     \
    X("eglDestroyContext", 9, 2, DestroyContext)                               \
    X("eglDestroySurface", 10, 2, DestroySurface)                              \
    X("eglTerminate", 11, 1, Terminate)                                       \
    X("eglGetError", 12, 0, GetError)                                         \
    X("eglQueryString", 13, 2, QueryString)                                   \
    X("eglGetProcAddress", 14, 1, GetProcAddress)                             \
    X("eglGetConfigs", 15, 4, GetConfigs)                                     \
    X("eglGetCurrentContext", 16, 0, GetCurrentContext)                       \
    X("eglGetCurrentSurface", 17, 1, GetCurrentSurface)                       \
    X("eglGetCurrentDisplay", 18, 0, GetCurrentDisplay)                       \
    X("eglQueryContext", 19, 4, QueryContext)                                 \
    X("eglBindAPI", 20, 1, BindApi)                                           \
    X("eglQueryAPI", 21, 0, QueryApi)                                         \
    X("eglReleaseThread", 22, 0, ReleaseThread)                               \
    X("eglSwapInterval", 23, 2, SwapInterval)                                 \
    X("eglCreatePbufferSurface", 24, 3, CreatePbufferSurface)

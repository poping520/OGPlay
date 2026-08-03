#include <android/input.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <cstdint>

namespace {

struct SampleState final {
    android_app* app{};
    EGLDisplay display{EGL_NO_DISPLAY};
    EGLSurface surface{EGL_NO_SURFACE};
    EGLContext context{EGL_NO_CONTEXT};
    int width{};
    int height{};
    float red{0.08F};
    float green{0.18F};
    float blue{0.36F};
    bool animating{};
    bool redraw{true};
};

void ShutdownDisplay(SampleState& state) {
    if (state.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (state.context != EGL_NO_CONTEXT) eglDestroyContext(state.display, state.context);
        if (state.surface != EGL_NO_SURFACE) eglDestroySurface(state.display, state.surface);
        eglTerminate(state.display);
    }
    state.display = EGL_NO_DISPLAY;
    state.surface = EGL_NO_SURFACE;
    state.context = EGL_NO_CONTEXT;
    state.width = 0;
    state.height = 0;
}

bool InitializeDisplay(SampleState& state) {
    const EGLint attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLConfig config{};
    EGLint config_count{};
    EGLint format{};

    state.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (state.display == EGL_NO_DISPLAY || eglInitialize(state.display, nullptr, nullptr) == EGL_FALSE ||
        eglChooseConfig(state.display, attributes, &config, 1, &config_count) == EGL_FALSE ||
        config_count != 1 || eglGetConfigAttrib(state.display, config, EGL_NATIVE_VISUAL_ID, &format) == EGL_FALSE) {
        ShutdownDisplay(state);
        return false;
    }
    ANativeWindow_setBuffersGeometry(state.app->window, 0, 0, format);
    state.surface = eglCreateWindowSurface(state.display, config, state.app->window, nullptr);
    state.context = eglCreateContext(state.display, config, EGL_NO_CONTEXT, context_attributes);
    if (state.surface == EGL_NO_SURFACE || state.context == EGL_NO_CONTEXT ||
        eglMakeCurrent(state.display, state.surface, state.surface, state.context) == EGL_FALSE) {
        ShutdownDisplay(state);
        return false;
    }
    eglQuerySurface(state.display, state.surface, EGL_WIDTH, &state.width);
    eglQuerySurface(state.display, state.surface, EGL_HEIGHT, &state.height);
    state.redraw = true;
    return true;
}

void DrawFrame(SampleState& state) {
    if (state.display == EGL_NO_DISPLAY) return;
    glViewport(0, 0, state.width, state.height);
    glClearColor(state.red, state.green, state.blue, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(state.display, state.surface);
    state.redraw = false;
}

int32_t HandleInput(android_app* app, AInputEvent* event) {
    auto& state = *static_cast<SampleState*>(app->userData);
    const auto type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        const auto action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (action != AMOTION_EVENT_ACTION_DOWN && action != AMOTION_EVENT_ACTION_MOVE) return 0;
        const auto width = std::max(1, state.width);
        const auto height = std::max(1, state.height);
        state.red = std::clamp(AMotionEvent_getX(event, 0) / static_cast<float>(width), 0.0F, 1.0F);
        state.green = std::clamp(AMotionEvent_getY(event, 0) / static_cast<float>(height), 0.0F, 1.0F);
        state.blue = 0.25F + 0.5F * (1.0F - state.red);
        state.redraw = true;
        return 1;
    }
    if (type == AINPUT_EVENT_TYPE_KEY && AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN) {
        const auto key = static_cast<std::uint32_t>(AKeyEvent_getKeyCode(event));
        state.red = static_cast<float>((key * 37U) & 0xffU) / 255.0F;
        state.green = static_cast<float>((key * 73U) & 0xffU) / 255.0F;
        state.blue = static_cast<float>((key * 109U) & 0xffU) / 255.0F;
        state.redraw = true;
        return 1;
    }
    return 0;
}

void HandleCommand(android_app* app, const int32_t command) {
    auto& state = *static_cast<SampleState*>(app->userData);
    switch (command) {
    case APP_CMD_INIT_WINDOW:
        if (app->window != nullptr) InitializeDisplay(state);
        break;
    case APP_CMD_TERM_WINDOW:
        ShutdownDisplay(state);
        break;
    case APP_CMD_WINDOW_RESIZED:
        if (state.display != EGL_NO_DISPLAY) {
            eglQuerySurface(state.display, state.surface, EGL_WIDTH, &state.width);
            eglQuerySurface(state.display, state.surface, EGL_HEIGHT, &state.height);
            state.redraw = true;
        }
        break;
    case APP_CMD_GAINED_FOCUS:
        state.animating = true;
        state.redraw = true;
        break;
    case APP_CMD_LOST_FOCUS:
        state.animating = false;
        break;
    default:
        break;
    }
}

}  // namespace

void android_main(android_app* app) {
    SampleState state;
    state.app = app;
    app->userData = &state;
    app->onAppCmd = HandleCommand;
    app->onInputEvent = HandleInput;

    while (app->destroyRequested == 0) {
        android_poll_source* source{};
        const int timeout = state.animating ? 16 : -1;
        const auto identifier = ALooper_pollAll(timeout, nullptr, nullptr,
                                                reinterpret_cast<void**>(&source));
        if (identifier >= 0 && source != nullptr) source->process(app, source);
        if (state.animating || state.redraw) DrawFrame(state);
    }
    ShutdownDisplay(state);
}

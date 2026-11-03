#include <EGL/egl.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <sys/stat.h>

#include <algorithm>
#include <ctime>
#include <string>

#include "audio.h"
#include "game.h"
#include "gfx.h"

namespace sweetshop {
namespace {

struct Engine {
    android_app* app = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0, height = 0;
    bool resumed = false;
    double last_time = 0;
    Renderer renderer;
    Audio audio;
    Game game;
};

double Now() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9;
}

void TearDownDisplay(Engine& engine) {
    if (engine.display == EGL_NO_DISPLAY) return;
    if (engine.renderer.Ready()) engine.renderer.Shutdown();
    eglMakeCurrent(engine.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (engine.context != EGL_NO_CONTEXT) eglDestroyContext(engine.display, engine.context);
    if (engine.surface != EGL_NO_SURFACE) eglDestroySurface(engine.display, engine.surface);
    eglTerminate(engine.display);
    engine.display = EGL_NO_DISPLAY;
    engine.surface = EGL_NO_SURFACE;
    engine.context = EGL_NO_CONTEXT;
}

bool SetUpDisplay(Engine& engine) {
    const EGLint attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                 EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0, format = 0;
    engine.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (engine.display == EGL_NO_DISPLAY || eglInitialize(engine.display, nullptr, nullptr) == EGL_FALSE ||
        eglChooseConfig(engine.display, attributes, &config, 1, &count) == EGL_FALSE || count < 1 ||
        eglGetConfigAttrib(engine.display, config, EGL_NATIVE_VISUAL_ID, &format) == EGL_FALSE) {
        __android_log_print(ANDROID_LOG_ERROR, "sweetshop", "EGL display setup failed");
        TearDownDisplay(engine);
        return false;
    }
    ANativeWindow_setBuffersGeometry(engine.app->window, 0, 0, format);
    engine.surface = eglCreateWindowSurface(engine.display, config, engine.app->window, nullptr);
    engine.context = eglCreateContext(engine.display, config, EGL_NO_CONTEXT, context_attributes);
    if (engine.surface == EGL_NO_SURFACE || engine.context == EGL_NO_CONTEXT ||
        eglMakeCurrent(engine.display, engine.surface, engine.surface, engine.context) == EGL_FALSE) {
        __android_log_print(ANDROID_LOG_ERROR, "sweetshop", "EGL surface setup failed");
        TearDownDisplay(engine);
        return false;
    }
    eglQuerySurface(engine.display, engine.surface, EGL_WIDTH, &engine.width);
    eglQuerySurface(engine.display, engine.surface, EGL_HEIGHT, &engine.height);
    if (!engine.renderer.Init(engine.app->activity->assetManager)) {
        TearDownDisplay(engine);
        return false;
    }
    engine.last_time = Now();
    return true;
}

void DrawFrame(Engine& engine) {
    if (engine.display == EGL_NO_DISPLAY || !engine.renderer.Ready()) return;
    const double now = Now();
    const float dt = static_cast<float>(std::clamp(now - engine.last_time, 0.0, 0.05));
    engine.last_time = now;
    engine.game.Update(dt);
    eglQuerySurface(engine.display, engine.surface, EGL_WIDTH, &engine.width);
    eglQuerySurface(engine.display, engine.surface, EGL_HEIGHT, &engine.height);
    engine.renderer.BeginFrame(engine.width, engine.height);
    engine.game.Render(engine.renderer);
    engine.renderer.EndFrame();
    eglSwapBuffers(engine.display, engine.surface);
}

int32_t HandleInput(android_app* app, AInputEvent* event) {
    auto& engine = *static_cast<Engine*>(app->userData);
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        float x = 0, y = 0;
        const bool inside = engine.renderer.ToLogical(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0), &x, &y);
        switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
            if (inside) engine.game.PointerDown(x, y);
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            engine.game.PointerMove(x, y);
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            engine.game.PointerUp(x, y);
            break;
        default:
            break;
        }
        return 1;
    }
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY && AKeyEvent_getKeyCode(event) == AKEYCODE_BACK) {
        // Consume both edges; act on release so the title screen can hand the key back.
        if (AKeyEvent_getAction(event) != AKEY_EVENT_ACTION_UP) return 1;
        return engine.game.Back() ? 1 : 0;
    }
    return 0;
}

void HandleCommand(android_app* app, const int32_t command) {
    auto& engine = *static_cast<Engine*>(app->userData);
    switch (command) {
    case APP_CMD_INIT_WINDOW:
        if (app->window != nullptr && SetUpDisplay(engine)) DrawFrame(engine);
        break;
    case APP_CMD_TERM_WINDOW:
        TearDownDisplay(engine);
        break;
    case APP_CMD_RESUME:
        engine.resumed = true;
        engine.audio.Resume();
        break;
    case APP_CMD_PAUSE:
        engine.resumed = false;
        engine.audio.Pause();
        engine.game.Save();
        break;
    case APP_CMD_SAVE_STATE:
    case APP_CMD_STOP:
        engine.game.Save();
        break;
    default:
        break;
    }
}

// Chinese devices get Chinese text; every other language falls back to English.
bool DeviceIsEnglish(const android_app* app) {
    char language[2] = {};
    if (app->config == nullptr) return false;
    AConfiguration_getLanguage(app->config, language);
    return !(language[0] == 'z' && language[1] == 'h');
}

std::string SavePath(const ANativeActivity* activity) {
    const char* directory = activity->internalDataPath;
    if (directory == nullptr || *directory == '\0') return {};
    mkdir(directory, 0700);
    return std::string(directory) + "/progress.txt";
}

}  // namespace
}  // namespace sweetshop

void android_main(android_app* app) {
    using namespace sweetshop;
    auto* engine = new Engine;
    engine->app = app;
    app->userData = engine;
    app->onAppCmd = HandleCommand;
    app->onInputEvent = HandleInput;
    engine->audio.Init();
    engine->game.Init(&engine->audio, SavePath(app->activity), DeviceIsEnglish(app));

    while (true) {
        int events = 0;
        android_poll_source* source = nullptr;
        while (ALooper_pollAll(engine->display != EGL_NO_DISPLAY ? 0 : -1, nullptr, &events,
                               reinterpret_cast<void**>(&source)) >= 0) {
            if (source != nullptr) source->process(app, source);
            if (app->destroyRequested != 0) {
                engine->game.Save();
                TearDownDisplay(*engine);
                engine->audio.Shutdown();
                delete engine;
                app->userData = nullptr;
                return;
            }
        }
        DrawFrame(*engine);
    }
}

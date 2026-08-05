#include <android/input.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

constexpr std::array<GLushort, 6> kQuadIndices{0, 1, 2, 0, 2, 3};
constexpr char kVertexShader[] = R"(
attribute vec2 aPosition;
attribute vec4 aColor;
attribute vec2 aTexCoord;
uniform mat3 uTransform;
varying vec4 vColor;
varying vec2 vTexCoord;
void main() {
    vec3 transformed = uTransform * vec3(aPosition, 1.0);
    gl_Position = vec4(transformed.xy, 0.0, 1.0);
    vColor = aColor;
    vTexCoord = aTexCoord;
}
)";
constexpr char kFragmentShader[] = R"(
precision mediump float;
varying vec4 vColor;
varying vec2 vTexCoord;
uniform sampler2D uTexture;
uniform float uTextured;
uniform vec4 uTint;
void main() {
    vec4 sampled = texture2D(uTexture, vTexCoord) * uTint;
    gl_FragColor = mix(vColor, sampled, uTextured);
}
)";

struct Vertex final {
    GLfloat x;
    GLfloat y;
    GLfloat red;
    GLfloat green;
    GLfloat blue;
    GLfloat alpha;
    GLfloat u;
    GLfloat v;
};

struct Renderer final {
    GLuint program{};
    GLuint vertex_shader{};
    GLuint fragment_shader{};
    GLuint vertex_buffer{};
    GLuint index_buffer{};
    GLuint texture{};
    GLint position{-1};
    GLint color{-1};
    GLint texcoord{-1};
    GLint transform{-1};
    GLint textured{-1};
    GLint tint{-1};
    GLint sampler{-1};
    bool healthy{};
};

struct SampleState final {
    android_app* app{};
    EGLDisplay display{EGL_NO_DISPLAY};
    EGLSurface surface{EGL_NO_SURFACE};
    EGLContext context{EGL_NO_CONTEXT};
    EGLConfig config{};
    Renderer renderer{};
    int width{};
    int height{};
    float marker_x{};
    float marker_y{};
    std::uint32_t palette{};
    bool animating{};
    bool redraw{true};
};

GLuint CompileShader(const GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0U) return 0U;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return shader;
    glDeleteShader(shader);
    return 0U;
}

void DestroyRenderer(Renderer& renderer) {
    if (renderer.texture != 0U) glDeleteTextures(1, &renderer.texture);
    if (renderer.index_buffer != 0U) glDeleteBuffers(1, &renderer.index_buffer);
    if (renderer.vertex_buffer != 0U) glDeleteBuffers(1, &renderer.vertex_buffer);
    if (renderer.program != 0U) glDeleteProgram(renderer.program);
    if (renderer.fragment_shader != 0U) glDeleteShader(renderer.fragment_shader);
    if (renderer.vertex_shader != 0U) glDeleteShader(renderer.vertex_shader);
    renderer = {};
}

std::array<std::uint8_t, 8U * 8U * 4U> MakeTexture() {
    std::array<std::uint8_t, 8U * 8U * 4U> pixels{};
    for (std::size_t y = 0; y < 8U; ++y) {
        for (std::size_t x = 0; x < 8U; ++x) {
            const std::size_t offset = (y * 8U + x) * 4U;
            const bool checker = ((x / 2U) + (y / 2U)) % 2U == 0U;
            pixels[offset] = checker ? static_cast<std::uint8_t>(32U + x * 24U) : 238U;
            pixels[offset + 1U] = checker ? static_cast<std::uint8_t>(40U + y * 22U) : 52U;
            pixels[offset + 2U] = (x < y) ? 224U : 72U;
            pixels[offset + 3U] = 255U;
        }
    }
    return pixels;
}

bool InitializeRenderer(Renderer& renderer) {
    renderer.vertex_shader = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    renderer.fragment_shader = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (renderer.vertex_shader == 0U || renderer.fragment_shader == 0U) return false;

    renderer.program = glCreateProgram();
    glAttachShader(renderer.program, renderer.vertex_shader);
    glAttachShader(renderer.program, renderer.fragment_shader);
    glLinkProgram(renderer.program);
    GLint linked = GL_FALSE;
    glGetProgramiv(renderer.program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) return false;

    renderer.position = glGetAttribLocation(renderer.program, "aPosition");
    renderer.color = glGetAttribLocation(renderer.program, "aColor");
    renderer.texcoord = glGetAttribLocation(renderer.program, "aTexCoord");
    renderer.transform = glGetUniformLocation(renderer.program, "uTransform");
    renderer.textured = glGetUniformLocation(renderer.program, "uTextured");
    renderer.tint = glGetUniformLocation(renderer.program, "uTint");
    renderer.sampler = glGetUniformLocation(renderer.program, "uTexture");
    if (renderer.position < 0 || renderer.color < 0 || renderer.texcoord < 0 ||
        renderer.transform < 0 || renderer.textured < 0 || renderer.tint < 0 ||
        renderer.sampler < 0) {
        return false;
    }

    glGenBuffers(1, &renderer.vertex_buffer);
    glGenBuffers(1, &renderer.index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer.index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIndices), kQuadIndices.data(),
                 GL_STATIC_DRAW);

    const auto texture_pixels = MakeTexture();
    glGenTextures(1, &renderer.texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 texture_pixels.data());

    glUseProgram(renderer.program);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vertex_buffer);
    glEnableVertexAttribArray(static_cast<GLuint>(renderer.position));
    glEnableVertexAttribArray(static_cast<GLuint>(renderer.color));
    glEnableVertexAttribArray(static_cast<GLuint>(renderer.texcoord));
    glVertexAttribPointer(static_cast<GLuint>(renderer.position), 2, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, x)));
    glVertexAttribPointer(static_cast<GLuint>(renderer.color), 4, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, red)));
    glVertexAttribPointer(static_cast<GLuint>(renderer.texcoord), 2, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, u)));
    constexpr std::array<GLfloat, 9> identity{1.0F, 0.0F, 0.0F,
                                             0.0F, 1.0F, 0.0F,
                                             0.0F, 0.0F, 1.0F};
    glUniformMatrix3fv(renderer.transform, 1, GL_FALSE, identity.data());
    glUniform1i(renderer.sampler, 0);

    GLint max_texture_size{};
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    renderer.healthy = glGetString(GL_VERSION) != nullptr &&
                       glGetString(GL_RENDERER) != nullptr && max_texture_size >= 64 &&
                       glGetError() == GL_NO_ERROR;
    return renderer.healthy;
}

void DrawQuad(const Renderer& renderer, const float left, const float bottom,
              const float right, const float top, const std::array<float, 4>& color,
              const bool textured) {
    const std::array<Vertex, 4> vertices{{
        {left, bottom, color[0], color[1], color[2], color[3], 0.0F, 0.0F},
        {right, bottom, color[0], color[1], color[2], color[3], 1.0F, 0.0F},
        {right, top, color[0], color[1], color[2], color[3], 1.0F, 1.0F},
        {left, top, color[0], color[1], color[2], color[3], 0.0F, 1.0F},
    }};
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STREAM_DRAW);
    glUniform1f(renderer.textured, textured ? 1.0F : 0.0F);
    glUniform4f(renderer.tint, color[0], color[1], color[2], color[3]);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(kQuadIndices.size()),
                   GL_UNSIGNED_SHORT, nullptr);
}

bool Near(const std::uint8_t value, const std::uint8_t expected) {
    return std::abs(static_cast<int>(value) - static_cast<int>(expected)) <= 3;
}

bool CheckStablePixels(const int width, const int height) {
    if (width < 16 || height < 16) return false;
    std::array<std::uint8_t, 4> orange{};
    std::array<std::uint8_t, 4> background{};
    glReadPixels(2, height - 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, orange.data());
    glReadPixels(width - 2, height - 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                 background.data());
    return Near(orange[0], 230U) && Near(orange[1], 64U) && Near(orange[2], 31U) &&
           Near(orange[3], 255U) && Near(background[0], 13U) &&
           Near(background[1], 20U) && Near(background[2], 38U) &&
           Near(background[3], 255U) && glGetError() == GL_NO_ERROR;
}

void DrawFrame(SampleState& state) {
    if (state.display == EGL_NO_DISPLAY) return;
    glViewport(0, 0, state.width, state.height);
    if (!state.renderer.healthy) {
        glClearColor(0.75F, 0.0F, 0.55F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(state.display, state.surface);
        state.redraw = false;
        return;
    }

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.05F, 0.08F, 0.15F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, state.height * 3 / 4, std::max(1, state.width / 4),
              std::max(1, state.height / 4));
    glClearColor(0.90F, 0.25F, 0.12F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(state.renderer.program);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.renderer.index_buffer);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state.renderer.texture);
    using Color = std::array<float, 4>;
    using Palette = std::array<Color, 3>;
    const std::array<Palette, 3> palettes{{
        Palette{{Color{0.92F, 0.20F, 0.24F, 1.0F},
                 Color{0.18F, 0.78F, 0.42F, 1.0F},
                 Color{0.18F, 0.45F, 0.94F, 1.0F}}},
        Palette{{Color{0.94F, 0.62F, 0.12F, 1.0F},
                 Color{0.65F, 0.25F, 0.92F, 1.0F},
                 Color{0.12F, 0.78F, 0.82F, 1.0F}}},
        Palette{{Color{0.88F, 0.32F, 0.62F, 1.0F},
                 Color{0.72F, 0.82F, 0.16F, 1.0F},
                 Color{0.32F, 0.52F, 0.92F, 1.0F}}},
    }};
    const auto& palette = palettes[state.palette % palettes.size()];
    DrawQuad(state.renderer, -0.88F, -0.72F, -0.68F, 0.42F, palette[0], false);
    DrawQuad(state.renderer, -0.60F, -0.72F, -0.40F, 0.18F, palette[1], false);
    DrawQuad(state.renderer, -0.32F, -0.72F, -0.12F, -0.05F, palette[2], false);
    DrawQuad(state.renderer, 0.04F, -0.58F, 0.72F, 0.50F,
             {1.0F, 1.0F, 1.0F, 1.0F}, true);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    DrawQuad(state.renderer, 0.36F, -0.30F, 0.86F, 0.30F,
             {0.75F, 0.18F, 0.90F, 0.48F}, false);
    glDisable(GL_BLEND);

    const float marker_size = 0.035F;
    DrawQuad(state.renderer, state.marker_x - marker_size, state.marker_y - marker_size,
             state.marker_x + marker_size, state.marker_y + marker_size,
             {1.0F, 1.0F, 1.0F, 1.0F}, false);
    const bool healthy = CheckStablePixels(state.width, state.height);
    DrawQuad(state.renderer, 0.78F, 0.72F, 0.92F, 0.90F,
             healthy ? std::array<float, 4>{0.15F, 0.95F, 0.30F, 1.0F}
                     : std::array<float, 4>{0.95F, 0.10F, 0.12F, 1.0F},
             false);
    eglSwapBuffers(state.display, state.surface);
    state.redraw = false;
}

void ShutdownDisplay(SampleState& state) {
    if (state.display != EGL_NO_DISPLAY) {
        if (state.context != EGL_NO_CONTEXT) DestroyRenderer(state.renderer);
        eglMakeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (state.context != EGL_NO_CONTEXT) eglDestroyContext(state.display, state.context);
        if (state.surface != EGL_NO_SURFACE) eglDestroySurface(state.display, state.surface);
        eglTerminate(state.display);
    }
    state.display = EGL_NO_DISPLAY;
    state.surface = EGL_NO_SURFACE;
    state.context = EGL_NO_CONTEXT;
    state.config = {};
    state.width = 0;
    state.height = 0;
}

bool InitializeDisplay(SampleState& state) {
    const EGLint attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE,
    };
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLint config_count{};
    EGLint format{};
    state.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (state.display == EGL_NO_DISPLAY ||
        eglInitialize(state.display, nullptr, nullptr) == EGL_FALSE ||
        eglChooseConfig(state.display, attributes, &state.config, 1, &config_count) == EGL_FALSE ||
        config_count != 1 ||
        eglGetConfigAttrib(state.display, state.config, EGL_NATIVE_VISUAL_ID, &format) == EGL_FALSE) {
        ShutdownDisplay(state);
        return false;
    }
    ANativeWindow_setBuffersGeometry(state.app->window, 0, 0, format);
    state.surface = eglCreateWindowSurface(state.display, state.config, state.app->window, nullptr);
    state.context = eglCreateContext(state.display, state.config, EGL_NO_CONTEXT,
                                     context_attributes);
    if (state.surface == EGL_NO_SURFACE || state.context == EGL_NO_CONTEXT ||
        eglMakeCurrent(state.display, state.surface, state.surface, state.context) == EGL_FALSE) {
        ShutdownDisplay(state);
        return false;
    }
    eglQuerySurface(state.display, state.surface, EGL_WIDTH, &state.width);
    eglQuerySurface(state.display, state.surface, EGL_HEIGHT, &state.height);
    state.marker_x = 0.0F;
    state.marker_y = 0.0F;
    state.renderer.healthy = InitializeRenderer(state.renderer);
    state.redraw = true;
    return true;
}

int32_t HandleInput(android_app* app, AInputEvent* event) {
    auto& state = *static_cast<SampleState*>(app->userData);
    const int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) return 1;
        if (action != AMOTION_EVENT_ACTION_DOWN && action != AMOTION_EVENT_ACTION_MOVE) return 0;
        const float width = static_cast<float>(std::max(1, state.width));
        const float height = static_cast<float>(std::max(1, state.height));
        state.marker_x = std::clamp(2.0F * AMotionEvent_getX(event, 0) / width - 1.0F,
                                    -0.96F, 0.96F);
        state.marker_y = std::clamp(1.0F - 2.0F * AMotionEvent_getY(event, 0) / height,
                                    -0.96F, 0.96F);
        state.redraw = true;
        return 1;
    }
    if (type == AINPUT_EVENT_TYPE_KEY && AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN) {
        state.palette = (state.palette + 1U) % 3U;
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
    case APP_CMD_CONFIG_CHANGED:
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
        const int timeout = (state.animating && state.redraw) ? 0 : -1;
        const int identifier = ALooper_pollAll(timeout, nullptr, nullptr,
                                               reinterpret_cast<void**>(&source));
        if (identifier >= 0 && source != nullptr) source->process(app, source);
        if (state.redraw) DrawFrame(state);
    }
    ShutdownDisplay(state);
}

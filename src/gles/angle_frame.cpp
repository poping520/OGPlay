#include "ogplay/gles/angle_frame.h"

#include <algorithm>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#if OGPLAY_HAS_ANGLE
#include <GLES2/gl2.h>
#endif

namespace ogplay::gles {

AngleFrame AngleFrame::CreatePbuffer(const AngleBackend backend,
                                     const std::uint32_t width,
                                     const std::uint32_t height) {
    auto api = CreateNativeAngleEglApi();
    auto lifecycle = EglLifecycle::CreatePbuffer(*api, backend, width, height);
    return AngleFrame(std::move(api), std::move(lifecycle), width, height);
}

AngleFrame::AngleFrame(std::unique_ptr<EglApi> api, EglLifecycle lifecycle,
                       const std::uint32_t width,
                       const std::uint32_t height) noexcept
    : api_(std::move(api)), lifecycle_(std::move(lifecycle)),
      width_(width), height_(height) {}

AngleFrame::~AngleFrame() = default;
AngleFrame::AngleFrame(AngleFrame&&) noexcept = default;
AngleFrame& AngleFrame::operator=(AngleFrame&&) noexcept = default;

void AngleFrame::Viewport(const std::int32_t x, const std::int32_t y,
                          const std::int32_t width,
                          const std::int32_t height) {
    if (width < 0 || height < 0) {
        throw std::invalid_argument("ANGLE viewport dimensions must not be negative");
    }
#if OGPLAY_HAS_ANGLE
    glViewport(x, y, width, height);
    RequireNoError("glViewport");
#else
    static_cast<void>(x);
    static_cast<void>(y);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::Scissor(const std::int32_t x, const std::int32_t y,
                         const std::int32_t width,
                         const std::int32_t height) {
    if (width < 0 || height < 0) {
        throw std::invalid_argument("ANGLE scissor dimensions must not be negative");
    }
#if OGPLAY_HAS_ANGLE
    glScissor(x, y, width, height);
    RequireNoError("glScissor");
#else
    static_cast<void>(x);
    static_cast<void>(y);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::SetScissorEnabled(const bool enabled) {
#if OGPLAY_HAS_ANGLE
    if (enabled) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
    RequireNoError(enabled ? "glEnable(GL_SCISSOR_TEST)"
                           : "glDisable(GL_SCISSOR_TEST)");
#else
    static_cast<void>(enabled);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::ClearColor(const float red, const float green,
                            const float blue, const float alpha) {
#if OGPLAY_HAS_ANGLE
    glClearColor(red, green, blue, alpha);
    RequireNoError("glClearColor");
#else
    static_cast<void>(red);
    static_cast<void>(green);
    static_cast<void>(blue);
    static_cast<void>(alpha);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::Clear(const std::uint32_t mask) {
#if OGPLAY_HAS_ANGLE
    glClear(mask);
    RequireNoError("glClear");
    ++clear_count_;
#else
    static_cast<void>(mask);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::uint32_t AngleFrame::CreateShader(const std::uint32_t type) {
#if OGPLAY_HAS_ANGLE
    const auto shader = glCreateShader(type);
    RequireNoError("glCreateShader");
    return shader;
#else
    static_cast<void>(type);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::ShaderSource(const std::uint32_t shader,
                              const std::span<const std::string> sources) {
    if (sources.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::length_error("ANGLE shader source count overflows GLsizei");
    }
#if OGPLAY_HAS_ANGLE
    std::vector<const char*> pointers;
    std::vector<GLint> lengths;
    pointers.reserve(sources.size());
    lengths.reserve(sources.size());
    for (const auto& source : sources) {
        if (source.size() > static_cast<std::size_t>(std::numeric_limits<GLint>::max())) {
            throw std::length_error("ANGLE shader source length overflows GLint");
        }
        pointers.push_back(source.data());
        lengths.push_back(static_cast<GLint>(source.size()));
    }
    glShaderSource(shader, static_cast<GLsizei>(sources.size()),
                   pointers.data(), lengths.data());
    RequireNoError("glShaderSource");
#else
    static_cast<void>(shader);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::CompileShader(const std::uint32_t shader) {
#if OGPLAY_HAS_ANGLE
    glCompileShader(shader);
    RequireNoError("glCompileShader");
    ++shader_compile_count_;
#else
    static_cast<void>(shader);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::int32_t AngleFrame::GetShaderParameter(const std::uint32_t shader,
                                             const std::uint32_t parameter) {
#if OGPLAY_HAS_ANGLE
    GLint value{};
    glGetShaderiv(shader, parameter, &value);
    RequireNoError("glGetShaderiv");
    return value;
#else
    static_cast<void>(shader);
    static_cast<void>(parameter);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::DeleteShader(const std::uint32_t shader) {
#if OGPLAY_HAS_ANGLE
    glDeleteShader(shader);
    RequireNoError("glDeleteShader");
#else
    static_cast<void>(shader);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::uint32_t AngleFrame::CreateProgram() {
#if OGPLAY_HAS_ANGLE
    const auto program = glCreateProgram();
    RequireNoError("glCreateProgram");
    return program;
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::AttachShader(const std::uint32_t program,
                              const std::uint32_t shader) {
#if OGPLAY_HAS_ANGLE
    glAttachShader(program, shader);
    RequireNoError("glAttachShader");
#else
    static_cast<void>(program);
    static_cast<void>(shader);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::LinkProgram(const std::uint32_t program) {
#if OGPLAY_HAS_ANGLE
    glLinkProgram(program);
    RequireNoError("glLinkProgram");
    ++program_link_count_;
#else
    static_cast<void>(program);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::int32_t AngleFrame::GetProgramParameter(const std::uint32_t program,
                                              const std::uint32_t parameter) {
#if OGPLAY_HAS_ANGLE
    GLint value{};
    glGetProgramiv(program, parameter, &value);
    RequireNoError("glGetProgramiv");
    return value;
#else
    static_cast<void>(program);
    static_cast<void>(parameter);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::int32_t AngleFrame::GetAttribLocation(const std::uint32_t program,
                                            const std::string& name) {
#if OGPLAY_HAS_ANGLE
    const auto location = glGetAttribLocation(program, name.c_str());
    RequireNoError("glGetAttribLocation");
    return location;
#else
    static_cast<void>(program);
    static_cast<void>(name);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::int32_t AngleFrame::GetUniformLocation(const std::uint32_t program,
                                             const std::string& name) {
#if OGPLAY_HAS_ANGLE
    const auto location = glGetUniformLocation(program, name.c_str());
    RequireNoError("glGetUniformLocation");
    return location;
#else
    static_cast<void>(program);
    static_cast<void>(name);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::UseProgram(const std::uint32_t program) {
#if OGPLAY_HAS_ANGLE
    glUseProgram(program);
    RequireNoError("glUseProgram");
#else
    static_cast<void>(program);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::DeleteProgram(const std::uint32_t program) {
#if OGPLAY_HAS_ANGLE
    glDeleteProgram(program);
    RequireNoError("glDeleteProgram");
#else
    static_cast<void>(program);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::vector<std::uint8_t> AngleFrame::ReadRgba8() {
    constexpr std::uint64_t kChannels = 4;
    const auto pixels = static_cast<std::uint64_t>(width_) * height_;
    if (pixels > std::numeric_limits<std::size_t>::max() / kChannels) {
        throw std::overflow_error("ANGLE frame readback size overflows");
    }
    std::vector<std::uint8_t> result(
        static_cast<std::size_t>(pixels * kChannels));
#if OGPLAY_HAS_ANGLE
    glFinish();
    RequireNoError("glFinish");
    glReadPixels(0, 0, static_cast<GLsizei>(width_),
                 static_cast<GLsizei>(height_), GL_RGBA, GL_UNSIGNED_BYTE,
                 result.data());
    RequireNoError("glReadPixels");
    const auto row_bytes = static_cast<std::size_t>(width_) * kChannels;
    for (std::size_t top = 0, bottom = height_ - 1U; top < bottom;
         ++top, --bottom) {
        const auto top_begin = result.begin() + static_cast<std::ptrdiff_t>(top * row_bytes);
        const auto bottom_begin = result.begin() + static_cast<std::ptrdiff_t>(bottom * row_bytes);
        std::swap_ranges(top_begin,
                         top_begin + static_cast<std::ptrdiff_t>(row_bytes),
                         bottom_begin);
    }
    ++readback_count_;
    return result;
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

AngleFrameInfo AngleFrame::Info() const noexcept {
    return {width_, height_, clear_count_, readback_count_,
            shader_compile_count_, program_link_count_};
}

void AngleFrame::RequireNoError(const char* const operation) const {
#if OGPLAY_HAS_ANGLE
    const auto error = glGetError();
    if (error != GL_NO_ERROR) {
        throw std::runtime_error(std::string(operation) +
                                 " failed with GLES error " +
                                 std::to_string(error));
    }
#else
    static_cast<void>(operation);
#endif
}

}  // namespace ogplay::gles

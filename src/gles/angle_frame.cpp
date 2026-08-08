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

void AngleFrame::ClearDepth(const float depth) {
#if OGPLAY_HAS_ANGLE
    glClearDepthf(depth);
    RequireNoError("glClearDepthf");
#else
    static_cast<void>(depth);
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

std::vector<std::uint32_t> AngleFrame::GenerateBuffers(const std::size_t count) {
    if (count > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) {
        throw std::length_error("ANGLE buffer count overflows GLsizei");
    }
#if OGPLAY_HAS_ANGLE
    std::vector<std::uint32_t> buffers(count);
    glGenBuffers(static_cast<GLsizei>(count), buffers.data());
    RequireNoError("glGenBuffers");
    return buffers;
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::DeleteBuffers(const std::span<const std::uint32_t> buffers) {
    if (buffers.size() > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) {
        throw std::length_error("ANGLE buffer count overflows GLsizei");
    }
#if OGPLAY_HAS_ANGLE
    glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
    RequireNoError("glDeleteBuffers");
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::BindBuffer(const std::uint32_t target, const std::uint32_t buffer) {
#if OGPLAY_HAS_ANGLE
    glBindBuffer(target, buffer);
    RequireNoError("glBindBuffer");
#else
    static_cast<void>(target); static_cast<void>(buffer);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::BufferData(
    const std::uint32_t target, const std::uint32_t byte_size,
    const std::optional<std::span<const std::byte>> data,
    const std::uint32_t usage) {
    if (data.has_value() && data->size() != byte_size) {
        throw std::invalid_argument("ANGLE buffer data size does not match its payload");
    }
#if OGPLAY_HAS_ANGLE
    glBufferData(target, static_cast<GLsizeiptr>(byte_size),
                 data.has_value() ? data->data() : nullptr, usage);
    RequireNoError("glBufferData");
#else
    static_cast<void>(target); static_cast<void>(usage);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::vector<std::uint32_t> AngleFrame::GenerateTextures(const std::size_t count) {
    if (count > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) {
        throw std::length_error("ANGLE texture count overflows GLsizei");
    }
#if OGPLAY_HAS_ANGLE
    std::vector<std::uint32_t> textures(count);
    glGenTextures(static_cast<GLsizei>(count), textures.data());
    RequireNoError("glGenTextures");
    return textures;
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::DeleteTextures(const std::span<const std::uint32_t> textures) {
    if (textures.size() > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) {
        throw std::length_error("ANGLE texture count overflows GLsizei");
    }
#if OGPLAY_HAS_ANGLE
    glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
    RequireNoError("glDeleteTextures");
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::ActiveTexture(const std::uint32_t texture) {
#if OGPLAY_HAS_ANGLE
    glActiveTexture(texture); RequireNoError("glActiveTexture");
#else
    static_cast<void>(texture); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::BindTexture(const std::uint32_t target, const std::uint32_t texture) {
#if OGPLAY_HAS_ANGLE
    glBindTexture(target, texture); RequireNoError("glBindTexture");
#else
    static_cast<void>(target); static_cast<void>(texture);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::PixelStore(const std::uint32_t parameter, const std::int32_t value) {
#if OGPLAY_HAS_ANGLE
    glPixelStorei(parameter, value); RequireNoError("glPixelStorei");
#else
    static_cast<void>(parameter); static_cast<void>(value);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::TextureParameter(const std::uint32_t target,
                                  const std::uint32_t parameter,
                                  const std::int32_t value) {
#if OGPLAY_HAS_ANGLE
    glTexParameteri(target, parameter, value); RequireNoError("glTexParameteri");
#else
    static_cast<void>(target); static_cast<void>(parameter); static_cast<void>(value);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::TextureParameterFloat(const std::uint32_t target,
                                       const std::uint32_t parameter,
                                       const float value) {
#if OGPLAY_HAS_ANGLE
    glTexParameterf(target, parameter, value);
    RequireNoError("glTexParameterf");
#else
    static_cast<void>(target);
    static_cast<void>(parameter);
    static_cast<void>(value);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::TextureImage2D(
    const std::uint32_t target, const std::int32_t level,
    const std::int32_t internal_format, const std::int32_t width,
    const std::int32_t height, const std::int32_t border,
    const std::uint32_t format, const std::uint32_t type,
    const std::optional<std::span<const std::byte>> pixels) {
#if OGPLAY_HAS_ANGLE
    glTexImage2D(target, level, internal_format, width, height, border, format, type,
                 pixels.has_value() ? pixels->data() : nullptr);
    RequireNoError("glTexImage2D");
#else
    static_cast<void>(target); static_cast<void>(level); static_cast<void>(internal_format);
    static_cast<void>(width); static_cast<void>(height); static_cast<void>(border);
    static_cast<void>(format); static_cast<void>(type);
    static_cast<void>(pixels);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::SetVertexAttributeEnabled(const std::uint32_t index,
                                            const bool enabled) {
#if OGPLAY_HAS_ANGLE
    if (enabled) glEnableVertexAttribArray(index);
    else glDisableVertexAttribArray(index);
    RequireNoError(enabled ? "glEnableVertexAttribArray" : "glDisableVertexAttribArray");
#else
    static_cast<void>(index); static_cast<void>(enabled);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::VertexAttributePointer(
    const std::uint32_t index, const std::int32_t size,
    const std::uint32_t type, const bool normalized,
    const std::int32_t stride, const std::uint32_t offset) {
#if OGPLAY_HAS_ANGLE
    glVertexAttribPointer(index, size, type, normalized ? GL_TRUE : GL_FALSE,
                          stride, reinterpret_cast<const void*>(
                                      static_cast<std::uintptr_t>(offset)));
    RequireNoError("glVertexAttribPointer");
#else
    static_cast<void>(index); static_cast<void>(size); static_cast<void>(type);
    static_cast<void>(normalized); static_cast<void>(stride); static_cast<void>(offset);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::Uniform1f(const std::int32_t location, const float value) {
#if OGPLAY_HAS_ANGLE
    glUniform1f(location, value); RequireNoError("glUniform1f");
#else
    static_cast<void>(location); static_cast<void>(value);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::Uniform1i(const std::int32_t location, const std::int32_t value) {
#if OGPLAY_HAS_ANGLE
    glUniform1i(location, value); RequireNoError("glUniform1i");
#else
    static_cast<void>(location); static_cast<void>(value);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::Uniform4f(const std::int32_t location, const float x,
                           const float y, const float z, const float w) {
#if OGPLAY_HAS_ANGLE
    glUniform4f(location, x, y, z, w); RequireNoError("glUniform4f");
#else
    static_cast<void>(location); static_cast<void>(x); static_cast<void>(y);
    static_cast<void>(z); static_cast<void>(w);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::UniformMatrix3(const std::int32_t location,
                                const std::int32_t count,
                                const bool transpose,
                                const std::span<const float> values) {
    if (count < 0 || static_cast<std::size_t>(count) > values.size() / 9U ||
        static_cast<std::size_t>(count) * 9U != values.size()) {
        throw std::invalid_argument("ANGLE matrix count does not match its payload");
    }
#if OGPLAY_HAS_ANGLE
    glUniformMatrix3fv(location, count, transpose ? GL_TRUE : GL_FALSE, values.data());
    RequireNoError("glUniformMatrix3fv");
#else
    static_cast<void>(location); static_cast<void>(transpose);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::SetCapability(const std::uint32_t capability,
                               const bool enabled) {
#if OGPLAY_HAS_ANGLE
    if (enabled) glEnable(capability);
    else glDisable(capability);
    RequireNoError(enabled ? "glEnable" : "glDisable");
#else
    static_cast<void>(capability); static_cast<void>(enabled);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::BlendFunction(const std::uint32_t source,
                               const std::uint32_t destination) {
#if OGPLAY_HAS_ANGLE
    glBlendFunc(source, destination);
    RequireNoError("glBlendFunc");
#else
    static_cast<void>(source); static_cast<void>(destination);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::ColorMask(const bool red, const bool green, const bool blue,
                           const bool alpha) {
#if OGPLAY_HAS_ANGLE
    glColorMask(red ? GL_TRUE : GL_FALSE, green ? GL_TRUE : GL_FALSE,
                blue ? GL_TRUE : GL_FALSE, alpha ? GL_TRUE : GL_FALSE);
    RequireNoError("glColorMask");
#else
    static_cast<void>(red);
    static_cast<void>(green);
    static_cast<void>(blue);
    static_cast<void>(alpha);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::CullFace(const std::uint32_t mode) {
#if OGPLAY_HAS_ANGLE
    glCullFace(mode);
    RequireNoError("glCullFace");
#else
    static_cast<void>(mode);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::DepthFunction(const std::uint32_t function) {
#if OGPLAY_HAS_ANGLE
    glDepthFunc(function);
    RequireNoError("glDepthFunc");
#else
    static_cast<void>(function);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::DepthMask(const bool enabled) {
#if OGPLAY_HAS_ANGLE
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    RequireNoError("glDepthMask");
#else
    static_cast<void>(enabled);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::Finish() {
#if OGPLAY_HAS_ANGLE
    glFinish();
    RequireNoError("glFinish");
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::FrontFace(const std::uint32_t mode) {
#if OGPLAY_HAS_ANGLE
    glFrontFace(mode);
    RequireNoError("glFrontFace");
#else
    static_cast<void>(mode);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::Hint(const std::uint32_t target, const std::uint32_t mode) {
#if OGPLAY_HAS_ANGLE
    glHint(target, mode);
    RequireNoError("glHint");
#else
    static_cast<void>(target);
    static_cast<void>(mode);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::vector<std::int32_t> AngleFrame::GetIntegers(
    const std::uint32_t parameter, const std::size_t count) {
    if (count > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) {
        throw std::length_error("ANGLE integer query count overflows GLsizei");
    }
#if OGPLAY_HAS_ANGLE
    std::vector<std::int32_t> values(count);
    glGetIntegerv(parameter, values.data());
    RequireNoError("glGetIntegerv");
    return values;
#else
    static_cast<void>(parameter);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::string AngleFrame::GetString(const std::uint32_t parameter) {
#if OGPLAY_HAS_ANGLE
    const auto* value = glGetString(parameter);
    RequireNoError("glGetString");
    if (value == nullptr) throw std::runtime_error("glGetString returned null");
    return reinterpret_cast<const char*>(value);
#else
    static_cast<void>(parameter);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::uint32_t AngleFrame::GetError() noexcept {
#if OGPLAY_HAS_ANGLE
    return glGetError();
#else
    return 0;
#endif
}

void AngleFrame::DrawElements(const std::uint32_t mode,
                              const std::int32_t count,
                              const std::uint32_t type,
                              const std::uint32_t offset) {
#if OGPLAY_HAS_ANGLE
    glDrawElements(mode, count, type, reinterpret_cast<const void*>(
                                         static_cast<std::uintptr_t>(offset)));
    RequireNoError("glDrawElements");
#else
    static_cast<void>(mode); static_cast<void>(count);
    static_cast<void>(type); static_cast<void>(offset);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::ReadPixels(
    const std::int32_t x, const std::int32_t y,
    const std::int32_t width, const std::int32_t height,
    const std::uint32_t format, const std::uint32_t type,
    const std::span<std::byte> output) {
#if OGPLAY_HAS_ANGLE
    glReadPixels(x, y, width, height, format, type, output.data());
    RequireNoError("glReadPixels");
#else
    static_cast<void>(x); static_cast<void>(y); static_cast<void>(width);
    static_cast<void>(height); static_cast<void>(format); static_cast<void>(type);
    static_cast<void>(output);
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

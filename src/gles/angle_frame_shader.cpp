#include "ogplay/gles/angle_frame.h"

#include <limits>
#include <stdexcept>
#include <vector>

#if OGPLAY_HAS_ANGLE
#include <GLES2/gl2.h>
#endif

namespace ogplay::gles {
namespace {

void ValidateVectorPayload(const std::int32_t count,
                           const std::uint32_t components,
                           const std::size_t size) {
    if (count < 0 || components == 0U || components > 4U) {
        throw std::invalid_argument("ANGLE uniform vector shape is invalid");
    }
    const auto elements = static_cast<std::uint64_t>(count) * components;
    if (elements != size) {
        throw std::invalid_argument(
            "ANGLE uniform vector count does not match its payload");
    }
}

#if OGPLAY_HAS_ANGLE
void RequireShaderNoError(const char* const operation) {
    const auto error = glGetError();
    if (error != GL_NO_ERROR) {
        throw std::runtime_error(std::string(operation) +
                                 " failed with GLES error " +
                                 std::to_string(error));
    }
}

AngleActiveVariable GetActiveVariable(const std::uint32_t program,
                                      const std::uint32_t index,
                                      const std::uint32_t max_length_parameter,
                                      const bool attribute) {
    GLint max_length{};
    glGetProgramiv(program, max_length_parameter, &max_length);
    RequireShaderNoError("glGetProgramiv(active max length)");
    if (max_length < 0) {
        throw std::runtime_error("ANGLE returned a negative active name length");
    }
    std::vector<GLchar> name(static_cast<std::size_t>(max_length));
    GLsizei length{};
    GLint size{};
    GLenum type{};
    if (attribute) {
        glGetActiveAttrib(program, index, max_length, &length, &size, &type,
                          name.data());
    } else {
        glGetActiveUniform(program, index, max_length, &length, &size, &type,
                           name.data());
    }
    RequireShaderNoError(attribute ? "glGetActiveAttrib" : "glGetActiveUniform");
    if (length < 0 || length > max_length) {
        throw std::runtime_error("ANGLE returned an invalid active name length");
    }
    return {.name = std::string(name.data(), static_cast<std::size_t>(length)),
            .size = size,
            .type = type};
}

std::string ReadInfoLog(const std::uint32_t object, const bool program) {
    GLint max_length{};
    if (program) glGetProgramiv(object, GL_INFO_LOG_LENGTH, &max_length);
    else glGetShaderiv(object, GL_INFO_LOG_LENGTH, &max_length);
    RequireShaderNoError(program ? "glGetProgramiv(info log length)"
                                 : "glGetShaderiv(info log length)");
    if (max_length < 0) {
        throw std::runtime_error("ANGLE returned a negative info log length");
    }
    if (max_length == 0) return {};
    std::vector<GLchar> log(static_cast<std::size_t>(max_length));
    GLsizei length{};
    if (program) glGetProgramInfoLog(object, max_length, &length, log.data());
    else glGetShaderInfoLog(object, max_length, &length, log.data());
    RequireShaderNoError(program ? "glGetProgramInfoLog" : "glGetShaderInfoLog");
    if (length < 0 || length > max_length) {
        throw std::runtime_error("ANGLE returned an invalid info log length");
    }
    return std::string(log.data(), static_cast<std::size_t>(length));
}
#endif

}  // namespace

AngleActiveVariable AngleFrame::GetActiveAttribute(
    const std::uint32_t program, const std::uint32_t index) {
#if OGPLAY_HAS_ANGLE
    return GetActiveVariable(program, index, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH,
                             true);
#else
    static_cast<void>(program); static_cast<void>(index);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

AngleActiveVariable AngleFrame::GetActiveUniform(
    const std::uint32_t program, const std::uint32_t index) {
#if OGPLAY_HAS_ANGLE
    return GetActiveVariable(program, index, GL_ACTIVE_UNIFORM_MAX_LENGTH,
                             false);
#else
    static_cast<void>(program); static_cast<void>(index);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::string AngleFrame::GetProgramInfoLog(const std::uint32_t program) {
#if OGPLAY_HAS_ANGLE
    return ReadInfoLog(program, true);
#else
    static_cast<void>(program);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::string AngleFrame::GetShaderInfoLog(const std::uint32_t shader) {
#if OGPLAY_HAS_ANGLE
    return ReadInfoLog(shader, false);
#else
    static_cast<void>(shader);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::UniformFloats(const std::int32_t location,
                               const std::int32_t count,
                               const std::uint32_t components,
                               const std::span<const float> values) {
    ValidateVectorPayload(count, components, values.size());
#if OGPLAY_HAS_ANGLE
    switch (components) {
    case 1: glUniform1fv(location, count, values.data()); break;
    case 2: glUniform2fv(location, count, values.data()); break;
    case 3: glUniform3fv(location, count, values.data()); break;
    case 4: glUniform4fv(location, count, values.data()); break;
    default: throw std::logic_error("validated uniform component count changed");
    }
    RequireNoError("glUniform*fv");
#else
    static_cast<void>(location);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::UniformIntegers(
    const std::int32_t location, const std::int32_t count,
    const std::uint32_t components,
    const std::span<const std::int32_t> values) {
    ValidateVectorPayload(count, components, values.size());
#if OGPLAY_HAS_ANGLE
    switch (components) {
    case 1: glUniform1iv(location, count, values.data()); break;
    case 2: glUniform2iv(location, count, values.data()); break;
    case 3: glUniform3iv(location, count, values.data()); break;
    case 4: glUniform4iv(location, count, values.data()); break;
    default: throw std::logic_error("validated uniform component count changed");
    }
    RequireNoError("glUniform*iv");
#else
    static_cast<void>(location);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::UniformMatrix4(const std::int32_t location,
                                const std::int32_t count,
                                const bool transpose,
                                const std::span<const float> values) {
    if (count < 0 || static_cast<std::uint64_t>(count) * 16U != values.size()) {
        throw std::invalid_argument(
            "ANGLE matrix count does not match its payload");
    }
#if OGPLAY_HAS_ANGLE
    glUniformMatrix4fv(location, count, transpose ? GL_TRUE : GL_FALSE,
                       values.data());
    RequireNoError("glUniformMatrix4fv");
#else
    static_cast<void>(location); static_cast<void>(transpose);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::VertexAttribute4f(const std::uint32_t index, const float x,
                                   const float y, const float z, const float w) {
#if OGPLAY_HAS_ANGLE
    glVertexAttrib4f(index, x, y, z, w);
    RequireNoError("glVertexAttrib4f");
#else
    static_cast<void>(index); static_cast<void>(x); static_cast<void>(y);
    static_cast<void>(z); static_cast<void>(w);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

}  // namespace ogplay::gles

#include "ogplay/gles/angle_frame.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if OGPLAY_HAS_ANGLE
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
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

std::uint32_t UniformValueCount(const std::uint32_t type) {
    switch (type) {
    case GL_FLOAT:
    case GL_INT:
    case GL_BOOL:
    case GL_SAMPLER_2D:
    case GL_SAMPLER_CUBE:
    case GL_SAMPLER_3D_OES: return 1U;
    case GL_FLOAT_VEC2:
    case GL_INT_VEC2:
    case GL_BOOL_VEC2: return 2U;
    case GL_FLOAT_VEC3:
    case GL_INT_VEC3:
    case GL_BOOL_VEC3: return 3U;
    case GL_FLOAT_VEC4:
    case GL_INT_VEC4:
    case GL_BOOL_VEC4:
    case GL_FLOAT_MAT2: return 4U;
    case GL_FLOAT_MAT3: return 9U;
    case GL_FLOAT_MAT4: return 16U;
    default:
        throw std::runtime_error(
            "ANGLE reported an unsupported GLES2 uniform type");
    }
}

std::string UniformElementName(const std::string& active_name,
                               const std::int32_t element) {
    if (element == 0) return active_name;
    auto name = active_name;
    const auto marker = name.find("[0]");
    if (marker == std::string::npos) {
        name += "[" + std::to_string(element) + "]";
    } else {
        name.replace(marker, 3U, "[" + std::to_string(element) + "]");
    }
    return name;
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

std::vector<std::uint32_t> AngleFrame::GetAttachedShaders(
    const std::uint32_t program, const std::size_t maximum_count) {
    if (maximum_count > static_cast<std::size_t>(
                            (std::numeric_limits<std::int32_t>::max)())) {
        throw std::length_error("ANGLE attached shader count overflows GLsizei");
    }
#if OGPLAY_HAS_ANGLE
    std::vector<GLuint> shaders(maximum_count);
    GLsizei count{};
    glGetAttachedShaders(program, static_cast<GLsizei>(maximum_count), &count,
                         shaders.data());
    RequireNoError("glGetAttachedShaders");
    if (count < 0 || static_cast<std::size_t>(count) > maximum_count) {
        throw std::runtime_error("ANGLE returned an invalid attached shader count");
    }
    shaders.resize(static_cast<std::size_t>(count));
    return shaders;
#else
    static_cast<void>(program);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

AngleShaderPrecision AngleFrame::GetShaderPrecisionFormat(
    const std::uint32_t shader_type, const std::uint32_t precision_type) {
#if OGPLAY_HAS_ANGLE
    AngleShaderPrecision result;
    glGetShaderPrecisionFormat(shader_type, precision_type,
                               result.range.data(), &result.precision);
    RequireNoError("glGetShaderPrecisionFormat");
    return result;
#else
    static_cast<void>(shader_type); static_cast<void>(precision_type);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::string AngleFrame::GetShaderSource(const std::uint32_t shader,
                                        const std::size_t maximum_bytes) {
    if (maximum_bytes > static_cast<std::size_t>(
                            (std::numeric_limits<std::int32_t>::max)())) {
        throw std::length_error("ANGLE shader source buffer overflows GLsizei");
    }
#if OGPLAY_HAS_ANGLE
    std::vector<GLchar> source(maximum_bytes);
    GLsizei length{};
    glGetShaderSource(shader, static_cast<GLsizei>(maximum_bytes), &length,
                      source.data());
    RequireNoError("glGetShaderSource");
    if (length < 0 || static_cast<std::size_t>(length) > maximum_bytes) {
        throw std::runtime_error("ANGLE returned an invalid shader source length");
    }
    if (length == 0) return {};
    return {source.data(), static_cast<std::size_t>(length)};
#else
    static_cast<void>(shader);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::vector<AngleUniformValueCount> AngleFrame::DiscoverUniformValueCounts(
    const std::uint32_t program) {
#if OGPLAY_HAS_ANGLE
    GLint active_count{};
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &active_count);
    RequireNoError("glGetProgramiv(GL_ACTIVE_UNIFORMS)");
    if (active_count < 0) {
        throw std::runtime_error("ANGLE returned a negative active uniform count");
    }
    std::vector<AngleUniformValueCount> result;
    for (GLint index = 0; index < active_count; ++index) {
        const auto active = GetActiveVariable(
            program, static_cast<std::uint32_t>(index),
            GL_ACTIVE_UNIFORM_MAX_LENGTH, false);
        if (active.size < 1) {
            throw std::runtime_error("ANGLE returned an invalid uniform array size");
        }
        const auto value_count = UniformValueCount(active.type);
        for (GLint element = 0; element < active.size; ++element) {
            const auto name = UniformElementName(active.name, element);
            const auto location = glGetUniformLocation(program, name.c_str());
            RequireNoError("glGetUniformLocation(active uniform)");
            if (location < 0) continue;
            const auto duplicate = std::ranges::find_if(
                result, [location](const auto& entry) {
                    return entry.location == location;
                });
            if (duplicate == result.end()) {
                result.push_back({location, value_count});
            }
        }
    }
    return result;
#else
    static_cast<void>(program);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::ReleaseShaderCompiler() {
#if OGPLAY_HAS_ANGLE
    glReleaseShaderCompiler();
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::ShaderBinary(
    const std::span<const std::uint32_t> shaders, const std::uint32_t format,
    const std::span<const std::byte> binary) {
    if (shaders.size() > static_cast<std::size_t>(
                             (std::numeric_limits<std::int32_t>::max)()) ||
        binary.size() > static_cast<std::size_t>(
                            (std::numeric_limits<std::int32_t>::max)())) {
        throw std::length_error("ANGLE shader binary input overflows GLsizei");
    }
#if OGPLAY_HAS_ANGLE
    glShaderBinary(static_cast<GLsizei>(shaders.size()), shaders.data(),
                   format, binary.data(), static_cast<GLsizei>(binary.size()));
#else
    static_cast<void>(format);
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

void AngleFrame::UniformMatrix2(const std::int32_t location,
                                const std::int32_t count,
                                const bool transpose,
                                const std::span<const float> values) {
    if (count < 0 || static_cast<std::uint64_t>(count) * 4U != values.size()) {
        throw std::invalid_argument(
            "ANGLE matrix count does not match its payload");
    }
#if OGPLAY_HAS_ANGLE
    glUniformMatrix2fv(location, count, transpose ? GL_TRUE : GL_FALSE,
                       values.data());
    RequireNoError("glUniformMatrix2fv");
#else
    static_cast<void>(location); static_cast<void>(transpose);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::vector<float> AngleFrame::GetUniformFloats(
    const std::uint32_t program, const std::int32_t location,
    const std::size_t count) {
#if OGPLAY_HAS_ANGLE
    std::vector<float> values(count);
    glGetUniformfv(program, location, values.data());
    RequireNoError("glGetUniformfv");
    return values;
#else
    static_cast<void>(program); static_cast<void>(location);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::vector<std::int32_t> AngleFrame::GetUniformIntegers(
    const std::uint32_t program, const std::int32_t location,
    const std::size_t count) {
#if OGPLAY_HAS_ANGLE
    std::vector<std::int32_t> values(count);
    glGetUniformiv(program, location, values.data());
    RequireNoError("glGetUniformiv");
    return values;
#else
    static_cast<void>(program); static_cast<void>(location);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::vector<float> AngleFrame::GetVertexAttributeFloats(
    const std::uint32_t index, const std::uint32_t parameter,
    const std::size_t count) {
#if OGPLAY_HAS_ANGLE
    std::vector<float> values(count);
    glGetVertexAttribfv(index, parameter, values.data());
    RequireNoError("glGetVertexAttribfv");
    return values;
#else
    static_cast<void>(index); static_cast<void>(parameter);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::vector<std::int32_t> AngleFrame::GetVertexAttributeIntegers(
    const std::uint32_t index, const std::uint32_t parameter,
    const std::size_t count) {
#if OGPLAY_HAS_ANGLE
    std::vector<std::int32_t> values(count);
    glGetVertexAttribiv(index, parameter, values.data());
    RequireNoError("glGetVertexAttribiv");
    return values;
#else
    static_cast<void>(index); static_cast<void>(parameter);
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

#include "ogplay/gles/angle_frame.h"

#include <algorithm>
#include <bit>
#include <stdexcept>

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

namespace ogplay::gles {

std::byte* AngleFrame::MappedBufferPointer(const std::uint32_t target) {
    void* pointer{};
    glGetBufferPointerv(target, GL_BUFFER_MAP_POINTER, &pointer);
    RequireNoError("glGetBufferPointerv");
    return static_cast<std::byte*>(pointer);
}

void AngleFrame::BindEglImage(const std::uint32_t target, const std::uintptr_t image,
                             const bool renderbuffer) {
    if (renderbuffer) {
        const auto call = reinterpret_cast<PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC>(
            eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES"));
        if (!call) throw GlesApiError("EGL image target", GL_INVALID_OPERATION);
        call(target, reinterpret_cast<GLeglImageOES>(image));
    } else {
        const auto call = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            eglGetProcAddress("glEGLImageTargetTexture2DOES"));
        if (!call) throw GlesApiError("EGL image target", GL_INVALID_OPERATION);
        call(target, reinterpret_cast<GLeglImageOES>(image));
    }
    RequireNoError("glEGLImageTarget");
}

void AngleFrame::TransferPixelBuffer(const PixelBufferOperation operation,
                                     const std::span<const std::uint32_t> a) {
    const auto pointer = [](const std::uint32_t offset) {
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(offset));
    };
    const auto i = [](const std::uint32_t value) { return std::bit_cast<GLint>(value); };
    if (operation == PixelBufferOperation::compressed2d || operation == PixelBufferOperation::compressed_sub2d) {
        const bool sub = operation == PixelBufferOperation::compressed_sub2d;
        const auto format = a[sub ? 6 : 2];
        if (format == 0x8D64U || (format >= 0x8C00U && format <= 0x8C03U)) {
            const auto length = i(a[sub ? 7 : 6]);
            if (length <= 0) throw GlesApiError("compressed PBO size", GL_INVALID_VALUE);
            const auto buffer = BoundBuffer(GL_PIXEL_UNPACK_BUFFER);
            std::vector<std::byte> bytes(static_cast<std::size_t>(length));
            const auto* mapped = MapBufferRange(GL_PIXEL_UNPACK_BUFFER, a[sub ? 8 : 7], length, GL_MAP_READ_BIT);
            if (mapped == nullptr) throw GlesApiError("compressed PBO mapping", GL_INVALID_OPERATION);
            std::copy_n(mapped, bytes.size(), bytes.begin());
            if (!UnmapBuffer(GL_PIXEL_UNPACK_BUFFER)) throw GlesApiError("compressed PBO contents", GL_INVALID_OPERATION);
            BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0U);
            try {
                if (sub) CompressedTextureSubImage2D(a[0], i(a[1]), i(a[2]), i(a[3]), i(a[4]), i(a[5]), format, bytes);
                else CompressedTextureImage2D(a[0], i(a[1]), format, i(a[3]), i(a[4]), i(a[5]), bytes);
            } catch (...) { BindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer); throw; }
            BindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
            return;
        }
    }
    switch (operation) {
    case PixelBufferOperation::image2d:
        glTexImage2D(a[0], i(a[1]), i(a[2]), i(a[3]), i(a[4]), i(a[5]), a[6], a[7], pointer(a[8])); break;
    case PixelBufferOperation::sub2d:
        glTexSubImage2D(a[0], i(a[1]), i(a[2]), i(a[3]), i(a[4]), i(a[5]), a[6], a[7], pointer(a[8])); break;
    case PixelBufferOperation::image3d:
        glTexImage3D(a[0], i(a[1]), i(a[2]), i(a[3]), i(a[4]), i(a[5]), i(a[6]), a[7], a[8], pointer(a[9])); break;
    case PixelBufferOperation::sub3d:
        glTexSubImage3D(a[0], i(a[1]), i(a[2]), i(a[3]), i(a[4]), i(a[5]), i(a[6]), i(a[7]), a[8], a[9], pointer(a[10])); break;
    case PixelBufferOperation::read:
        glReadPixels(i(a[0]), i(a[1]), i(a[2]), i(a[3]), a[4], a[5], pointer(a[6])); break;
    case PixelBufferOperation::compressed2d:
        glCompressedTexImage2D(a[0], i(a[1]), a[2], i(a[3]), i(a[4]), i(a[5]), i(a[6]), pointer(a[7])); break;
    case PixelBufferOperation::compressed_sub2d:
        glCompressedTexSubImage2D(a[0], i(a[1]), i(a[2]), i(a[3]), i(a[4]), i(a[5]), a[6], i(a[7]), pointer(a[8])); break;
    case PixelBufferOperation::compressed3d:
        glCompressedTexImage3D(a[0], i(a[1]), a[2], i(a[3]), i(a[4]), i(a[5]), i(a[6]), i(a[7]), pointer(a[8])); break;
    case PixelBufferOperation::compressed_sub3d:
        glCompressedTexSubImage3D(a[0], i(a[1]), i(a[2]), i(a[3]), i(a[4]), i(a[5]), i(a[6]), i(a[7]), a[8], i(a[9]), pointer(a[10])); break;
    }
    RequireNoError("GLES pixel buffer transfer");
}

std::uint32_t AngleFrame::InvokeGles3Scalar(
    const std::uint16_t id, const std::span<const std::uint32_t> a) {
    const auto i = [](const std::uint32_t v) { return std::bit_cast<GLint>(v); };
    const auto f = [](const std::uint32_t v) { return std::bit_cast<GLfloat>(v); };
    std::uint32_t result{};
    switch (id) {
    case 0: glBeginQuery(a[0], a[1]); break;
    case 1: glBeginTransformFeedback(a[0]); break;
    case 2: glBindBufferBase(a[0], a[1], a[2]); break;
    case 3: glBindBufferRange(a[0], a[1], a[2], static_cast<GLintptr>(i(a[3])), static_cast<GLsizeiptr>(i(a[4]))); break;
    case 4: glBindSampler(a[0], a[1]); break;
    case 5: glBindTransformFeedback(a[0], a[1]); break;
    case 6: glBindVertexArray(a[0]); break;
    case 7: glBlitFramebuffer(i(a[0]), i(a[1]), i(a[2]), i(a[3]), i(a[4]), i(a[5]), i(a[6]), i(a[7]), a[8], a[9]); break;
    case 8: glClearBufferfi(a[0], i(a[1]), f(a[2]), i(a[3])); break;
    case 15: glCopyBufferSubData(a[0], a[1], static_cast<GLintptr>(i(a[2])), static_cast<GLintptr>(i(a[3])), static_cast<GLsizeiptr>(i(a[4]))); break;
    case 16: glCopyTexSubImage3D(a[0], i(a[1]), i(a[2]), i(a[3]), i(a[4]), i(a[5]), i(a[6]), i(a[7]), i(a[8])); break;
    case 22: glDrawArraysInstanced(a[0], i(a[1]), i(a[2]), i(a[3])); break;
    case 26: glEndQuery(a[0]); break;
    case 27: glEndTransformFeedback(); break;
    case 29: glFlushMappedBufferRange(a[0], static_cast<GLintptr>(i(a[1])), static_cast<GLsizeiptr>(i(a[2]))); break;
    case 30: glFramebufferTextureLayer(a[0], a[1], a[2], i(a[3]), i(a[4])); break;
    case 60: result = glIsQuery(a[0]); break;
    case 61: result = glIsSampler(a[0]); break;
    case 63: result = glIsTransformFeedback(a[0]); break;
    case 64: result = glIsVertexArray(a[0]); break;
    case 66: glPauseTransformFeedback(); break;
    case 68: glProgramParameteri(a[0], a[1], i(a[2])); break;
    case 69: glReadBuffer(a[0]); break;
    case 70: glRenderbufferStorageMultisample(a[0], i(a[1]), a[2], i(a[3]), i(a[4])); break;
    case 71: glResumeTransformFeedback(); break;
    case 72: glSamplerParameterf(a[0], a[1], f(a[2])); break;
    case 74: glSamplerParameteri(a[0], a[1], i(a[2])); break;
    case 77: glTexStorage2D(a[0], i(a[1]), a[2], i(a[3]), i(a[4])); break;
    case 78: glTexStorage3D(a[0], i(a[1]), a[2], i(a[3]), i(a[4]), i(a[5])); break;
    case 81: glUniform1ui(i(a[0]), a[1]); break;
    case 83: glUniform2ui(i(a[0]), a[1], a[2]); break;
    case 85: glUniform3ui(i(a[0]), a[1], a[2], a[3]); break;
    case 87: glUniform4ui(i(a[0]), a[1], a[2], a[3], a[4]); break;
    case 89: glUniformBlockBinding(a[0], a[1], a[2]); break;
    case 96: result = glUnmapBuffer(a[0]); break;
    case 97: glVertexAttribDivisor(a[0], a[1]); break;
    case 98: glVertexAttribI4i(a[0], i(a[1]), i(a[2]), i(a[3]), i(a[4])); break;
    case 100: glVertexAttribI4ui(a[0], a[1], a[2], a[3], a[4]); break;
    default: throw std::logic_error("GLES3 scalar dispatch id is unsupported");
    }
    RequireNoError("GLES3 scalar call");
    return result;
}

void AngleFrame::InvokeGles3Names(const std::uint16_t id,
                                  const std::span<std::uint32_t> names) {
    const auto count = static_cast<GLsizei>(names.size());
    auto* data = reinterpret_cast<GLuint*>(names.data());
    switch (id) {
    case 17: glDeleteQueries(count, data); break;
    case 18: glDeleteSamplers(count, data); break;
    case 20: glDeleteTransformFeedbacks(count, data); break;
    case 21: glDeleteVertexArrays(count, data); break;
    case 31: glGenQueries(count, data); break;
    case 32: glGenSamplers(count, data); break;
    case 33: glGenTransformFeedbacks(count, data); break;
    case 34: glGenVertexArrays(count, data); break;
    default: throw std::logic_error("GLES3 name dispatch id is unsupported");
    }
    RequireNoError("GLES3 name call");
}

void AngleFrame::InvokeGles3Words(const std::uint16_t id,
                                  const std::span<const std::uint32_t> a,
                                  const std::span<std::uint32_t> w) {
    auto* u = reinterpret_cast<GLuint*>(w.data());
    auto* i = reinterpret_cast<GLint*>(w.data());
    auto* f = reinterpret_cast<GLfloat*>(w.data());
    switch (id) {
    case 9: glClearBufferfv(a[0], static_cast<GLint>(a[1]), f); break;
    case 10: glClearBufferiv(a[0], static_cast<GLint>(a[1]), i); break;
    case 11: glClearBufferuiv(a[0], static_cast<GLint>(a[1]), u); break;
    case 23: glDrawBuffers(static_cast<GLsizei>(a[0]), u); break;
    case 36: glGetActiveUniformBlockiv(a[0], a[1], a[2], i); break;
    case 43: glGetIntegeri_v(a[0], a[1], i); break;
    case 44: glGetInternalformativ(a[0], a[1], a[2], static_cast<GLsizei>(a[3]), i); break;
    case 46: glGetQueryObjectuiv(a[0], a[1], u); break;
    case 47: glGetQueryiv(a[0], a[1], i); break;
    case 48: glGetSamplerParameterfv(a[0], a[1], f); break;
    case 49: glGetSamplerParameteriv(a[0], a[1], i); break;
    case 56: glGetVertexAttribIiv(a[0], a[1], i); break;
    case 57: glGetVertexAttribIuiv(a[0], a[1], u); break;
    case 58: glInvalidateFramebuffer(a[0], static_cast<GLsizei>(a[1]), u); break;
    case 59: glInvalidateSubFramebuffer(a[0], static_cast<GLsizei>(a[1]), u,
                                        static_cast<GLint>(a[3]), static_cast<GLint>(a[4]),
                                        static_cast<GLsizei>(a[5]), static_cast<GLsizei>(a[6])); break;
    case 73: glSamplerParameterfv(a[0], a[1], f); break;
    case 75: glSamplerParameteriv(a[0], a[1], i); break;
    case 82: glUniform1uiv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), u); break;
    case 84: glUniform2uiv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), u); break;
    case 86: glUniform3uiv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), u); break;
    case 88: glUniform4uiv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), u); break;
    case 90: glUniformMatrix2x3fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), a[2] != 0, f); break;
    case 91: glUniformMatrix2x4fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), a[2] != 0, f); break;
    case 92: glUniformMatrix3x2fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), a[2] != 0, f); break;
    case 93: glUniformMatrix3x4fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), a[2] != 0, f); break;
    case 94: glUniformMatrix4x2fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), a[2] != 0, f); break;
    case 95: glUniformMatrix4x3fv(static_cast<GLint>(a[0]), static_cast<GLsizei>(a[1]), a[2] != 0, f); break;
    case 99: glVertexAttribI4iv(a[0], i); break;
    case 101: glVertexAttribI4uiv(a[0], u); break;
    default: throw std::logic_error("GLES3 word dispatch id is unsupported");
    }
    RequireNoError("GLES3 pointer call");
}

std::string AngleFrame::GetStringIndexed(const std::uint32_t name,
                                         const std::uint32_t index) {
    const auto* value = glGetStringi(name, index);
    RequireNoError("glGetStringi");
    return value == nullptr ? std::string{} :
           std::string(reinterpret_cast<const char*>(value));
}

std::int32_t AngleFrame::GetFragDataLocation(const std::uint32_t program,
                                             const std::string_view name) {
    const auto value = glGetFragDataLocation(program, std::string(name).c_str());
    RequireNoError("glGetFragDataLocation");
    return value;
}

std::uint32_t AngleFrame::GetUniformBlockIndex(const std::uint32_t program,
                                               const std::string_view name) {
    const auto value = glGetUniformBlockIndex(program, std::string(name).c_str());
    RequireNoError("glGetUniformBlockIndex");
    return value;
}

std::vector<std::int64_t> AngleFrame::GetGles3Integer64(
    const std::uint16_t id, const std::span<const std::uint32_t> a,
    const std::size_t count) {
    std::vector<GLint64> values(count);
    switch (id) {
    case 38: glGetBufferParameteri64v(a[0], a[1], values.data()); break;
    case 41: glGetInteger64i_v(a[0], a[1], values.data()); break;
    case 42: glGetInteger64v(a[0], values.data()); break;
    default: throw std::logic_error("GLES3 integer64 dispatch id is unsupported");
    }
    RequireNoError("GLES3 integer64 query");
    return {values.begin(), values.end()};
}

void AngleFrame::InvokeGles3Bytes(const std::uint16_t id,
                                  const std::span<const std::uint32_t> a,
                                  const std::span<const std::byte> bytes) {
    switch (id) {
    case 13:
        glCompressedTexImage3D(a[0], static_cast<GLint>(a[1]), a[2],
            static_cast<GLsizei>(a[3]), static_cast<GLsizei>(a[4]),
            static_cast<GLsizei>(a[5]), static_cast<GLint>(a[6]),
            static_cast<GLsizei>(a[7]), bytes.data());
        break;
    case 14:
        glCompressedTexSubImage3D(a[0], static_cast<GLint>(a[1]),
            static_cast<GLint>(a[2]), static_cast<GLint>(a[3]),
            static_cast<GLint>(a[4]), static_cast<GLsizei>(a[5]),
            static_cast<GLsizei>(a[6]), static_cast<GLsizei>(a[7]), a[8],
            static_cast<GLsizei>(a[9]), bytes.data());
        break;
    case 67:
        glProgramBinary(a[0], a[1], bytes.data(), static_cast<GLsizei>(a[3]));
        break;
    default: throw std::logic_error("GLES3 byte dispatch id is unsupported");
    }
    RequireNoError("GLES3 byte call");
}

std::uintptr_t AngleFrame::FenceSync(const std::uint32_t condition,
                                     const std::uint32_t flags) {
    const auto sync = glFenceSync(condition, flags);
    RequireNoError("glFenceSync");
    return reinterpret_cast<std::uintptr_t>(sync);
}

void AngleFrame::DeleteSync(const std::uintptr_t sync) {
    glDeleteSync(reinterpret_cast<GLsync>(sync));
    RequireNoError("glDeleteSync");
}

bool AngleFrame::IsSync(const std::uintptr_t sync) {
    const auto result = glIsSync(reinterpret_cast<GLsync>(sync)) == GL_TRUE;
    RequireNoError("glIsSync");
    return result;
}

std::uint32_t AngleFrame::ClientWaitSync(const std::uintptr_t sync,
                                         const std::uint32_t flags,
                                         const std::uint64_t timeout) {
    const auto result = glClientWaitSync(reinterpret_cast<GLsync>(sync), flags,
                                         timeout);
    RequireNoError("glClientWaitSync");
    return result;
}

void AngleFrame::WaitSync(const std::uintptr_t sync, const std::uint32_t flags,
                          const std::uint64_t timeout) {
    glWaitSync(reinterpret_cast<GLsync>(sync), flags, timeout);
    RequireNoError("glWaitSync");
}

std::vector<std::int32_t> AngleFrame::GetSyncValues(
    const std::uintptr_t sync, const std::uint32_t pname,
    const std::int32_t buffer_size, std::int32_t& length) {
    std::vector<std::int32_t> values(static_cast<std::size_t>(buffer_size));
    GLsizei native_length{};
    glGetSynciv(reinterpret_cast<GLsync>(sync), pname, buffer_size,
                &native_length, values.data());
    RequireNoError("glGetSynciv");
    length = native_length;
    values.resize(static_cast<std::size_t>(native_length));
    return values;
}

void AngleFrame::InvokeGles3Offset(const std::uint16_t id,
                                   const std::span<const std::uint32_t> a) {
    switch (id) {
    case 24:
        glDrawElementsInstanced(a[0], static_cast<GLsizei>(a[1]), a[2],
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(a[3])),
            static_cast<GLsizei>(a[4]));
        break;
    case 25:
        glDrawRangeElements(a[0], a[1], a[2], static_cast<GLsizei>(a[3]), a[4],
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(a[5])));
        break;
    case 102:
        glVertexAttribIPointer(a[0], static_cast<GLint>(a[1]), a[2],
            static_cast<GLsizei>(a[3]),
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(a[4])));
        break;
    default: throw std::logic_error("GLES3 offset dispatch id is unsupported");
    }
    RequireNoError("GLES3 buffer offset call");
}

std::string AngleFrame::GetActiveUniformBlockName(
    const std::uint32_t program, const std::uint32_t index,
    const std::int32_t buffer_size) {
    std::vector<GLchar> name(static_cast<std::size_t>(buffer_size));
    GLsizei length{};
    glGetActiveUniformBlockName(program, index, buffer_size, &length, name.data());
    RequireNoError("glGetActiveUniformBlockName");
    return std::string(name.data(), static_cast<std::size_t>(length));
}

std::vector<std::int32_t> AngleFrame::GetActiveUniformValues(
    const std::uint32_t program, const std::span<const std::uint32_t> indices,
    const std::uint32_t pname) {
    std::vector<GLint> values(indices.size());
    glGetActiveUniformsiv(program, static_cast<GLsizei>(indices.size()),
                          indices.data(), pname, values.data());
    RequireNoError("glGetActiveUniformsiv");
    return {values.begin(), values.end()};
}

std::vector<std::byte> AngleFrame::GetProgramBinary(
    const std::uint32_t program, const std::int32_t buffer_size,
    std::int32_t& length, std::uint32_t& format) {
    std::vector<std::byte> binary(static_cast<std::size_t>(buffer_size));
    GLsizei native_length{}; GLenum native_format{};
    glGetProgramBinary(program, buffer_size, &native_length, &native_format,
                       binary.data());
    RequireNoError("glGetProgramBinary");
    length = native_length; format = native_format;
    binary.resize(static_cast<std::size_t>(native_length));
    return binary;
}

AngleActiveVariable AngleFrame::GetTransformFeedbackVarying(
    const std::uint32_t program, const std::uint32_t index) {
    GLint maximum{};
    glGetProgramiv(program, GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH, &maximum);
    RequireNoError("glGetProgramiv");
    std::vector<GLchar> name(static_cast<std::size_t>((std::max)(maximum, 1)));
    GLsizei length{}, size{}; GLenum type{};
    glGetTransformFeedbackVarying(program, index, maximum, &length, &size, &type,
                                  name.data());
    RequireNoError("glGetTransformFeedbackVarying");
    return {{name.data(), static_cast<std::size_t>(length)}, size, type};
}

std::vector<std::uint32_t> AngleFrame::GetUniformIndices(
    const std::uint32_t program, const std::span<const std::string> names) {
    std::vector<const GLchar*> pointers;
    pointers.reserve(names.size());
    for (const auto& name : names) pointers.push_back(name.c_str());
    std::vector<GLuint> indices(names.size());
    glGetUniformIndices(program, static_cast<GLsizei>(names.size()),
                        pointers.data(), indices.data());
    RequireNoError("glGetUniformIndices");
    return {indices.begin(), indices.end()};
}

std::vector<std::uint32_t> AngleFrame::GetUniformUnsigned(
    const std::uint32_t program, const std::int32_t location,
    const std::size_t count) {
    std::vector<GLuint> values(count);
    glGetUniformuiv(program, location, values.data());
    RequireNoError("glGetUniformuiv");
    return {values.begin(), values.end()};
}

void AngleFrame::TransformFeedbackVaryings(
    const std::uint32_t program, const std::span<const std::string> names,
    const std::uint32_t buffer_mode) {
    std::vector<const GLchar*> pointers;
    pointers.reserve(names.size());
    for (const auto& name : names) pointers.push_back(name.c_str());
    glTransformFeedbackVaryings(program, static_cast<GLsizei>(names.size()),
                                pointers.data(), buffer_mode);
    RequireNoError("glTransformFeedbackVaryings");
}

void AngleFrame::TextureImage3D(
    const std::uint16_t id, const std::span<const std::uint32_t> a,
    const std::optional<std::span<const std::byte>> pixels) {
    const auto* data = pixels.has_value() ? pixels->data() : nullptr;
    if (id == 76U) {
        glTexImage3D(a[0], static_cast<GLint>(a[1]), static_cast<GLint>(a[2]),
                     static_cast<GLsizei>(a[3]), static_cast<GLsizei>(a[4]),
                     static_cast<GLsizei>(a[5]), static_cast<GLint>(a[6]),
                     a[7], a[8], data);
    } else {
        glTexSubImage3D(a[0], static_cast<GLint>(a[1]), static_cast<GLint>(a[2]),
                        static_cast<GLint>(a[3]), static_cast<GLint>(a[4]),
                        static_cast<GLsizei>(a[5]), static_cast<GLsizei>(a[6]),
                        static_cast<GLsizei>(a[7]), a[8], a[9], data);
    }
    RequireNoError(id == 76U ? "glTexImage3D" : "glTexSubImage3D");
}

std::byte* AngleFrame::MapBufferRange(const std::uint32_t target,
    const std::int32_t offset, const std::int32_t length,
    const std::uint32_t access) {
    auto* result = glMapBufferRange(target, offset, length, access);
    RequireNoError("glMapBufferRange");
    return static_cast<std::byte*>(result);
}

bool AngleFrame::UnmapBuffer(const std::uint32_t target) {
    const auto result = glUnmapBuffer(target) == GL_TRUE;
    RequireNoError("glUnmapBuffer");
    return result;
}

std::uint32_t AngleFrame::BoundBuffer(const std::uint32_t target) {
    GLenum pname{};
    switch (target) {
    case GL_ARRAY_BUFFER: pname = GL_ARRAY_BUFFER_BINDING; break;
    case GL_ELEMENT_ARRAY_BUFFER: pname = GL_ELEMENT_ARRAY_BUFFER_BINDING; break;
    case GL_COPY_READ_BUFFER: pname = GL_COPY_READ_BUFFER_BINDING; break;
    case GL_COPY_WRITE_BUFFER: pname = GL_COPY_WRITE_BUFFER_BINDING; break;
    case GL_PIXEL_PACK_BUFFER: pname = GL_PIXEL_PACK_BUFFER_BINDING; break;
    case GL_PIXEL_UNPACK_BUFFER: pname = GL_PIXEL_UNPACK_BUFFER_BINDING; break;
    case GL_TRANSFORM_FEEDBACK_BUFFER: pname = GL_TRANSFORM_FEEDBACK_BUFFER_BINDING; break;
    case GL_UNIFORM_BUFFER: pname = GL_UNIFORM_BUFFER_BINDING; break;
    default: throw GlesApiError("GLES3 buffer target", GL_INVALID_ENUM);
    }
    GLint buffer{}; glGetIntegerv(pname, &buffer); RequireNoError("glGetIntegerv");
    return static_cast<std::uint32_t>(buffer);
}

}  // namespace ogplay::gles

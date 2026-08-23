#include "ogplay/gles/angle_frame.h"

#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#if OGPLAY_HAS_ANGLE
#include <GLES2/gl2.h>
#endif

namespace ogplay::gles {
namespace {

void ValidateCount(const std::size_t count, const char* resource) {
    if (count > static_cast<std::size_t>(
                    (std::numeric_limits<std::int32_t>::max)())) {
        throw std::length_error(std::string("ANGLE ") + resource +
                                " count overflows GLsizei");
    }
}

}  // namespace

std::vector<std::uint32_t> AngleFrame::GenerateFramebuffers(
    const std::size_t count) {
    ValidateCount(count, "framebuffer");
#if OGPLAY_HAS_ANGLE
    std::vector<std::uint32_t> names(count);
    glGenFramebuffers(static_cast<GLsizei>(count), names.data());
    RequireNoError("glGenFramebuffers");
    return names;
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::DeleteFramebuffers(
    const std::span<const std::uint32_t> framebuffers) {
    ValidateCount(framebuffers.size(), "framebuffer");
#if OGPLAY_HAS_ANGLE
    glDeleteFramebuffers(static_cast<GLsizei>(framebuffers.size()),
                         framebuffers.data());
    RequireNoError("glDeleteFramebuffers");
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::BindFramebuffer(const std::uint32_t target,
                                 const std::uint32_t framebuffer) {
#if OGPLAY_HAS_ANGLE
    glBindFramebuffer(target, framebuffer);
    RequireNoError("glBindFramebuffer");
#else
    static_cast<void>(target); static_cast<void>(framebuffer);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::uint32_t AngleFrame::CheckFramebufferStatus(const std::uint32_t target) {
#if OGPLAY_HAS_ANGLE
    const auto status = glCheckFramebufferStatus(target);
    RequireNoError("glCheckFramebufferStatus");
    return status;
#else
    static_cast<void>(target);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

bool AngleFrame::IsFramebuffer(const std::uint32_t framebuffer) {
#if OGPLAY_HAS_ANGLE
    const auto result = glIsFramebuffer(framebuffer) == GL_TRUE;
    RequireNoError("glIsFramebuffer");
    return result;
#else
    static_cast<void>(framebuffer);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::int32_t AngleFrame::GetFramebufferAttachmentParameter(
    const std::uint32_t target, const std::uint32_t attachment,
    const std::uint32_t parameter) {
#if OGPLAY_HAS_ANGLE
    GLint value{};
    glGetFramebufferAttachmentParameteriv(target, attachment, parameter,
                                           &value);
    RequireNoError("glGetFramebufferAttachmentParameteriv");
    return value;
#else
    static_cast<void>(target); static_cast<void>(attachment);
    static_cast<void>(parameter);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::vector<std::uint32_t> AngleFrame::GenerateRenderbuffers(
    const std::size_t count) {
    ValidateCount(count, "renderbuffer");
#if OGPLAY_HAS_ANGLE
    std::vector<std::uint32_t> names(count);
    glGenRenderbuffers(static_cast<GLsizei>(count), names.data());
    RequireNoError("glGenRenderbuffers");
    return names;
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::DeleteRenderbuffers(
    const std::span<const std::uint32_t> renderbuffers) {
    ValidateCount(renderbuffers.size(), "renderbuffer");
#if OGPLAY_HAS_ANGLE
    glDeleteRenderbuffers(static_cast<GLsizei>(renderbuffers.size()),
                          renderbuffers.data());
    RequireNoError("glDeleteRenderbuffers");
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::BindRenderbuffer(const std::uint32_t target,
                                  const std::uint32_t renderbuffer) {
#if OGPLAY_HAS_ANGLE
    glBindRenderbuffer(target, renderbuffer);
    RequireNoError("glBindRenderbuffer");
#else
    static_cast<void>(target); static_cast<void>(renderbuffer);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

bool AngleFrame::IsRenderbuffer(const std::uint32_t renderbuffer) {
#if OGPLAY_HAS_ANGLE
    const auto result = glIsRenderbuffer(renderbuffer) == GL_TRUE;
    RequireNoError("glIsRenderbuffer");
    return result;
#else
    static_cast<void>(renderbuffer);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::int32_t AngleFrame::GetRenderbufferParameter(
    const std::uint32_t target, const std::uint32_t parameter) {
#if OGPLAY_HAS_ANGLE
    GLint value{};
    glGetRenderbufferParameteriv(target, parameter, &value);
    RequireNoError("glGetRenderbufferParameteriv");
    return value;
#else
    static_cast<void>(target); static_cast<void>(parameter);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::RenderbufferStorage(
    const std::uint32_t target, const std::uint32_t internal_format,
    const std::int32_t width, const std::int32_t height) {
#if OGPLAY_HAS_ANGLE
    glRenderbufferStorage(target, internal_format, width, height);
    RequireNoError("glRenderbufferStorage");
#else
    static_cast<void>(target); static_cast<void>(internal_format);
    static_cast<void>(width); static_cast<void>(height);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::FramebufferTexture2D(
    const std::uint32_t target, const std::uint32_t attachment,
    const std::uint32_t texture_target, const std::uint32_t texture,
    const std::int32_t level) {
#if OGPLAY_HAS_ANGLE
    glFramebufferTexture2D(target, attachment, texture_target, texture, level);
    RequireNoError("glFramebufferTexture2D");
#else
    static_cast<void>(target); static_cast<void>(attachment);
    static_cast<void>(texture_target); static_cast<void>(texture);
    static_cast<void>(level);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::FramebufferRenderbuffer(
    const std::uint32_t target, const std::uint32_t attachment,
    const std::uint32_t renderbuffer_target,
    const std::uint32_t renderbuffer) {
#if OGPLAY_HAS_ANGLE
    glFramebufferRenderbuffer(target, attachment, renderbuffer_target,
                              renderbuffer);
    RequireNoError("glFramebufferRenderbuffer");
#else
    static_cast<void>(target); static_cast<void>(attachment);
    static_cast<void>(renderbuffer_target); static_cast<void>(renderbuffer);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

}  // namespace ogplay::gles

#include "ogplay/gles/angle_frame.h"

#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#if OGPLAY_HAS_ANGLE
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
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

void AngleFrame::BindPackedDefault(EglHandle draw_image, EglHandle read_image, EglHandle draw_depth, EglHandle read_depth) {
#if OGPLAY_HAS_ANGLE
    if (!draw_image || !read_image) throw GlesApiError("packed framebuffer image", GL_INVALID_OPERATION);
    GLint rb{}, old_draw{}, old_read{};
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &rb);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read);
    auto bind = reinterpret_cast<void(*)(GLenum,void*)>(eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES"));
    if (!bind) throw GlesApiError("packed framebuffer import", GL_INVALID_OPERATION);
    std::array<GLuint,4> imported{};
    std::array<GLuint,2> framebuffers{};
    glGenRenderbuffers(4, imported.data());
    glGenFramebuffers(2, framebuffers.data());
    const auto clean_imports = [&] {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(rb));
        glDeleteRenderbuffers(4, imported.data());
    };
    try {
        RequireNoError("packed framebuffer allocation");
        for (std::size_t i = 0; i < 2; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[i]);
            glBindRenderbuffer(GL_RENDERBUFFER, imported[i]);
            bind(GL_RENDERBUFFER, reinterpret_cast<void*>(i ? read_image : draw_image));
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, imported[i]);
            const auto depth_image=i ? read_depth : draw_depth;
            if (depth_image) {
                glBindRenderbuffer(GL_RENDERBUFFER, imported[i+2]);
                bind(GL_RENDERBUFFER,reinterpret_cast<void*>(depth_image));
                glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,imported[i+2]);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,imported[i+2]);
            }
            RequireNoError("packed framebuffer import");
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw GlesApiError("packed framebuffer completeness", GL_INVALID_FRAMEBUFFER_OPERATION);
        }
        // Attachments retain the storage. Delete import names while neither
        // private FBO is bound; guest renderbuffer names cannot collide.
        clean_imports();
        RequireNoError("packed framebuffer cleanup");
    } catch (...) {
        clean_imports();
        glDeleteFramebuffers(2, framebuffers.data());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(old_draw));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(old_read));
        throw;
    }
    glDeleteFramebuffers(2, packed_framebuffers_.data());
    packed_framebuffers_ = framebuffers;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ResolveFramebuffer(GL_DRAW_FRAMEBUFFER, guest_framebuffer_bindings_[0], false));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, ResolveFramebuffer(GL_READ_FRAMEBUFFER, guest_framebuffer_bindings_[1], false));
    RequireNoError("packed framebuffer bind");
#else
    static_cast<void>(draw_image); static_cast<void>(read_image);static_cast<void>(draw_depth);static_cast<void>(read_depth);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::uint32_t AngleFrame::ResolveFramebuffer(std::uint32_t target,std::uint32_t guest,bool create) {
    if (!packed_default_) return guest;
    if (!guest) return packed_framebuffers_[target==0x8CA8U ? 1U : 0U];
    if (const auto found=framebuffer_names_.find(guest);found!=framebuffer_names_.end()) return found->second;
    if (!create) return 0;
#if OGPLAY_HAS_ANGLE
    GLuint native{};glGenFramebuffers(1,&native);RequireNoError("glGenFramebuffers");
    framebuffer_names_.emplace(guest,native);return native;
#else
    throw EglLifecycleError(EglOperation::unavailable,0);
#endif
}
std::int32_t AngleFrame::GuestFramebufferBinding(std::int32_t native) const {
    if (!packed_default_) return native;
    if (!native || static_cast<std::uint32_t>(native)==packed_framebuffers_[0] ||
        static_cast<std::uint32_t>(native)==packed_framebuffers_[1]) return 0;
    for (const auto& [guest,host]:framebuffer_names_) if (host==static_cast<std::uint32_t>(native)) return static_cast<std::int32_t>(guest);
    throw std::logic_error("unowned native framebuffer binding");
}
bool AngleFrame::IsDefaultFramebufferTarget(std::uint32_t target) const {
    return packed_default_ && guest_framebuffer_bindings_[target==0x8CA8U ? 1U : 0U]==0;
}

std::vector<std::uint32_t> AngleFrame::GenerateFramebuffers(
    const std::size_t count) {
    ValidateCount(count, "framebuffer");
#if OGPLAY_HAS_ANGLE
    if (packed_default_) {
        std::vector<std::uint32_t> names;
        names.reserve(count);
        for (std::size_t i=0;i<count;++i) {
            while (!next_framebuffer_name_ || framebuffer_names_.contains(next_framebuffer_name_)) ++next_framebuffer_name_;
            const auto guest=next_framebuffer_name_++;ResolveFramebuffer(GL_FRAMEBUFFER,guest,true);names.push_back(guest);
        }
        return names;
    }
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
    if (packed_default_) {
        for (const auto guest:framebuffers) {
            const auto found=framebuffer_names_.find(guest);
            if (!guest || found==framebuffer_names_.end()) continue;
            glDeleteFramebuffers(1,&found->second);framebuffer_names_.erase(found);
            for (std::size_t i=0;i<2;++i) if (guest_framebuffer_bindings_[i]==guest) {
                guest_framebuffer_bindings_[i]=0;
                glBindFramebuffer(i ? GL_READ_FRAMEBUFFER : GL_DRAW_FRAMEBUFFER,packed_framebuffers_[i]);
            }
        }
        RequireNoError("glDeleteFramebuffers");return;
    }
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
    if (target!=GL_FRAMEBUFFER && target!=GL_READ_FRAMEBUFFER && target!=GL_DRAW_FRAMEBUFFER)
        throw GlesApiError("glBindFramebuffer",GL_INVALID_ENUM);
    if (packed_default_ && target==GL_FRAMEBUFFER) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,ResolveFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer,true));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,ResolveFramebuffer(GL_READ_FRAMEBUFFER,framebuffer,true));
    } else glBindFramebuffer(target,ResolveFramebuffer(target,framebuffer,true));
    RequireNoError("glBindFramebuffer");
    if (packed_default_) {
        if (target!=GL_READ_FRAMEBUFFER) guest_framebuffer_bindings_[0]=framebuffer;
        if (target!=GL_DRAW_FRAMEBUFFER) guest_framebuffer_bindings_[1]=framebuffer;
    }
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
    if (packed_default_ && (!framebuffer || !framebuffer_names_.contains(framebuffer))) return false;
    const auto result = glIsFramebuffer(ResolveFramebuffer(GL_FRAMEBUFFER,framebuffer,false)) == GL_TRUE;
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
    auto host_attachment=attachment;
    if (IsDefaultFramebufferTarget(target)) {
        if (ClientVersion()<3 || attachment!=GL_BACK)
            throw GlesApiError("glGetFramebufferAttachmentParameteriv",GL_INVALID_OPERATION);
        if (parameter==GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE) return GL_FRAMEBUFFER_DEFAULT;
        if (parameter==GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME) return 0;
        host_attachment=GL_COLOR_ATTACHMENT0;
    }
    GLint value{};
    glGetFramebufferAttachmentParameteriv(target, host_attachment, parameter,
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
    if (IsDefaultFramebufferTarget(target)) throw GlesApiError("glFramebufferTexture2D",GL_INVALID_OPERATION);
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
    if (IsDefaultFramebufferTarget(target)) throw GlesApiError("glFramebufferRenderbuffer",GL_INVALID_OPERATION);
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

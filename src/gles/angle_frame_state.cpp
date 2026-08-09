#include "ogplay/gles/angle_frame.h"

#include <limits>
#include <stdexcept>

#if OGPLAY_HAS_ANGLE
#include <GLES2/gl2.h>
#endif

namespace ogplay::gles {

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
    glBlendFunc(source, destination); RequireNoError("glBlendFunc");
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
    static_cast<void>(red); static_cast<void>(green);
    static_cast<void>(blue); static_cast<void>(alpha);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::ClearStencil(const std::int32_t value) {
#if OGPLAY_HAS_ANGLE
    glClearStencil(value); RequireNoError("glClearStencil");
#else
    static_cast<void>(value); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::CullFace(const std::uint32_t mode) {
#if OGPLAY_HAS_ANGLE
    glCullFace(mode); RequireNoError("glCullFace");
#else
    static_cast<void>(mode); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::DepthFunction(const std::uint32_t function) {
#if OGPLAY_HAS_ANGLE
    glDepthFunc(function); RequireNoError("glDepthFunc");
#else
    static_cast<void>(function); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::DepthMask(const bool enabled) {
#if OGPLAY_HAS_ANGLE
    glDepthMask(enabled ? GL_TRUE : GL_FALSE); RequireNoError("glDepthMask");
#else
    static_cast<void>(enabled); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::DepthRange(const float near_value, const float far_value) {
#if OGPLAY_HAS_ANGLE
    glDepthRangef(near_value, far_value); RequireNoError("glDepthRangef");
#else
    static_cast<void>(near_value); static_cast<void>(far_value);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::Finish() {
#if OGPLAY_HAS_ANGLE
    glFinish(); RequireNoError("glFinish");
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::FrontFace(const std::uint32_t mode) {
#if OGPLAY_HAS_ANGLE
    glFrontFace(mode); RequireNoError("glFrontFace");
#else
    static_cast<void>(mode); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::Hint(const std::uint32_t target, const std::uint32_t mode) {
#if OGPLAY_HAS_ANGLE
    glHint(target, mode); RequireNoError("glHint");
#else
    static_cast<void>(target); static_cast<void>(mode);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::LineWidth(const float width) {
#if OGPLAY_HAS_ANGLE
    glLineWidth(width); RequireNoError("glLineWidth");
#else
    static_cast<void>(width); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::PolygonOffset(const float factor, const float units) {
#if OGPLAY_HAS_ANGLE
    glPolygonOffset(factor, units); RequireNoError("glPolygonOffset");
#else
    static_cast<void>(factor); static_cast<void>(units);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::StencilFunction(const std::uint32_t function,
                                 const std::int32_t reference,
                                 const std::uint32_t mask) {
#if OGPLAY_HAS_ANGLE
    glStencilFunc(function, reference, mask); RequireNoError("glStencilFunc");
#else
    static_cast<void>(function); static_cast<void>(reference); static_cast<void>(mask);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::StencilMask(const std::uint32_t mask) {
#if OGPLAY_HAS_ANGLE
    glStencilMask(mask); RequireNoError("glStencilMask");
#else
    static_cast<void>(mask); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

void AngleFrame::StencilOperation(const std::uint32_t stencil_fail,
                                  const std::uint32_t depth_fail,
                                  const std::uint32_t depth_pass) {
#if OGPLAY_HAS_ANGLE
    glStencilOp(stencil_fail, depth_fail, depth_pass); RequireNoError("glStencilOp");
#else
    static_cast<void>(stencil_fail); static_cast<void>(depth_fail);
    static_cast<void>(depth_pass); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::vector<std::int32_t> AngleFrame::GetIntegers(
    const std::uint32_t parameter, const std::size_t count) {
    if (count > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) {
        throw std::length_error("ANGLE integer query count overflows GLsizei");
    }
#if OGPLAY_HAS_ANGLE
    std::vector<std::int32_t> values(count);
    glGetIntegerv(parameter, values.data()); RequireNoError("glGetIntegerv");
    return values;
#else
    static_cast<void>(parameter); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::string AngleFrame::GetString(const std::uint32_t parameter) {
#if OGPLAY_HAS_ANGLE
    const auto* value = glGetString(parameter); RequireNoError("glGetString");
    if (value == nullptr) throw std::runtime_error("glGetString returned null");
    return reinterpret_cast<const char*>(value);
#else
    static_cast<void>(parameter); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

std::uint32_t AngleFrame::GetError() noexcept {
#if OGPLAY_HAS_ANGLE
    return glGetError();
#else
    return 0;
#endif
}

}  // namespace ogplay::gles

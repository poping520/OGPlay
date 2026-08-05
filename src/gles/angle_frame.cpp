#include "ogplay/gles/angle_frame.h"

#include <algorithm>
#include <limits>
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
    return {width_, height_, clear_count_, readback_count_};
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

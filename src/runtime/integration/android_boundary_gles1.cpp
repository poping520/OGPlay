#include "android_boundary_gles1.h"

#include <bit>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>

namespace ogplay::runtime::detail {
namespace {

[[nodiscard]] std::size_t HintIndex(const std::uint32_t target) {
    switch (target) {
    case 0x0C50U: return 0;  // GL_PERSPECTIVE_CORRECTION_HINT
    case 0x0C51U: return 1;  // GL_POINT_SMOOTH_HINT
    case 0x0C52U: return 2;  // GL_LINE_SMOOTH_HINT
    case 0x0C54U: return 3;  // GL_FOG_HINT
    case kGles1GenerateMipmapHint: return 4;
    default: throw std::invalid_argument("glHint target is invalid for GLES1");
    }
}

[[nodiscard]] bool SharedCapability(const std::uint32_t capability) noexcept {
    switch (capability) {
    case 0x0BE2U:  // GL_BLEND
    case 0x0B44U:  // GL_CULL_FACE
    case 0x0B71U:  // GL_DEPTH_TEST
    case 0x0BD0U:  // GL_DITHER
    case 0x8037U:  // GL_POLYGON_OFFSET_FILL
    case 0x809EU:  // GL_SAMPLE_ALPHA_TO_COVERAGE
    case 0x80A0U:  // GL_SAMPLE_COVERAGE
    case 0x0C11U:  // GL_SCISSOR_TEST
    case 0x0B90U:  // GL_STENCIL_TEST
        return true;
    default: return false;
    }
}

[[nodiscard]] bool FixedCapability(const std::uint32_t capability) noexcept {
    switch (capability) {
    case 0x0BC0U:  // GL_ALPHA_TEST
    case 0x0BF2U:  // GL_COLOR_LOGIC_OP
    case 0x0B60U:  // GL_FOG
    case 0x0B50U:  // GL_LIGHTING
    case 0x0B57U:  // GL_COLOR_MATERIAL
    case 0x0BA1U:  // GL_NORMALIZE
    case 0x803AU:  // GL_RESCALE_NORMAL
    case 0x0DE1U:  // GL_TEXTURE_2D
    case 0x0B10U:  // GL_POINT_SMOOTH
    case 0x0B20U:  // GL_LINE_SMOOTH
    case 0x809DU:  // GL_MULTISAMPLE
    case 0x809FU:  // GL_SAMPLE_ALPHA_TO_ONE
        return true;
    default:
        return (capability >= 0x3000U && capability <= 0x3005U) ||
               (capability >= 0x4000U && capability <= 0x4007U);
    }
}

[[nodiscard]] std::uint64_t CapabilityKey(
    const std::uint32_t capability, const std::uint32_t active_texture) {
    if (!SharedCapability(capability) && !FixedCapability(capability)) {
        throw std::invalid_argument("GLES1 capability is invalid");
    }
    if (capability == 0x0DE1U) {
        return (static_cast<std::uint64_t>(active_texture) << 32U) |
               capability;
    }
    return capability;
}

}  // namespace

void AndroidBoundaryGles1State::Reset() noexcept {
    shade_model_ = kGles1SmoothShadeModel;
    hints_.fill(kGles1DontCare);
    active_texture_ = 0x84C0U;
    capabilities_.clear();
    transfer_state_ = {};
}

void AndroidBoundaryGles1State::SetShadeModel(const std::uint32_t mode) {
    if (mode != kGles1FlatShadeModel && mode != kGles1SmoothShadeModel) {
        throw std::invalid_argument(
            "glShadeModel mode must be GL_FLAT or GL_SMOOTH");
    }
    shade_model_ = mode;
}

std::uint32_t AndroidBoundaryGles1State::ShadeModel() const noexcept {
    return shade_model_;
}

const gles::GlesTransferState&
AndroidBoundaryGles1State::TransferState() const noexcept {
    return transfer_state_;
}

void AndroidBoundaryGles1State::SetTransferState(
    gles::GlesTransferState state) noexcept {
    transfer_state_ = std::move(state);
}

void AndroidBoundaryGles1State::SetHint(const std::uint32_t target,
                                        const std::uint32_t mode) {
    if (mode != kGles1DontCare && mode != 0x1101U && mode != 0x1102U) {
        throw std::invalid_argument("glHint mode is invalid for GLES1");
    }
    hints_[HintIndex(target)] = mode;
}

std::uint32_t AndroidBoundaryGles1State::Hint(
    const std::uint32_t target) const {
    return hints_[HintIndex(target)];
}

void AndroidBoundaryGles1State::SetActiveTexture(
    const std::uint32_t texture) noexcept {
    active_texture_ = texture;
}

std::uint32_t AndroidBoundaryGles1State::ActiveTexture() const noexcept {
    return active_texture_;
}

void AndroidBoundaryGles1State::SetCapability(
    const std::uint32_t capability, const bool enabled) {
    capabilities_[CapabilityKey(capability, active_texture_)] = enabled;
}

bool AndroidBoundaryGles1State::Capability(
    const std::uint32_t capability) const {
    const auto found = capabilities_.find(
        CapabilityKey(capability, active_texture_));
    return found != capabilities_.end() && found->second;
}

std::int32_t ScaleAndroidBoundaryViewportComponent(
    const std::int32_t value, const std::uint32_t factor) {
    const auto scaled = static_cast<std::int64_t>(value) * factor;
    if (scaled < std::numeric_limits<std::int32_t>::min() ||
        scaled > std::numeric_limits<std::int32_t>::max()) {
        throw std::overflow_error("supersampled viewport component overflows");
    }
    return static_cast<std::int32_t>(scaled);
}

void BindAndroidBoundaryGles1Core(
    gles::GlesDispatchTable& dispatch, AndroidBoundaryGles1State& state,
    const std::uint32_t supersample_factor,
    AndroidBoundaryFrameResolver require_frame) {
    if (supersample_factor == 0 || !require_frame) {
        throw std::invalid_argument("GLES1 boundary binding is incomplete");
    }
    dispatch.Bind(
        "glActiveTexture",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            require_frame("glActiveTexture").ActiveTexture(arguments[0]);
            state.SetActiveTexture(arguments[0]);
            return 0U;
        });
    dispatch.Bind(
        "glBindBuffer",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            auto next = state.TransferState();
            next.BindBuffer(arguments[0], arguments[1]);
            require_frame("glBindBuffer")
                .BindBuffer(arguments[0], arguments[1]);
            state.SetTransferState(std::move(next));
            return 0U;
        });
    dispatch.Bind(
        "glBindTexture",
        [require_frame](const std::span<const std::uint32_t> arguments,
                        const std::uint64_t) {
            require_frame("glBindTexture")
                .BindTexture(arguments[0], arguments[1]);
            return 0U;
        });
    dispatch.Bind(
        "glBlendFunc",
        [require_frame](const std::span<const std::uint32_t> arguments,
                        const std::uint64_t) {
            require_frame("glBlendFunc")
                .BlendFunction(arguments[0], arguments[1]);
            return 0U;
        });
    dispatch.Bind(
        "glColorMask",
        [require_frame](const std::span<const std::uint32_t> arguments,
                        const std::uint64_t) {
            require_frame("glColorMask")
                .ColorMask(arguments[0] != 0U, arguments[1] != 0U,
                           arguments[2] != 0U, arguments[3] != 0U);
            return 0U;
        });
    dispatch.Bind(
        "glCullFace",
        [require_frame](const std::span<const std::uint32_t> arguments,
                        const std::uint64_t) {
            require_frame("glCullFace").CullFace(arguments[0]);
            return 0U;
        });
    dispatch.Bind(
        "glDepthFunc",
        [require_frame](const std::span<const std::uint32_t> arguments,
                        const std::uint64_t) {
            require_frame("glDepthFunc").DepthFunction(arguments[0]);
            return 0U;
        });
    dispatch.Bind(
        "glDepthMask",
        [require_frame](const std::span<const std::uint32_t> arguments,
                        const std::uint64_t) {
            require_frame("glDepthMask").DepthMask(arguments[0] != 0U);
            return 0U;
        });
    dispatch.Bind(
        "glDisable",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            auto& frame = require_frame("glDisable");
            if (SharedCapability(arguments[0])) {
                frame.SetCapability(arguments[0], false);
            }
            state.SetCapability(arguments[0], false);
            return 0U;
        });
    dispatch.Bind(
        "glFinish",
        [require_frame](const std::span<const std::uint32_t>,
                        const std::uint64_t) {
            require_frame("glFinish").Finish();
            return 0U;
        });
    dispatch.Bind(
        "glFrontFace",
        [require_frame](const std::span<const std::uint32_t> arguments,
                        const std::uint64_t) {
            require_frame("glFrontFace").FrontFace(arguments[0]);
            return 0U;
        });
    dispatch.Bind(
        "glGetError",
        [require_frame](const std::span<const std::uint32_t>,
                        const std::uint64_t) {
            return require_frame("glGetError").GetError();
        });
    dispatch.Bind(
        "glHint",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            auto& frame = require_frame("glHint");
            if (arguments[0] == kGles1GenerateMipmapHint) {
                frame.Hint(arguments[0], arguments[1]);
            }
            state.SetHint(arguments[0], arguments[1]);
            return 0U;
        });
    dispatch.Bind(
        "glPixelStorei",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            const auto value = std::bit_cast<std::int32_t>(arguments[1]);
            auto next = state.TransferState();
            next.PixelStore(arguments[0], value);
            require_frame("glPixelStorei").PixelStore(arguments[0], value);
            state.SetTransferState(std::move(next));
            return 0U;
        });
    dispatch.Bind(
        "glTexParameterf",
        [require_frame](const std::span<const std::uint32_t> arguments,
                        const std::uint64_t) {
            require_frame("glTexParameterf")
                .TextureParameterFloat(
                    arguments[0], arguments[1],
                    std::bit_cast<float>(arguments[2]));
            return 0U;
        });
    dispatch.Bind(
        "glTexParameteri",
        [require_frame](const std::span<const std::uint32_t> arguments,
                        const std::uint64_t) {
            require_frame("glTexParameteri")
                .TextureParameter(
                    arguments[0], arguments[1],
                    std::bit_cast<std::int32_t>(arguments[2]));
            return 0U;
        });
    dispatch.Bind(
        "glViewport",
        [supersample_factor, require_frame](
            const std::span<const std::uint32_t> arguments, const std::uint64_t) {
            require_frame("glViewport")
                .Viewport(ScaleAndroidBoundaryViewportComponent(
                              std::bit_cast<std::int32_t>(arguments[0]),
                              supersample_factor),
                          ScaleAndroidBoundaryViewportComponent(
                              std::bit_cast<std::int32_t>(arguments[1]),
                              supersample_factor),
                          ScaleAndroidBoundaryViewportComponent(
                              std::bit_cast<std::int32_t>(arguments[2]),
                              supersample_factor),
                          ScaleAndroidBoundaryViewportComponent(
                              std::bit_cast<std::int32_t>(arguments[3]),
                              supersample_factor));
            return 0U;
        });
    dispatch.Bind(
        "glScissor",
        [supersample_factor, require_frame](
            const std::span<const std::uint32_t> arguments, const std::uint64_t) {
            require_frame("glScissor")
                .Scissor(ScaleAndroidBoundaryViewportComponent(
                             std::bit_cast<std::int32_t>(arguments[0]),
                             supersample_factor),
                         ScaleAndroidBoundaryViewportComponent(
                             std::bit_cast<std::int32_t>(arguments[1]),
                             supersample_factor),
                         ScaleAndroidBoundaryViewportComponent(
                             std::bit_cast<std::int32_t>(arguments[2]),
                             supersample_factor),
                         ScaleAndroidBoundaryViewportComponent(
                             std::bit_cast<std::int32_t>(arguments[3]),
                             supersample_factor));
            return 0U;
        });
    dispatch.Bind(
        "glClear",
        [require_frame](const std::span<const std::uint32_t> arguments,
                        const std::uint64_t) {
            require_frame("glClear").Clear(arguments[0]);
            return 0U;
        });
    dispatch.Bind(
        "glClearColor",
        [require_frame](
            const std::span<const std::uint32_t> arguments, const std::uint64_t) {
            require_frame("glClearColor")
                .ClearColor(std::bit_cast<float>(arguments[0]),
                            std::bit_cast<float>(arguments[1]),
                            std::bit_cast<float>(arguments[2]),
                            std::bit_cast<float>(arguments[3]));
            return 0U;
        });
    dispatch.Bind(
        "glClearDepthf",
        [require_frame](
            const std::span<const std::uint32_t> arguments, const std::uint64_t) {
            require_frame("glClearDepthf").ClearDepth(
                std::bit_cast<float>(arguments[0]));
            return 0U;
        });
    dispatch.Bind(
        "glEnable",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            auto& frame = require_frame("glEnable");
            if (SharedCapability(arguments[0])) {
                frame.SetCapability(arguments[0], true);
            }
            state.SetCapability(arguments[0], true);
            return 0U;
        });
    dispatch.Bind(
        "glShadeModel",
        [&state, require_frame = std::move(require_frame)](
            const std::span<const std::uint32_t> arguments, const std::uint64_t) {
            static_cast<void>(require_frame("glShadeModel"));
            state.SetShadeModel(arguments[0]);
            return 0U;
        });
}

}  // namespace ogplay::runtime::detail

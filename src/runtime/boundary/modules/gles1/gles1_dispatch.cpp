#include "gles1_dispatch.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include "ogplay/gles/guest_transfer.h"
#include "ogplay/memory/address_space.h"
#include "runtime/boundary/services/gles_transfer_io.h"
#include "gles1_fixed.h"
#include "gles1_support.h"

namespace ogplay::runtime::detail {
namespace {
constexpr std::size_t kMaximumMatrixStackDepth = 32;
constexpr std::uint32_t kGles1Texture2d = 0x0DE1U;
constexpr std::uint32_t kGles1TextureCubeMap = 0x8513U;
[[nodiscard]] std::string TextureTargetMessage(
    const std::uint32_t target) {
    std::ostringstream stream;
    stream << "GLES1 texture target must be GL_TEXTURE_2D, got 0x" << std::hex
           << target;
    return stream.str();
}
// OES_texture_cube_map addresses bindings, parameters and derived state
// through the cube map target; only image uploads name individual faces.
void RequireTextureBindingTarget(const std::uint32_t target) {
    if (target != kGles1Texture2d && target != kGles1TextureCubeMap) {
        throw std::invalid_argument(TextureTargetMessage(target));
    }
}
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
    case 0x8513U:  // GL_TEXTURE_CUBE_MAP_OES
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
    if (capability == 0x0DE1U || capability == 0x8513U) {
        return (static_cast<std::uint64_t>(active_texture) << 32U) |
               capability;
    }
    return capability;
}
[[nodiscard]] gles::GuestBuffer PrepareTextureNames(
    memory::AddressSpace& address_space, const std::uint32_t count_word,
    const std::uint32_t address, const gles::GuestTransferDirection direction,
    const std::uint64_t thread_id, const std::string_view operation) {
    const auto count = std::bit_cast<std::int32_t>(count_word);
    if (count < 0) {
        throw std::invalid_argument(std::string(operation) +
                                    " count cannot be negative");
    }
    return gles::GuestBuffer::Prepare(
        address_space, memory::GuestAddress{address},
        static_cast<std::uint64_t>(count) * sizeof(std::uint32_t), direction,
        false, thread_id);
}

[[nodiscard]] std::vector<std::uint32_t> ReadTextureNames(
    const gles::GuestBuffer& transfer) {
    return gles_io::LoadWordsLE(transfer.Bytes());
}

void WriteTextureNames(gles::GuestBuffer& transfer,
                       const std::span<const std::uint32_t> names) {
    gles_io::WriteWordsExact(transfer, names,
                             "GLES1 texture name output size differs");
}

}  // namespace

AndroidBoundaryGles1MatrixState::AndroidBoundaryGles1MatrixState(
    const SharedGlState& shared) : shared_(&shared) {
    Reset();
}

void AndroidBoundaryGles1MatrixState::Reset() {
    mode_ = kGles1Modelview;
    modelview_.assign(1, Gles1IdentityMatrix());
    projection_.assign(1, Gles1IdentityMatrix());
    for (auto& texture : textures_) texture.assign(1, Gles1IdentityMatrix());
}

void AndroidBoundaryGles1MatrixState::SetMode(const std::uint32_t mode) {
    static_cast<void>(Stack(mode, ActiveTexture()));
    mode_ = mode;
}

std::uint32_t AndroidBoundaryGles1MatrixState::Mode() const noexcept {
    return mode_;
}

void AndroidBoundaryGles1MatrixState::LoadIdentity() {
    CurrentStack().back() = Gles1IdentityMatrix();
}

void AndroidBoundaryGles1MatrixState::Load(
    const std::span<const float, 16> matrix) {
    RequireFiniteGles1MatrixValues(matrix);
    std::ranges::copy(matrix, CurrentStack().back().begin());
}

void AndroidBoundaryGles1MatrixState::Push() {
    auto& stack = CurrentStack();
    if (stack.size() >= kMaximumMatrixStackDepth) {
        throw std::overflow_error("GLES1 matrix stack overflow");
    }
    stack.push_back(stack.back());
}

void AndroidBoundaryGles1MatrixState::Pop() {
    auto& stack = CurrentStack();
    if (stack.size() == 1) {
        throw std::underflow_error("GLES1 matrix stack underflow");
    }
    stack.pop_back();
}

void AndroidBoundaryGles1MatrixState::Rotate(
    const float angle_degrees, const float x, const float y, const float z) {
    const std::array values{angle_degrees, x, y, z};
    RequireFiniteGles1MatrixValues(values);
    const auto length = std::sqrt(x * x + y * y + z * z);
    if (length == 0.0F || !std::isfinite(length)) {
        throw std::invalid_argument("GLES1 rotation axis is invalid");
    }
    const auto nx = x / length;
    const auto ny = y / length;
    const auto nz = z / length;
    const auto radians = angle_degrees * std::numbers::pi_v<float> / 180.0F;
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    const auto one_minus_cosine = 1.0F - cosine;
    const Gles1Matrix rotation{
        nx * nx * one_minus_cosine + cosine,
        ny * nx * one_minus_cosine + nz * sine,
        nx * nz * one_minus_cosine - ny * sine, 0.0F,
        nx * ny * one_minus_cosine - nz * sine,
        ny * ny * one_minus_cosine + cosine,
        ny * nz * one_minus_cosine + nx * sine, 0.0F,
        nx * nz * one_minus_cosine + ny * sine,
        ny * nz * one_minus_cosine - nx * sine,
        nz * nz * one_minus_cosine + cosine, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    auto& current = CurrentStack().back();
    const auto next = Gles1MultiplyMatrices(current, rotation);
    RequireFiniteGles1MatrixValues(next);
    current = next;
}

void AndroidBoundaryGles1MatrixState::Translate(
    const float x, const float y, const float z) {
    const std::array values{x, y, z};
    RequireFiniteGles1MatrixValues(values);
    auto translation = Gles1IdentityMatrix();
    translation[12] = x;
    translation[13] = y;
    translation[14] = z;
    auto& current = CurrentStack().back();
    const auto next = Gles1MultiplyMatrices(current, translation);
    RequireFiniteGles1MatrixValues(next);
    current = next;
}

void AndroidBoundaryGles1MatrixState::Multiply(
    const std::span<const float, 16> matrix) {
    RequireFiniteGles1MatrixValues(matrix);
    Gles1Matrix right{};
    std::ranges::copy(matrix, right.begin());
    auto& current = CurrentStack().back();
    const auto next = Gles1MultiplyMatrices(current, right);
    RequireFiniteGles1MatrixValues(next);
    current = next;
}

const Gles1Matrix& AndroidBoundaryGles1MatrixState::Current() const noexcept {
    return Stack(mode_, ActiveTexture()).back();
}

const Gles1Matrix& AndroidBoundaryGles1MatrixState::Current(
    const std::uint32_t mode, const std::uint32_t texture) const {
    return Stack(mode, texture).back();
}

std::size_t AndroidBoundaryGles1MatrixState::StackDepth(
    const std::uint32_t mode) const {
    return Stack(mode, ActiveTexture()).size();
}

std::vector<Gles1Matrix>&
AndroidBoundaryGles1MatrixState::CurrentStack() noexcept {
    if (mode_ == kGles1Projection) return projection_;
    if (mode_ == kGles1Texture) {
        return textures_.at(ActiveTexture() - 0x84C0U);
    }
    return modelview_;
}

const std::vector<Gles1Matrix>& AndroidBoundaryGles1MatrixState::Stack(
    const std::uint32_t mode, const std::uint32_t texture) const {
    if (mode == kGles1Modelview) return modelview_;
    if (mode == kGles1Projection) return projection_;
    if (mode == kGles1Texture) return textures_.at(texture - 0x84C0U);
    throw std::invalid_argument("glMatrixMode mode is invalid for GLES1");
}

std::uint32_t AndroidBoundaryGles1MatrixState::ActiveTexture() const noexcept {
    return shared_->active_texture;
}

AndroidBoundaryGles1State::AndroidBoundaryGles1State()
    : matrices_(*shared_),
      fixed_(std::make_unique<AndroidBoundaryGles1FixedState>()) {
    Reset();
}

AndroidBoundaryGles1State::AndroidBoundaryGles1State(SharedGlState& shared)
    : shared_(&shared),
      matrices_(*shared_),
      fixed_(std::make_unique<AndroidBoundaryGles1FixedState>()) {
    Reset();
}

AndroidBoundaryGles1State::~AndroidBoundaryGles1State() = default;

void AndroidBoundaryGles1State::Reset() {
    shade_model_ = kGles1SmoothShadeModel;
    hints_.fill(kGles1DontCare);
    shared_->Reset();
    capabilities_.clear();
    logic_operation_ = 0x1503U;
    matrices_.Reset();
    fixed_->Reset();
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
    return shared_->transfer;
}

void AndroidBoundaryGles1State::SetTransferState(
    gles::GlesTransferState state) noexcept {
    shared_->transfer = std::move(state);
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

void AndroidBoundaryGles1State::ValidateActiveTexture(
    const std::uint32_t texture) const {
    if (texture < 0x84C0U || texture > 0x84DFU) {
        throw std::invalid_argument(
            "GLES1 texture unit is outside GL_TEXTURE0..31");
    }
}

void AndroidBoundaryGles1State::SetActiveTexture(
    const std::uint32_t texture) {
    ValidateActiveTexture(texture);
    shared_->SetActiveTexture(texture);
}

std::uint32_t AndroidBoundaryGles1State::ActiveTexture() const noexcept {
    return shared_->active_texture;
}

void AndroidBoundaryGles1State::ValidateTextureTarget(
    const std::uint32_t target) const {
    RequireTextureBindingTarget(target);
}

void AndroidBoundaryGles1State::BindTexture(
    const std::uint32_t target, const std::uint32_t texture) {
    RequireTextureBindingTarget(target);
    shared_->BindTexture(target, texture);
}

std::uint32_t AndroidBoundaryGles1State::BoundTexture(
    const std::uint32_t target) const {
    RequireTextureBindingTarget(target);
    return shared_->BoundTexture(target);
}

void AndroidBoundaryGles1State::DeleteTextures(
    const std::span<const std::uint32_t> textures) noexcept {
    shared_->DeleteTextures(textures);
}

void AndroidBoundaryGles1State::SetTextureBaseFormat(
    const std::uint32_t target, const std::uint32_t format) {
    shared_->SetTextureBaseFormat(target, format);
}

std::optional<std::uint32_t> AndroidBoundaryGles1State::TextureBaseFormat(
    const std::uint32_t target) const {
    return shared_->TextureBaseFormat(target);
}

std::optional<std::uint32_t> AndroidBoundaryGles1State::TextureBaseFormat(
    const std::uint32_t texture_unit, const std::uint32_t target) const {
    if (texture_unit < 0x84C0U || texture_unit > 0x84DFU) {
        throw std::invalid_argument(
            "GLES1 texture unit is outside GL_TEXTURE0..31");
    }
    return shared_->TextureBaseFormat(texture_unit, target);
}

void AndroidBoundaryGles1State::SetGenerateMipmap(
    const std::uint32_t target, const bool enabled) {
    RequireTextureBindingTarget(target);
    shared_->SetGenerateMipmap(target, enabled);
}

bool AndroidBoundaryGles1State::GenerateMipmapEnabled(
    const std::uint32_t target) const {
    return shared_->GenerateMipmapEnabled(target);
}

void AndroidBoundaryGles1State::SetCapability(
    const std::uint32_t capability, const bool enabled) {
    if (SharedCapability(capability)) {
        shared_->SetCapability(capability, enabled);
        return;
    }
    capabilities_[CapabilityKey(capability, shared_->active_texture)] = enabled;
}

bool AndroidBoundaryGles1State::Capability(
    const std::uint32_t capability) const {
    return Capability(shared_->active_texture, capability);
}

bool AndroidBoundaryGles1State::Capability(
    const std::uint32_t texture_unit, const std::uint32_t capability) const {
    if (SharedCapability(capability)) return shared_->Capability(capability);
    const auto found = capabilities_.find(
        CapabilityKey(capability, texture_unit));
    return found != capabilities_.end() && found->second;
}

std::vector<std::uint32_t> AndroidBoundaryGles1State::EnabledTextureUnits() const {
    std::vector<std::uint32_t> result;
    for (auto texture = 0x84C0U; texture <= 0x84DFU; ++texture) {
        if (Capability(texture, 0x0DE1U) ||
            Capability(texture, 0x8513U)) {
            result.push_back(texture);
        }
    }
    return result;
}

void AndroidBoundaryGles1State::SetLogicOperation(
    const std::uint32_t operation) {
    if (operation < 0x1500U || operation > 0x150FU) {
        throw std::invalid_argument("GLES1 logic operation is invalid");
    }
    logic_operation_ = operation;
}

std::uint32_t AndroidBoundaryGles1State::LogicOperation() const noexcept {
    return logic_operation_;
}

AndroidBoundaryGles1MatrixState&
AndroidBoundaryGles1State::Matrices() noexcept {
    return matrices_;
}

const AndroidBoundaryGles1MatrixState&
AndroidBoundaryGles1State::Matrices() const noexcept {
    return matrices_;
}

AndroidBoundaryGles1FixedState& AndroidBoundaryGles1State::Fixed() noexcept {
    return *fixed_;
}

std::uint32_t AndroidBoundaryGles1State::TakeGuestError() const noexcept {
    return shared_->TakeGuestError();
}

const AndroidBoundaryGles1FixedState&
AndroidBoundaryGles1State::Fixed() const noexcept {
    return *fixed_;
}

SharedGlState& AndroidBoundaryGles1State::Shared() noexcept { return *shared_; }

const SharedGlState& AndroidBoundaryGles1State::Shared() const noexcept {
    return *shared_;
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
    memory::AddressSpace& address_space, const std::uint32_t supersample_factor,
    AndroidBoundaryFrameResolver require_frame) {
    if (supersample_factor == 0 || !require_frame) {
        throw std::invalid_argument("GLES1 boundary binding is incomplete");
    }
    BindAndroidBoundaryGles1FixedState(
        dispatch, state.Fixed(), address_space, require_frame, &state.Shared());
    dispatch.Bind(
        "glGenTextures",
        [&address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            auto output = PrepareTextureNames(
                address_space, arguments[0], arguments[1],
                gles::GuestTransferDirection::output, thread_id,
                "glGenTextures");
            const auto names = require_frame("glGenTextures")
                                   .GenerateTextures(arguments[0]);
            WriteTextureNames(output, names);
            return 0U;
        });
    dispatch.Bind(
        "glDeleteTextures",
        [&state, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            const auto input = PrepareTextureNames(
                address_space, arguments[0], arguments[1],
                gles::GuestTransferDirection::input, thread_id,
                "glDeleteTextures");
            const auto names = ReadTextureNames(input);
            require_frame("glDeleteTextures").DeleteTextures(names);
            state.DeleteTextures(names);
            return 0U;
        });
    dispatch.Bind(
        "glMatrixMode",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            static_cast<void>(require_frame("glMatrixMode"));
            state.Matrices().SetMode(arguments[0]);
            return 0U;
        });
    dispatch.Bind(
        "glLoadIdentity",
        [&state, require_frame](const std::span<const std::uint32_t>,
                                const std::uint64_t) {
            static_cast<void>(require_frame("glLoadIdentity"));
            state.Matrices().LoadIdentity();
            return 0U;
        });
    dispatch.Bind(
        "glLoadMatrixf",
        [&state, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            const auto matrix = ReadGuestGles1Matrix(
                address_space, arguments[0], thread_id);
            static_cast<void>(require_frame("glLoadMatrixf"));
            state.Matrices().Load(matrix);
            return 0U;
        });
    dispatch.Bind(
        "glPushMatrix",
        [&state, require_frame](const std::span<const std::uint32_t>,
                                const std::uint64_t) {
            static_cast<void>(require_frame("glPushMatrix"));
            state.Matrices().Push();
            return 0U;
        });
    dispatch.Bind(
        "glPopMatrix",
        [&state, require_frame](const std::span<const std::uint32_t>,
                                const std::uint64_t) {
            static_cast<void>(require_frame("glPopMatrix"));
            state.Matrices().Pop();
            return 0U;
        });
    dispatch.Bind(
        "glRotatef",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            static_cast<void>(require_frame("glRotatef"));
            state.Matrices().Rotate(
                std::bit_cast<float>(arguments[0]),
                std::bit_cast<float>(arguments[1]),
                std::bit_cast<float>(arguments[2]),
                std::bit_cast<float>(arguments[3]));
            return 0U;
        });
    dispatch.Bind(
        "glTranslatef",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            static_cast<void>(require_frame("glTranslatef"));
            state.Matrices().Translate(
                std::bit_cast<float>(arguments[0]),
                std::bit_cast<float>(arguments[1]),
                std::bit_cast<float>(arguments[2]));
            return 0U;
        });
    dispatch.Bind(
        "glActiveTexture",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            state.ValidateActiveTexture(arguments[0]);
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
        [&state, require_frame](const std::span<const std::uint32_t> arguments,
                               const std::uint64_t) {
            state.ValidateTextureTarget(arguments[0]);
            require_frame("glBindTexture")
                .BindTexture(arguments[0], arguments[1]);
            state.BindTexture(arguments[0], arguments[1]);
            return 0U;
        });
    dispatch.Bind("glBlendFunc", [require_frame](const auto arguments, const auto) {
        require_frame("glBlendFunc").BlendFunction(arguments[0], arguments[1]);
        return 0U;
    });
    dispatch.Bind("glColorMask", [require_frame](const auto arguments, const auto) {
        require_frame("glColorMask").ColorMask(
            arguments[0] != 0U, arguments[1] != 0U,
            arguments[2] != 0U, arguments[3] != 0U);
        return 0U;
    });
    dispatch.Bind("glCullFace", [require_frame](const auto arguments, const auto) {
        require_frame("glCullFace").CullFace(arguments[0]);
        return 0U;
    });
    dispatch.Bind("glDepthFunc", [require_frame](const auto arguments, const auto) {
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
        "glFlush",
        [require_frame](const std::span<const std::uint32_t>,
                        const std::uint64_t) {
            require_frame("glFlush").Flush();
            return 0U;
        });
    dispatch.Bind("glSampleCoverage", [require_frame](const auto arguments,
                                                       const auto) {
        require_frame("glSampleCoverage")
            .SampleCoverage(std::bit_cast<float>(arguments[0]),
                            arguments[1] != 0U);
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
        [&state, require_frame](const std::span<const std::uint32_t>,
                        const std::uint64_t) {
            // Guest-invalid enums latch into the shared per-context error
            // slot; drain it before the backend flag just like a driver
            // reports its own accumulated errors.
            const auto latched = state.TakeGuestError();
            if (latched != 0U) return latched;
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
        [&state, require_frame](const std::span<const std::uint32_t> arguments,
                               const std::uint64_t) {
            if (arguments[1] == kGles1GenerateMipmap) {
                const auto value = std::bit_cast<float>(arguments[2]);
                if (value != 0.0F && value != 1.0F) {
                    throw std::invalid_argument(
                        "GLES1 GL_GENERATE_MIPMAP must be GL_FALSE or GL_TRUE");
                }
                static_cast<void>(require_frame("glTexParameterf"));
                state.SetGenerateMipmap(arguments[0], value != 0.0F);
                return 0U;
            }
            require_frame("glTexParameterf")
                .TextureParameterFloat(
                    arguments[0], arguments[1],
                    std::bit_cast<float>(arguments[2]));
            return 0U;
        });
    dispatch.Bind(
        "glTexParameteri",
        [&state, require_frame](const std::span<const std::uint32_t> arguments,
                               const std::uint64_t) {
            if (arguments[1] == kGles1GenerateMipmap) {
                const auto value = std::bit_cast<std::int32_t>(arguments[2]);
                if (value != 0 && value != 1) {
                    throw std::invalid_argument(
                        "GLES1 GL_GENERATE_MIPMAP must be GL_FALSE or GL_TRUE");
                }
                static_cast<void>(require_frame("glTexParameteri"));
                state.SetGenerateMipmap(arguments[0], value != 0);
                return 0U;
            }
            require_frame("glTexParameteri")
                .TextureParameter(
                    arguments[0], arguments[1],
                    std::bit_cast<std::int32_t>(arguments[2]));
            return 0U;
        });
    dispatch.Bind(
        "glViewport",
        [&state, supersample_factor, require_frame](
            const std::span<const std::uint32_t> arguments, const std::uint64_t) {
            const std::array logical{
                std::bit_cast<std::int32_t>(arguments[0]),
                std::bit_cast<std::int32_t>(arguments[1]),
                std::bit_cast<std::int32_t>(arguments[2]),
                std::bit_cast<std::int32_t>(arguments[3])};
            require_frame("glViewport").Viewport(ScaleAndroidBoundaryViewportComponent(
                              std::bit_cast<std::int32_t>(arguments[0]),
                              supersample_factor),
                          ScaleAndroidBoundaryViewportComponent(
                              std::bit_cast<std::int32_t>(arguments[1]),
                              supersample_factor),
                          ScaleAndroidBoundaryViewportComponent(
                              std::bit_cast<std::int32_t>(arguments[2]),
                              supersample_factor),
                          ScaleAndroidBoundaryViewportComponent(
                              std::bit_cast<std::int32_t>(arguments[3]), supersample_factor));
            state.Shared().SetViewport(logical);
            return 0U;
        });
    dispatch.Bind(
        "glScissor",
        [&state, supersample_factor, require_frame](
            const std::span<const std::uint32_t> arguments, const std::uint64_t) {
            const std::array logical{
                std::bit_cast<std::int32_t>(arguments[0]),
                std::bit_cast<std::int32_t>(arguments[1]),
                std::bit_cast<std::int32_t>(arguments[2]),
                std::bit_cast<std::int32_t>(arguments[3])};
            require_frame("glScissor").Scissor(ScaleAndroidBoundaryViewportComponent(
                             std::bit_cast<std::int32_t>(arguments[0]),
                             supersample_factor),
                         ScaleAndroidBoundaryViewportComponent(
                             std::bit_cast<std::int32_t>(arguments[1]),
                             supersample_factor),
                         ScaleAndroidBoundaryViewportComponent(
                             std::bit_cast<std::int32_t>(arguments[2]),
                             supersample_factor),
                         ScaleAndroidBoundaryViewportComponent(
                             std::bit_cast<std::int32_t>(arguments[3]), supersample_factor));
            state.Shared().SetScissor(logical);
            return 0U;
        });
    dispatch.Bind("glClear", [require_frame](const auto arguments, const auto) {
        require_frame("glClear").Clear(arguments[0]);
        return 0U;
    });
    dispatch.Bind(
        "glClearColor",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments, const std::uint64_t) {
            const std::array color{std::bit_cast<float>(arguments[0]),
                                   std::bit_cast<float>(arguments[1]),
                                   std::bit_cast<float>(arguments[2]),
                                   std::bit_cast<float>(arguments[3])};
            require_frame("glClearColor").ClearColor(
                color[0], color[1], color[2], color[3]);
            state.Shared().SetClearColor(color);
            return 0U;
        });
    dispatch.Bind(
        "glClearDepthf",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments, const std::uint64_t) {
            const auto depth = std::bit_cast<float>(arguments[0]);
            require_frame("glClearDepthf").ClearDepth(depth);
            state.Shared().SetClearDepth(depth);
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

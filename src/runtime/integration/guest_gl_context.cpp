#include "ogplay/runtime/integration/guest_gl_context.h"

#include <algorithm>
#include <stdexcept>

namespace ogplay::runtime {

namespace {

constexpr std::uint32_t kTexture0 = 0x84C0U;
constexpr std::uint32_t kTexture31 = 0x84DFU;
constexpr std::uint32_t kTexture2d = 0x0DE1U;
constexpr std::uint32_t kTextureCubeMap = 0x8513U;

[[nodiscard]] std::uint32_t TextureBindingTargetForMetadata(
    const std::uint32_t target) {
    if (target >= 0x8515U && target <= 0x851AU) {
        return kTextureCubeMap;
    }
    if (target == kTexture2d || target == kTextureCubeMap) return target;
    throw std::invalid_argument("shared GL texture metadata target is invalid");
}

void RequireFramebufferTarget(const std::uint32_t target) {
    if (target != 0x8D40U) {
        throw std::invalid_argument("shared GL framebuffer target is invalid");
    }
}

void RequireRenderbufferTarget(const std::uint32_t target) {
    if (target != 0x8D41U) {
        throw std::invalid_argument("shared GL renderbuffer target is invalid");
    }
}

void RequireCommonCapability(const std::uint32_t capability) {
    switch (capability) {
    case 0x0BE2U: case 0x0B44U: case 0x0B71U: case 0x0BD0U:
    case 0x8037U: case 0x809EU: case 0x80A0U: case 0x0C11U:
    case 0x0B90U:
        return;
    default:
        throw std::invalid_argument("shared GL capability is invalid");
    }
}

}  // namespace

void SharedGlState::ValidateActiveTexture(const std::uint32_t texture) const {
    if (texture < kTexture0 || texture > kTexture31) {
        throw std::invalid_argument("shared GL texture unit is outside GL_TEXTURE0..31");
    }
}

void SharedGlState::SetActiveTexture(const std::uint32_t texture) {
    ValidateActiveTexture(texture);
    active_texture = texture;
}

void SharedGlState::ValidateTextureTarget(const std::uint32_t target) const {
    if (target != kTexture2d && target != kTextureCubeMap) {
        throw std::invalid_argument("shared GL texture target is invalid");
    }
}

void SharedGlState::BindTexture(const std::uint32_t target,
                                const std::uint32_t texture) {
    ValidateTextureTarget(target);
    bound_textures_[{active_texture, target}] = texture;
}

std::uint32_t SharedGlState::BoundTexture(const std::uint32_t target) const {
    return BoundTexture(active_texture, target);
}

std::uint32_t SharedGlState::BoundTexture(
    const std::uint32_t texture_unit, const std::uint32_t target) const {
    ValidateTextureTarget(target);
    if (texture_unit < kTexture0 || texture_unit > kTexture31) {
        throw std::invalid_argument("shared GL texture unit is outside GL_TEXTURE0..31");
    }
    const auto found = bound_textures_.find({texture_unit, target});
    return found == bound_textures_.end() ? 0U : found->second;
}

void SharedGlState::DeleteTextures(
    const std::span<const std::uint32_t> textures) noexcept {
    for (const auto texture : textures) {
        std::erase_if(generate_mipmap_, [texture](const auto& entry) {
            return entry.first.texture == texture;
        });
        std::erase_if(texture_base_formats_, [texture](const auto& entry) {
            return entry.first.texture == texture;
        });
        for (auto& [binding, bound] : bound_textures_) {
            static_cast<void>(binding);
            if (bound == texture) bound = 0U;
        }
    }
}

void SharedGlState::SetTextureBaseFormat(const std::uint32_t target,
                                         const std::uint32_t format) {
    const auto binding_target = TextureBindingTargetForMetadata(target);
    texture_base_formats_[{BoundTexture(binding_target), target}] = format;
}

std::optional<std::uint32_t> SharedGlState::TextureBaseFormat(
    const std::uint32_t target) const {
    return TextureBaseFormat(active_texture, target);
}

std::optional<std::uint32_t> SharedGlState::TextureBaseFormat(
    const std::uint32_t texture_unit, const std::uint32_t target) const {
    const auto binding_target = TextureBindingTargetForMetadata(target);
    const auto found = texture_base_formats_.find(
        {BoundTexture(texture_unit, binding_target), target});
    if (found == texture_base_formats_.end()) return std::nullopt;
    return found->second;
}

void SharedGlState::SetGenerateMipmap(const std::uint32_t target,
                                      const bool enabled) {
    const auto binding_target = TextureBindingTargetForMetadata(target);
    generate_mipmap_[{BoundTexture(binding_target), target}] = enabled;
}

bool SharedGlState::GenerateMipmapEnabled(const std::uint32_t target) const {
    const auto binding_target = TextureBindingTargetForMetadata(target);
    const auto found = generate_mipmap_.find(
        {BoundTexture(binding_target), target});
    return found != generate_mipmap_.end() && found->second;
}

void SharedGlState::ValidateFramebufferTarget(
    const std::uint32_t target) const {
    RequireFramebufferTarget(target);
}

void SharedGlState::BindFramebuffer(const std::uint32_t target,
                                    const std::uint32_t framebuffer) {
    ValidateFramebufferTarget(target);
    framebuffer_ = framebuffer;
}

void SharedGlState::ValidateRenderbufferTarget(
    const std::uint32_t target) const {
    RequireRenderbufferTarget(target);
}

void SharedGlState::BindRenderbuffer(const std::uint32_t target,
                                     const std::uint32_t renderbuffer) {
    ValidateRenderbufferTarget(target);
    renderbuffer_ = renderbuffer;
}

void SharedGlState::DeleteFramebuffers(
    const std::span<const std::uint32_t> framebuffers) noexcept {
    if (std::ranges::find(framebuffers, framebuffer_) != framebuffers.end()) {
        framebuffer_ = 0U;
    }
}

void SharedGlState::DeleteRenderbuffers(
    const std::span<const std::uint32_t> renderbuffers) noexcept {
    if (std::ranges::find(renderbuffers, renderbuffer_) != renderbuffers.end()) {
        renderbuffer_ = 0U;
    }
}

std::uint32_t SharedGlState::Framebuffer() const noexcept { return framebuffer_; }
std::uint32_t SharedGlState::Renderbuffer() const noexcept { return renderbuffer_; }

void SharedGlState::SetViewport(
    const std::array<std::int32_t, 4> viewport) noexcept {
    viewport_ = viewport;
}

void SharedGlState::SetScissor(
    const std::array<std::int32_t, 4> scissor) noexcept {
    scissor_ = scissor;
}

const std::array<std::int32_t, 4>& SharedGlState::Viewport() const noexcept {
    return viewport_;
}

const std::array<std::int32_t, 4>& SharedGlState::Scissor() const noexcept {
    return scissor_;
}

void SharedGlState::SetClearColor(const std::array<float, 4> color) noexcept {
    clear_color_ = color;
}

void SharedGlState::SetClearDepth(const float depth) noexcept {
    clear_depth_ = depth;
}

void SharedGlState::SetClearStencil(const std::int32_t stencil) noexcept {
    clear_stencil_ = stencil;
}

const std::array<float, 4>& SharedGlState::ClearColor() const noexcept {
    return clear_color_;
}

float SharedGlState::ClearDepth() const noexcept { return clear_depth_; }
std::int32_t SharedGlState::ClearStencil() const noexcept { return clear_stencil_; }

void SharedGlState::ValidateCapability(
    const std::uint32_t capability) const {
    RequireCommonCapability(capability);
}

void SharedGlState::SetCapability(const std::uint32_t capability,
                                  const bool enabled) {
    ValidateCapability(capability);
    capabilities_[capability] = enabled;
}

bool SharedGlState::Capability(const std::uint32_t capability) const {
    ValidateCapability(capability);
    const auto found = capabilities_.find(capability);
    return found != capabilities_.end() && found->second;
}

void SharedGlState::SetCurrentProgram(const std::uint32_t program) noexcept {
    current_program_ = program;
}

std::uint32_t SharedGlState::CurrentProgram() const noexcept {
    return current_program_;
}

const std::map<TextureBindingKey, std::uint32_t>&
SharedGlState::TextureBindings() const noexcept {
    return bound_textures_;
}

void SharedGlState::Reset() noexcept {
    transfer = {};
    active_texture = 0x84C0U;
    bound_textures_.clear();
    texture_base_formats_.clear();
    generate_mipmap_.clear();
    framebuffer_ = 0U;
    renderbuffer_ = 0U;
    viewport_ = {};
    scissor_ = {};
    clear_color_ = {};
    clear_depth_ = 1.0F;
    clear_stencil_ = 0;
    capabilities_.clear();
    current_program_ = 0U;
}

void NativeGlState::BeginFixedDraw() {
    if (fixed_draw_active_) {
        throw std::logic_error("nested GLES1 fixed draw native transaction");
    }
    fixed_draw_active_ = true;
}

void NativeGlState::EndFixedDraw() {
    if (!fixed_draw_active_) {
        throw std::logic_error("GLES1 fixed draw native transaction is not active");
    }
    fixed_draw_active_ = false;
}

bool NativeGlState::FixedDrawActive() const noexcept {
    return fixed_draw_active_;
}

void NativeGlState::Reset() noexcept { fixed_draw_active_ = false; }

GuestGlContext::GuestGlContext(const GuestGlContextId id) : id_(id) {
    if (id_ == 0U) {
        throw std::invalid_argument("guest GL context identity must not be zero");
    }
}

GuestGlContextId GuestGlContext::Id() const noexcept { return id_; }

SharedGlState& GuestGlContext::Shared() noexcept { return shared_; }

const SharedGlState& GuestGlContext::Shared() const noexcept { return shared_; }

NativeGlState& GuestGlContext::Native() noexcept { return native_; }

const NativeGlState& GuestGlContext::Native() const noexcept { return native_; }

GuestGlRenderer GuestGlContext::SelectDrawRenderer(
    const bool fixed_vertex_array_enabled,
    const bool programmable_attribute_enabled) const noexcept {
    if (!fixed_vertex_array_enabled && programmable_attribute_enabled &&
        shared_.CurrentProgram() != 0U) {
        return GuestGlRenderer::programmable;
    }
    return GuestGlRenderer::fixed_function;
}

void GuestGlContext::Reset() noexcept {
    shared_.Reset();
    native_.Reset();
}

}  // namespace ogplay::runtime

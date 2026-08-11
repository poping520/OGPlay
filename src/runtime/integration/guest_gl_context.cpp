#include "ogplay/runtime/integration/guest_gl_context.h"

#include <stdexcept>
#include <algorithm>

namespace ogplay::runtime {

namespace {

constexpr std::uint32_t kTexture0 = 0x84C0U;
constexpr std::uint32_t kTexture31 = 0x84DFU;
constexpr std::uint32_t kTexture2d = 0x0DE1U;

void ValidateTextureTarget(const std::uint32_t target) {
    if (target != kTexture2d) {
        throw std::invalid_argument("shared GL texture target must be GL_TEXTURE_2D");
    }
}

}  // namespace

void SharedGlState::SetActiveTexture(const std::uint32_t texture) {
    if (texture < kTexture0 || texture > kTexture31) {
        throw std::invalid_argument("shared GL texture unit is outside GL_TEXTURE0..31");
    }
    active_texture = texture;
}

void SharedGlState::BindTexture(const std::uint32_t target,
                                const std::uint32_t texture) {
    ValidateTextureTarget(target);
    bound_textures_[active_texture] = texture;
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
    const auto found = bound_textures_.find(texture_unit);
    return found == bound_textures_.end() ? 0U : found->second;
}

void SharedGlState::DeleteTextures(
    const std::span<const std::uint32_t> textures) noexcept {
    for (const auto texture : textures) {
        generate_mipmap_.erase(texture);
        texture_base_formats_.erase(texture);
        for (auto& [unit, bound] : bound_textures_) {
            static_cast<void>(unit);
            if (bound == texture) bound = 0U;
        }
    }
}

void SharedGlState::SetTextureBaseFormat(const std::uint32_t target,
                                         const std::uint32_t format) {
    texture_base_formats_[BoundTexture(target)] = format;
}

std::optional<std::uint32_t> SharedGlState::TextureBaseFormat(
    const std::uint32_t target) const {
    return TextureBaseFormat(active_texture, target);
}

std::optional<std::uint32_t> SharedGlState::TextureBaseFormat(
    const std::uint32_t texture_unit, const std::uint32_t target) const {
    const auto found = texture_base_formats_.find(
        BoundTexture(texture_unit, target));
    if (found == texture_base_formats_.end()) return std::nullopt;
    return found->second;
}

void SharedGlState::SetGenerateMipmap(const std::uint32_t target,
                                      const bool enabled) {
    generate_mipmap_[BoundTexture(target)] = enabled;
}

bool SharedGlState::GenerateMipmapEnabled(const std::uint32_t target) const {
    const auto found = generate_mipmap_.find(BoundTexture(target));
    return found != generate_mipmap_.end() && found->second;
}

void SharedGlState::Reset() noexcept {
    transfer = {};
    active_texture = 0x84C0U;
    bound_textures_.clear();
    texture_base_formats_.clear();
    generate_mipmap_.clear();
}

GuestGlContext::GuestGlContext(const GuestGlContextId id) : id_(id) {
    if (id_ == 0U) {
        throw std::invalid_argument("guest GL context identity must not be zero");
    }
}

GuestGlContextId GuestGlContext::Id() const noexcept { return id_; }

SharedGlState& GuestGlContext::Shared() noexcept { return shared_; }

const SharedGlState& GuestGlContext::Shared() const noexcept { return shared_; }

void GuestGlContext::Reset() noexcept { shared_.Reset(); }

}  // namespace ogplay::runtime

#include "android_boundary_gles1_query.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ogplay/gles/guest_transfer.h"
#include "ogplay/memory/address_space.h"

namespace ogplay::runtime::detail {
namespace {

constexpr memory::GuestAddress kGles1QueryStringRegion{0x70010000U};
constexpr std::uint32_t kQueryStringSlotBytes = 16U * 1024U;
constexpr std::uint32_t kQueryStringRegionBytes = kQueryStringSlotBytes * 4U;
constexpr std::uint32_t kTexture0 = 0x84C0U;
constexpr std::uint32_t kTexture31 = 0x84DFU;

[[nodiscard]] std::uint64_t TextureEnvironmentKey(
    const std::uint32_t texture, const std::uint32_t pname) noexcept {
    return (static_cast<std::uint64_t>(texture) << 32U) | pname;
}

void RequireTextureUnit(const std::uint32_t texture) {
    if (texture < kTexture0 || texture > kTexture31) {
        throw std::invalid_argument("GLES1 texture unit is outside GL_TEXTURE0..31");
    }
}

void RequireTextureEnvironmentMode(const float value) {
    constexpr std::array modes{0x0104U, 0x0BE2U, 0x1E01U,
                               0x2100U, 0x2101U, 0x8570U};
    if (!std::isfinite(value) || value < 0.0F ||
        value != std::floor(value) ||
        std::ranges::find(modes, static_cast<std::uint32_t>(value)) ==
            modes.end()) {
        throw std::invalid_argument("GLES1 texture environment mode is invalid");
    }
}

[[nodiscard]] std::vector<float> ReadGuestFloats(
    const memory::AddressSpace& address_space, const std::uint32_t address,
    const std::size_t count, const std::uint64_t thread_id) {
    std::vector<std::byte> bytes(count * sizeof(std::uint32_t));
    address_space.Read(memory::GuestAddress{address}, bytes, thread_id);
    std::vector<float> values(count);
    for (std::size_t index = 0; index < count; ++index) {
        std::uint32_t word{};
        for (std::size_t byte = 0; byte < sizeof(word); ++byte) {
            word |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(
                        bytes[index * sizeof(word) + byte]))
                    << (byte * 8U);
        }
        values[index] = std::bit_cast<float>(word);
    }
    return values;
}

void WriteGuestFloats(gles::GuestBuffer& output,
                      const std::span<const float> values) {
    auto bytes = output.WritableBytes();
    if (bytes.size() != values.size() * sizeof(std::uint32_t)) {
        throw std::logic_error("GLES1 float query output size differs");
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto word = std::bit_cast<std::uint32_t>(values[index]);
        for (std::size_t byte = 0; byte < sizeof(word); ++byte) {
            bytes[index * sizeof(word) + byte] =
                static_cast<std::byte>(word >> (byte * 8U));
        }
    }
    output.Commit();
}

[[nodiscard]] std::int32_t SignedTextureValue(
    const std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}

[[nodiscard]] gles::GuestBuffer PrepareTexturePixels(
    memory::AddressSpace& address_space,
    const AndroidBoundaryGles1State& state,
    const std::string_view function_name,
    const std::span<const std::uint32_t> arguments,
    const std::size_t pointer_index, const bool nullable,
    const std::uint64_t thread_id) {
    const auto resolution = state.TransferState().Resolve(
        {.function_name = function_name,
         .parameter_name = "pixels",
         .expression = "pixel_bytes(width,height,format,type)",
         .arguments = arguments});
    if (!resolution.has_value() ||
        resolution->disposition != gles::GlesLengthDisposition::transfer) {
        throw std::logic_error("GLES1 pixel transfer did not resolve to guest bytes");
    }
    return gles::GuestBuffer::Prepare(
        address_space, memory::GuestAddress{arguments[pointer_index]},
        resolution->element_count, gles::GuestTransferDirection::input,
        nullable, thread_id);
}

void GenerateAutomaticMipmap(AndroidBoundaryGles1State& state,
                             gles::AngleFrame& frame,
                             const std::uint32_t target,
                             const std::int32_t level) {
    if (level == 0 && state.GenerateMipmapEnabled(target)) {
        frame.GenerateMipmap(target);
    }
}

[[nodiscard]] std::uint32_t QueryStringOffset(const std::uint32_t parameter) {
    switch (parameter) {
    case 0x1F00U: return 0U;                              // GL_VENDOR
    case 0x1F01U: return kQueryStringSlotBytes;           // GL_RENDERER
    case 0x1F02U: return kQueryStringSlotBytes * 2U;      // GL_VERSION
    case 0x1F03U: return kQueryStringSlotBytes * 3U;      // GL_EXTENSIONS
    default: throw std::invalid_argument("GLES1 string query is unsupported");
    }
}

}  // namespace

AndroidBoundaryGles1QueryStrings::AndroidBoundaryGles1QueryStrings(
    memory::AddressSpace& address_space)
    : address_space_(&address_space) {}

void AndroidBoundaryGles1QueryStrings::Validate(
    const std::uint32_t parameter) const {
    static_cast<void>(QueryStringOffset(parameter));
}

std::uint32_t AndroidBoundaryGles1QueryStrings::Publish(
    const std::uint32_t parameter, const std::string_view value,
    const std::uint64_t thread_id) {
    const auto offset = QueryStringOffset(parameter);
    if (value.size() >= kQueryStringSlotBytes) {
        throw std::length_error("ANGLE GLES1 query string exceeds its guest slot");
    }
    if (!region_mapped_) {
        address_space_->Map({kGles1QueryStringRegion, kQueryStringRegionBytes},
                            memory::PageProtection::read |
                                memory::PageProtection::write);
        address_space_->Protect(
            {kGles1QueryStringRegion, kQueryStringRegionBytes},
            memory::PageProtection::read);
        region_mapped_ = true;
    }
    std::vector<std::byte> bytes;
    bytes.reserve(value.size() + 1U);
    for (const auto character : value) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    bytes.push_back(std::byte{});
    const memory::GuestRange region{kGles1QueryStringRegion,
                                    kQueryStringRegionBytes};
    address_space_->Protect(region, memory::PageProtection::read |
                                        memory::PageProtection::write);
    try {
        address_space_->Write(kGles1QueryStringRegion.Add(offset), bytes,
                              thread_id);
    } catch (...) {
        address_space_->Protect(region, memory::PageProtection::read);
        throw;
    }
    address_space_->Protect(region, memory::PageProtection::read);
    return kGles1QueryStringRegion.Add(offset).Value();
}

AndroidBoundaryGles1LegacyState::AndroidBoundaryGles1LegacyState() { Reset(); }

void AndroidBoundaryGles1LegacyState::Reset() {
    alpha_function_ = 0x0207U;  // GL_ALWAYS
    alpha_reference_ = 0.0F;
    client_active_texture_ = kTexture0;
    color_ = {1.0F, 1.0F, 1.0F, 1.0F};
    texture_environment_.clear();
    for (auto texture = kTexture0; texture <= kTexture31; ++texture) {
        texture_environment_[TextureEnvironmentKey(
            texture, kGles1TextureEnvironmentMode)] = {8448.0F};
        texture_environment_[TextureEnvironmentKey(
            texture, kGles1TextureEnvironmentColor)] =
            {0.0F, 0.0F, 0.0F, 0.0F};
    }
}

void AndroidBoundaryGles1LegacyState::SetAlphaFunction(
    const std::uint32_t function, const float reference) {
    if (function < 0x0200U || function > 0x0207U) {
        throw std::invalid_argument("GLES1 alpha function is invalid");
    }
    if (!std::isfinite(reference)) {
        throw std::invalid_argument("GLES1 alpha reference must be finite");
    }
    alpha_function_ = function;
    alpha_reference_ = std::clamp(reference, 0.0F, 1.0F);
}

void AndroidBoundaryGles1LegacyState::SetClientActiveTexture(
    const std::uint32_t texture) {
    RequireTextureUnit(texture);
    client_active_texture_ = texture;
}

void AndroidBoundaryGles1LegacyState::SetColor(
    const std::span<const float, 4> color) {
    if (!std::ranges::all_of(color, [](const float value) {
            return std::isfinite(value);
        })) {
        throw std::invalid_argument("GLES1 current color must be finite");
    }
    std::ranges::transform(color, color_.begin(), [](const float value) {
        return std::clamp(value, 0.0F, 1.0F);
    });
}

void AndroidBoundaryGles1LegacyState::SetTextureEnvironment(
    const std::uint32_t texture, const std::uint32_t target,
    const std::uint32_t pname, const std::span<const float> values) {
    RequireTextureUnit(texture);
    if (target != kGles1TextureEnvironment) {
        throw std::invalid_argument("GLES1 texture environment target is invalid");
    }
    if (pname == kGles1TextureEnvironmentMode) {
        if (values.size() != 1U) {
            throw std::invalid_argument("GLES1 texture environment mode requires one value");
        }
        RequireTextureEnvironmentMode(values.front());
    } else if (pname == kGles1TextureEnvironmentColor) {
        if (values.size() != 4U ||
            !std::ranges::all_of(values, [](const float value) {
                return std::isfinite(value);
            })) {
            throw std::invalid_argument("GLES1 texture environment color is invalid");
        }
    } else {
        throw std::invalid_argument("GLES1 texture environment pname is unsupported");
    }
    auto stored = std::vector<float>(values.begin(), values.end());
    if (pname == kGles1TextureEnvironmentColor) {
        std::ranges::transform(stored, stored.begin(), [](const float value) {
            return std::clamp(value, 0.0F, 1.0F);
        });
    }
    texture_environment_[TextureEnvironmentKey(texture, pname)] =
        std::move(stored);
}

std::uint32_t AndroidBoundaryGles1LegacyState::AlphaFunction() const noexcept {
    return alpha_function_;
}

float AndroidBoundaryGles1LegacyState::AlphaReference() const noexcept {
    return alpha_reference_;
}

std::uint32_t AndroidBoundaryGles1LegacyState::ClientActiveTexture() const noexcept {
    return client_active_texture_;
}

const std::array<float, 4>& AndroidBoundaryGles1LegacyState::Color() const noexcept {
    return color_;
}

const std::vector<float>& AndroidBoundaryGles1LegacyState::TextureEnvironment(
    const std::uint32_t texture, const std::uint32_t pname) const {
    RequireTextureUnit(texture);
    return texture_environment_.at(TextureEnvironmentKey(texture, pname));
}

void BindAndroidBoundaryGles1Queries(
    gles::GlesDispatchTable& dispatch,
    AndroidBoundaryGles1QueryStrings& strings,
    AndroidBoundaryGles1StringResolver resolve_string) {
    if (!resolve_string) {
        throw std::invalid_argument("GLES1 string resolver is missing");
    }
    dispatch.Bind(
        "glGetString",
        [&strings, resolve_string = std::move(resolve_string)](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            strings.Validate(arguments[0]);
            return strings.Publish(arguments[0], resolve_string(arguments[0]),
                                   thread_id);
        });
}

void BindAndroidBoundaryGles1Legacy(
    gles::GlesDispatchTable& dispatch,
    AndroidBoundaryGles1LegacyState& legacy,
    AndroidBoundaryGles1State& core,
    memory::AddressSpace& address_space,
    AndroidBoundaryFrameResolver require_frame) {
    if (!require_frame) {
        throw std::invalid_argument("GLES1 legacy state frame resolver is missing");
    }
    dispatch.Bind(
        "glAlphaFunc",
        [&legacy, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            auto next = legacy;
            next.SetAlphaFunction(arguments[0],
                                  std::bit_cast<float>(arguments[1]));
            static_cast<void>(require_frame("glAlphaFunc"));
            legacy = std::move(next);
            return 0U;
        });
    dispatch.Bind(
        "glClientActiveTexture",
        [&legacy, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            auto next = legacy;
            next.SetClientActiveTexture(arguments[0]);
            static_cast<void>(require_frame("glClientActiveTexture"));
            legacy = std::move(next);
            return 0U;
        });
    dispatch.Bind(
        "glColor4f",
        [&legacy, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            const std::array color{
                std::bit_cast<float>(arguments[0]),
                std::bit_cast<float>(arguments[1]),
                std::bit_cast<float>(arguments[2]),
                std::bit_cast<float>(arguments[3])};
            auto next = legacy;
            next.SetColor(color);
            static_cast<void>(require_frame("glColor4f"));
            legacy = std::move(next);
            return 0U;
        });
    dispatch.Bind(
        "glColor4ub",
        [&legacy, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            constexpr float kMaximum = 255.0F;
            const std::array color{
                static_cast<float>(arguments[0] & 0xFFU) / kMaximum,
                static_cast<float>(arguments[1] & 0xFFU) / kMaximum,
                static_cast<float>(arguments[2] & 0xFFU) / kMaximum,
                static_cast<float>(arguments[3] & 0xFFU) / kMaximum};
            auto next = legacy;
            next.SetColor(color);
            static_cast<void>(require_frame("glColor4ub"));
            legacy = std::move(next);
            return 0U;
        });
    dispatch.Bind(
        "glTexEnvfv",
        [&legacy, &core, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            const auto count = arguments[1] == kGles1TextureEnvironmentMode
                                   ? 1U
                                   : arguments[1] == kGles1TextureEnvironmentColor
                                         ? 4U
                                         : 0U;
            if (count == 0U) {
                throw std::invalid_argument(
                    "GLES1 texture environment pname is unsupported");
            }
            const auto values = ReadGuestFloats(
                address_space, arguments[2], count, thread_id);
            auto next = legacy;
            next.SetTextureEnvironment(core.ActiveTexture(), arguments[0],
                                       arguments[1], values);
            static_cast<void>(require_frame("glTexEnvfv"));
            legacy = std::move(next);
            return 0U;
        });
    dispatch.Bind(
        "glTexEnvi",
        [&legacy, &core, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            const std::array values{
                static_cast<float>(std::bit_cast<std::int32_t>(arguments[2]))};
            auto next = legacy;
            next.SetTextureEnvironment(core.ActiveTexture(), arguments[0],
                                       arguments[1], values);
            static_cast<void>(require_frame("glTexEnvi"));
            legacy = std::move(next);
            return 0U;
        });
    dispatch.Bind(
        "glGetFloatv",
        [&legacy, &core, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            std::size_t count{};
            switch (arguments[0]) {
            case 0x0BA6U:
            case 0x0BA7U:
            case 0x0BA8U: count = 16U; break;
            case 0x0B00U: count = 4U; break;
            case 0x0BC1U:
            case 0x0BC2U:
            case 0x84E1U:
            case kGles1MaxTextureAnisotropy: count = 1U; break;
            default:
                throw std::invalid_argument("GLES1 float query is unsupported");
            }
            auto output = gles::GuestBuffer::Prepare(
                address_space, memory::GuestAddress{arguments[1]},
                count * sizeof(std::uint32_t),
                gles::GuestTransferDirection::output, false, thread_id);
            std::vector<float> values;
            if (arguments[0] == kGles1MaxTextureAnisotropy) {
                const auto queried = require_frame("glGetFloatv")
                                         .GetIntegers(arguments[0], 1U);
                values = {static_cast<float>(queried.front())};
            } else {
                static_cast<void>(require_frame("glGetFloatv"));
                switch (arguments[0]) {
                case 0x0BA6U:
                case 0x0BA7U:
                case 0x0BA8U: {
                    auto matrices = core.Matrices();
                    matrices.SetMode(arguments[0] == 0x0BA6U
                                         ? kGles1Modelview
                                         : arguments[0] == 0x0BA7U
                                               ? kGles1Projection
                                               : kGles1Texture);
                    values.assign(matrices.Current().begin(),
                                  matrices.Current().end());
                    break;
                }
                case 0x0B00U:
                    values.assign(legacy.Color().begin(), legacy.Color().end());
                    break;
                case 0x0BC1U:
                    values = {static_cast<float>(legacy.AlphaFunction())};
                    break;
                case 0x0BC2U: values = {legacy.AlphaReference()}; break;
                case 0x84E1U:
                    values = {static_cast<float>(legacy.ClientActiveTexture())};
                    break;
                default: break;
                }
            }
            WriteGuestFloats(output, values);
            return 0U;
        });
}

void BindAndroidBoundaryGles1Textures(
    gles::GlesDispatchTable& dispatch, AndroidBoundaryGles1State& state,
    memory::AddressSpace& address_space,
    AndroidBoundaryFrameResolver require_frame) {
    if (!require_frame) {
        throw std::invalid_argument("GLES1 texture frame resolver is missing");
    }
    dispatch.Bind(
        "glCompressedTexImage2D",
        [&state, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            const auto image_size = SignedTextureValue(arguments[6]);
            if (image_size < 0) {
                throw std::invalid_argument("GLES1 compressed image size is negative");
            }
            auto data = gles::GuestBuffer::Prepare(
                address_space, memory::GuestAddress{arguments[7]},
                static_cast<std::uint32_t>(image_size),
                gles::GuestTransferDirection::input, false, thread_id);
            auto& frame = require_frame("glCompressedTexImage2D");
            frame.CompressedTextureImage2D(
                arguments[0], SignedTextureValue(arguments[1]), arguments[2],
                SignedTextureValue(arguments[3]), SignedTextureValue(arguments[4]),
                SignedTextureValue(arguments[5]), data.Bytes());
            GenerateAutomaticMipmap(state, frame, arguments[0],
                                    SignedTextureValue(arguments[1]));
            return 0U;
        });
    dispatch.Bind(
        "glCopyTexImage2D",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            auto& frame = require_frame("glCopyTexImage2D");
            frame.CopyTextureImage2D(
                arguments[0], SignedTextureValue(arguments[1]), arguments[2],
                SignedTextureValue(arguments[3]), SignedTextureValue(arguments[4]),
                SignedTextureValue(arguments[5]), SignedTextureValue(arguments[6]),
                SignedTextureValue(arguments[7]));
            GenerateAutomaticMipmap(state, frame, arguments[0],
                                    SignedTextureValue(arguments[1]));
            return 0U;
        });
    dispatch.Bind(
        "glTexImage2D",
        [&state, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            auto pixels = PrepareTexturePixels(
                address_space, state, "glTexImage2D", arguments, 8U, true,
                thread_id);
            auto& frame = require_frame("glTexImage2D");
            frame.TextureImage2D(
                arguments[0], SignedTextureValue(arguments[1]),
                SignedTextureValue(arguments[2]), SignedTextureValue(arguments[3]),
                SignedTextureValue(arguments[4]), SignedTextureValue(arguments[5]),
                arguments[6], arguments[7],
                pixels.IsNull()
                    ? std::nullopt
                    : std::optional<std::span<const std::byte>>(pixels.Bytes()));
            GenerateAutomaticMipmap(state, frame, arguments[0],
                                    SignedTextureValue(arguments[1]));
            return 0U;
        });
    dispatch.Bind(
        "glTexSubImage2D",
        [&state, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            auto pixels = PrepareTexturePixels(
                address_space, state, "glTexSubImage2D", arguments, 8U, false,
                thread_id);
            auto& frame = require_frame("glTexSubImage2D");
            frame.TextureSubImage2D(
                arguments[0], SignedTextureValue(arguments[1]),
                SignedTextureValue(arguments[2]), SignedTextureValue(arguments[3]),
                SignedTextureValue(arguments[4]), SignedTextureValue(arguments[5]),
                arguments[6], arguments[7], pixels.Bytes());
            GenerateAutomaticMipmap(state, frame, arguments[0],
                                    SignedTextureValue(arguments[1]));
            return 0U;
        });
}

}  // namespace ogplay::runtime::detail

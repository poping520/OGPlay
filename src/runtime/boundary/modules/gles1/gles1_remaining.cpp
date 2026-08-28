#include "gles1_remaining.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

#include "gles1_fixed.h"
#include "ogplay/gles/guest_transfer.h"
#include "ogplay/memory/address_space.h"
#include "runtime/boundary/services/gles_transfer_io.h"

namespace ogplay::runtime::detail {
namespace {

constexpr float kFixedScale = 65536.0F;
constexpr std::uint32_t kTexture0 = 0x84C0U;
constexpr std::uint32_t kGenerateMipmap = 0x8191U;

[[nodiscard]] std::int32_t Signed(const std::uint32_t word) noexcept {
    return std::bit_cast<std::int32_t>(word);
}

[[nodiscard]] float Fixed(const std::uint32_t word) noexcept {
    return static_cast<float>(Signed(word)) / kFixedScale;
}

[[nodiscard]] std::int32_t ToFixed(const float value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("GLES1 fixed query value is not finite");
    }
    const auto scaled = static_cast<double>(value) * kFixedScale;
    return static_cast<std::int32_t>(std::clamp(
        std::round(scaled), static_cast<double>((std::numeric_limits<std::int32_t>::min)()),
        static_cast<double>((std::numeric_limits<std::int32_t>::max)())));
}

[[nodiscard]] std::vector<std::uint32_t> ReadWords(const memory::AddressSpace& address_space,
                                                   const std::uint32_t address,
                                                   const std::size_t count,
                                                   const std::uint64_t thread_id) {
    return gles_io::LoadGuestWordsLE(address_space, address, count, thread_id);
}

template <typename T, std::size_t Extent>
void WriteWords(memory::AddressSpace& address_space, const std::uint32_t address,
                const std::span<const T, Extent> values, const std::uint64_t thread_id) {
    auto output = gles::GuestBuffer::Prepare(
        address_space, memory::GuestAddress{address}, values.size() * sizeof(std::uint32_t),
        gles::GuestTransferDirection::output, false, thread_id);
    auto bytes = output.WritableBytes();
    std::vector<std::uint32_t> words;
    words.reserve(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        if constexpr (std::is_same_v<T, float>) {
            words.push_back(std::bit_cast<std::uint32_t>(values[index]));
        } else {
            words.push_back(std::bit_cast<std::uint32_t>(
                static_cast<std::int32_t>(values[index])));
        }
    }
    gles_io::StoreWordsLE(bytes, words);
    output.Commit();
}

[[nodiscard]] bool TextureEnvironmentEnum(const std::uint32_t pname) noexcept {
    return pname == kGles1TextureEnvironmentMode || pname == kGles1CombineRgb ||
           pname == kGles1CombineAlpha ||
           (pname >= kGles1Source0Rgb && pname <= kGles1Source2Rgb) ||
           (pname >= kGles1Source0Alpha && pname <= kGles1Source2Alpha) ||
           (pname >= kGles1Operand0Rgb && pname <= kGles1Operand2Rgb) ||
           (pname >= kGles1Operand0Alpha && pname <= kGles1Operand2Alpha);
}

[[nodiscard]] std::size_t TextureEnvironmentCount(const std::uint32_t pname) {
    if (pname == kGles1TextureEnvironmentColor)
        return 4U;
    if (TextureEnvironmentEnum(pname) || pname == kGles1RgbScale || pname == kGles1AlphaScale)
        return 1U;
    throw std::invalid_argument("GLES1 texture environment pname is unsupported");
}

[[nodiscard]] bool TextureParameterEnum(const std::uint32_t pname) noexcept {
    return pname == 0x2800U || pname == 0x2801U || pname == 0x2802U || pname == 0x2803U ||
           pname == kGenerateMipmap;
}

[[nodiscard]] std::vector<float>
ConvertTextureEnvironment(const std::span<const std::uint32_t> words, const std::uint32_t pname,
                          const bool fixed) {
    std::vector<float> values(words.size());
    for (std::size_t index = 0; index < words.size(); ++index) {
        values[index] = fixed && !TextureEnvironmentEnum(pname)
                            ? Fixed(words[index])
                            : static_cast<float>(Signed(words[index]));
    }
    return values;
}

template <typename Convert>
[[nodiscard]] std::vector<std::int32_t> ConvertOutput(const std::span<const float> values,
                                                      Convert convert) {
    std::vector<std::int32_t> output(values.size());
    std::ranges::transform(values, output.begin(), convert);
    return output;
}

[[nodiscard]] std::vector<float> FixedQueryValues(const std::uint32_t pname,
                                                  const AndroidBoundaryGles1State& core,
                                                  const AndroidBoundaryGles1LegacyState& legacy,
                                                  gles::AngleFrame& frame) {
    switch (pname) {
    case 0x0BA6U:
    case 0x0BA7U:
    case 0x0BA8U: {
        const auto mode = pname == 0x0BA6U   ? kGles1Modelview
                          : pname == 0x0BA7U ? kGles1Projection
                                             : kGles1Texture;
        const auto& matrix = core.Matrices().Current(mode, core.ActiveTexture());
        return {matrix.begin(), matrix.end()};
    }
    case 0x0B00U:
        return {legacy.Color().begin(), legacy.Color().end()};
    case 0x0B02U:
        return {legacy.Normal().begin(), legacy.Normal().end()};
    case 0x0BC1U:
        return {static_cast<float>(legacy.AlphaFunction())};
    case 0x0BC2U:
        return {legacy.AlphaReference()};
    case 0x0BA0U:
        return {static_cast<float>(core.Matrices().Mode())};
    case 0x0B54U:
        return {static_cast<float>(core.ShadeModel())};
    case 0x84E0U:
        return {static_cast<float>(core.ActiveTexture())};
    case 0x84E1U:
        return {static_cast<float>(legacy.ClientActiveTexture())};
    case 0x0C22U: {
        const auto& color = core.Shared().ClearColor();
        return {color.begin(), color.end()};
    }
    case 0x0B73U:
        return {core.Shared().ClearDepth()};
    case 0x0BA2U: {
        const auto& values = core.Shared().Viewport();
        return {static_cast<float>(values[0]), static_cast<float>(values[1]),
                static_cast<float>(values[2]), static_cast<float>(values[3])};
    }
    case 0x0C10U: {
        const auto& values = core.Shared().Scissor();
        return {static_cast<float>(values[0]), static_cast<float>(values[1]),
                static_cast<float>(values[2]), static_cast<float>(values[3])};
    }
    case kGles1FogMode:
    case kGles1FogDensity:
    case 0x0B63U:
    case 0x0B64U:
    case kGles1FogColor:
        return core.Fixed().Fog(pname);
    case kGles1LightModelAmbient:
        return core.Fixed().LightModel(pname);
    case 0x8126U:
    case 0x8127U:
    case 0x8128U:
        return {core.Fixed().PointParameter(pname)};
    case 0x8129U: {
        const auto& values = core.Fixed().PointDistanceAttenuation();
        return {values.begin(), values.end()};
    }
    default: {
        const auto count = pname == 0x846DU || pname == 0x846EU || pname == 0x0B70U ? 2U
                           : pname == 0x0C23U                                       ? 4U
                                                                                    : 1U;
        return frame.GetFloats(pname, count);
    }
    }
}

[[nodiscard]] bool FixedQueryPreservesInteger(const std::uint32_t pname) noexcept {
    return pname == 0x0BA0U || pname == 0x0B54U || pname == 0x84E0U || pname == 0x84E1U ||
           pname == 0x0BC1U || pname == kGles1FogMode;
}

} // namespace

void BindAndroidBoundaryGles1Remaining(gles::GlesDispatchTable& dispatch,
                                       AndroidBoundaryGles1State& core,
                                       AndroidBoundaryGles1LegacyState& legacy,
                                       AndroidBoundaryGles1DrawState& draw,
                                       memory::AddressSpace& address_space,
                                       AndroidBoundaryFrameResolver require_frame) {
    if (!require_frame) {
        throw std::invalid_argument("GLES1 remaining frame resolver is missing");
    }
    dispatch.Bind("glCompressedTexSubImage2D", [&address_space, require_frame](const auto a,
                                                                               const auto thread) {
        const auto size = Signed(a[7]);
        if (size < 0)
            throw std::invalid_argument("glCompressedTexSubImage2D imageSize is negative");
        const auto input = gles::GuestBuffer::Prepare(
            address_space, memory::GuestAddress{a[8]}, static_cast<std::size_t>(size),
            gles::GuestTransferDirection::input, false, thread);
        require_frame("glCompressedTexSubImage2D")
            .CompressedTextureSubImage2D(a[0], Signed(a[1]), Signed(a[2]), Signed(a[3]),
                                         Signed(a[4]), Signed(a[5]), a[6], input.Bytes());
        return 0U;
    });
    dispatch.Bind("glCopyTexSubImage2D", [require_frame](const auto a, const auto) {
        require_frame("glCopyTexSubImage2D")
            .CopyTextureSubImage2D(a[0], Signed(a[1]), Signed(a[2]), Signed(a[3]), Signed(a[4]),
                                   Signed(a[5]), Signed(a[6]), Signed(a[7]));
        return 0U;
    });
    dispatch.Bind("glGetBufferParameteriv",
                  [&address_space, require_frame](const auto a, const auto thread) {
                      const std::array value{
                          require_frame("glGetBufferParameteriv").GetBufferParameter(a[0], a[1])};
                      WriteWords(address_space, a[2], std::span(value), thread);
                      return 0U;
                  });
    dispatch.Bind("glIsBuffer", [require_frame](const auto a, const auto) {
        return require_frame("glIsBuffer").IsBuffer(a[0]) ? 1U : 0U;
    });
    dispatch.Bind("glIsTexture", [require_frame](const auto a, const auto) {
        return require_frame("glIsTexture").IsTexture(a[0]) ? 1U : 0U;
    });
    const auto bind_clip_query = [&dispatch, &legacy, &address_space,
                                  require_frame](const std::string_view name, const bool fixed) {
        dispatch.Bind(name, [&legacy, &address_space, require_frame, name,
                             fixed](const auto a, const auto thread) {
            static_cast<void>(require_frame(name));
            const auto& equation = legacy.ClipPlane(a[0]);
            if (fixed) {
                const auto values = ConvertOutput(equation, ToFixed);
                WriteWords(address_space, a[1], std::span(values), thread);
            } else {
                WriteWords(address_space, a[1], std::span(equation), thread);
            }
            return 0U;
        });
    };
    bind_clip_query("glGetClipPlanef", false);
    bind_clip_query("glGetClipPlanex", true);
    dispatch.Bind("glGetFixedv", [&core, &legacy, &address_space,
                                  require_frame](const auto a, const auto thread) {
        auto& frame = require_frame("glGetFixedv");
        const auto values = FixedQueryValues(a[0], core, legacy, frame);
        const auto converted = ConvertOutput(values, [pname = a[0]](const float value) {
            return FixedQueryPreservesInteger(pname) ? static_cast<std::int32_t>(value)
                                                     : ToFixed(value);
        });
        WriteWords(address_space, a[1], std::span(converted), thread);
        return 0U;
    });
    const auto bind_fixed_state_query = [&dispatch, &address_space,
                                         require_frame](const std::string_view name,
                                                        const bool fixed, const auto resolve) {
        dispatch.Bind(name, [&address_space, require_frame, name, fixed,
                             resolve](const auto a, const auto thread) {
            static_cast<void>(require_frame(name));
            const auto& values = resolve(a);
            if (fixed) {
                const auto converted = ConvertOutput(values, ToFixed);
                WriteWords(address_space, a[2], std::span(converted), thread);
            } else {
                WriteWords(address_space, a[2], std::span(values), thread);
            }
            return 0U;
        });
    };
    bind_fixed_state_query("glGetLightfv", false, [&core](const auto a) -> const auto& {
        return core.Fixed().Light(a[0], a[1]);
    });
    bind_fixed_state_query("glGetLightxv", true, [&core](const auto a) -> const auto& {
        return core.Fixed().Light(a[0], a[1]);
    });
    bind_fixed_state_query("glGetMaterialfv", false, [&core](const auto a) -> const auto& {
        return core.Fixed().Material(a[0], a[1]);
    });
    bind_fixed_state_query("glGetMaterialxv", true, [&core](const auto a) -> const auto& {
        return core.Fixed().Material(a[0], a[1]);
    });
    const auto bind_environment_query = [&dispatch, &core, &legacy, &address_space, require_frame](
                                            const std::string_view name, const char output_kind) {
        dispatch.Bind(name, [&core, &legacy, &address_space, require_frame, name,
                             output_kind](const auto a, const auto thread) {
            static_cast<void>(require_frame(name));
            const auto& values = legacy.TextureEnvironment(core.ActiveTexture(), a[1]);
            if (output_kind == 'f') {
                WriteWords(address_space, a[2], std::span(values), thread);
            } else {
                const auto converted =
                    ConvertOutput(values, [output_kind, pname = a[1]](float value) {
                        return output_kind == 'x' && !TextureEnvironmentEnum(pname)
                                   ? ToFixed(value)
                                   : static_cast<std::int32_t>(value);
                    });
                WriteWords(address_space, a[2], std::span(converted), thread);
            }
            return 0U;
        });
    };
    bind_environment_query("glGetTexEnvfv", 'f');
    bind_environment_query("glGetTexEnviv", 'i');
    bind_environment_query("glGetTexEnvxv", 'x');
    const auto bind_environment_set = [&dispatch, &core, &legacy, &address_space,
                                       require_frame](const std::string_view name,
                                                      const char input_kind, const bool vector) {
        dispatch.Bind(name, [&core, &legacy, &address_space, require_frame, name, input_kind,
                             vector](const auto a, const auto thread) {
            const auto count = vector ? TextureEnvironmentCount(a[1]) : 1U;
            const auto words = vector ? ReadWords(address_space, a[2], count, thread)
                                      : std::vector<std::uint32_t>{a[2]};
            std::vector<float> values;
            if (input_kind == 'f') {
                values.resize(words.size());
                std::ranges::transform(words, values.begin(),
                                       [](const auto word) { return std::bit_cast<float>(word); });
            } else {
                values = ConvertTextureEnvironment(words, a[1], input_kind == 'x');
            }
            const auto texture = core.ActiveTexture();
            legacy.ValidateTextureEnvironment(texture, a[0], a[1], values);
            static_cast<void>(require_frame(name));
            legacy.SetTextureEnvironment(texture, a[0], a[1], values);
            return 0U;
        });
    };
    bind_environment_set("glTexEnviv", 'i', true);
    bind_environment_set("glTexEnvx", 'x', false);
    bind_environment_set("glTexEnvxv", 'x', true);
    const auto bind_texture_parameter_set =
        [&dispatch, &core, &address_space,
         require_frame](const std::string_view name, const char input_kind, const bool vector) {
            dispatch.Bind(name, [&core, &address_space, require_frame, name, input_kind,
                                 vector](const auto a, const auto thread) {
                const auto word = vector ? ReadWords(address_space, a[2], 1U, thread)[0] : a[2];
                if (a[1] == kGenerateMipmap) {
                    const auto enabled = input_kind == 'f'   ? std::bit_cast<float>(word) != 0.0F
                                         : input_kind == 'x' ? Fixed(word) != 0.0F
                                                             : Signed(word) != 0;
                    static_cast<void>(require_frame(name));
                    core.SetGenerateMipmap(a[0], enabled);
                    return 0U;
                }
                auto& frame = require_frame(name);
                if (input_kind == 'f')
                    frame.TextureParameterFloat(a[0], a[1], std::bit_cast<float>(word));
                else if (input_kind == 'x' && !TextureParameterEnum(a[1]))
                    frame.TextureParameterFloat(a[0], a[1], Fixed(word));
                else
                    frame.TextureParameter(a[0], a[1], Signed(word));
                return 0U;
            });
        };
    bind_texture_parameter_set("glTexParameterfv", 'f', true);
    bind_texture_parameter_set("glTexParameteriv", 'i', true);
    bind_texture_parameter_set("glTexParameterx", 'x', false);
    bind_texture_parameter_set("glTexParameterxv", 'x', true);
    const auto bind_texture_parameter_query = [&dispatch, &core, &address_space,
                                               require_frame](const std::string_view name,
                                                              const char output_kind) {
        dispatch.Bind(name, [&core, &address_space, require_frame, name,
                             output_kind](const auto a, const auto thread) {
            auto& frame = require_frame(name);
            if (a[1] == kGenerateMipmap) {
                const auto enabled = core.GenerateMipmapEnabled(a[0]);
                const std::array value{output_kind == 'x' ? (enabled ? 65536 : 0)
                                                          : (enabled ? 1 : 0)};
                WriteWords(address_space, a[2], std::span(value), thread);
            } else if (output_kind == 'f') {
                const std::array value{frame.GetTextureParameterFloat(a[0], a[1])};
                WriteWords(address_space, a[2], std::span(value), thread);
            } else {
                auto value = frame.GetTextureParameterInteger(a[0], a[1]);
                if (output_kind == 'x' && !TextureParameterEnum(a[1]))
                    value = ToFixed(static_cast<float>(value));
                const std::array values{value};
                WriteWords(address_space, a[2], std::span(values), thread);
            }
            return 0U;
        });
    };
    bind_texture_parameter_query("glGetTexParameterfv", 'f');
    bind_texture_parameter_query("glGetTexParameteriv", 'i');
    bind_texture_parameter_query("glGetTexParameterxv", 'x');
    dispatch.Bind("glLogicOp", [&core, require_frame](const auto a, const auto) {
        static_cast<void>(require_frame("glLogicOp"));
        core.SetLogicOperation(a[0]);
        return 0U;
    });
    dispatch.Bind("glPointSizePointerOES", [&core, &draw, require_frame](const auto a, const auto) {
        auto next = draw.PreparePointer(kGles1PointSizeArray, kTexture0, 1, a[0], Signed(a[1]),
                                        a[2], core.TransferState().Snapshot().array_buffer);
        static_cast<void>(require_frame("glPointSizePointerOES"));
        draw.CommitPointer(kGles1PointSizeArray, kTexture0, next);
        return 0U;
    });
}

} // namespace ogplay::runtime::detail

#include "ogplay/runtime/integration/android_boundary_gles.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/cpu/cpu.h"
#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/gles_call_preparation.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"
#include "ogplay/gles/guest_transfer.h"
#include "ogplay/memory/address_space.h"

namespace ogplay::runtime {
namespace {

constexpr memory::GuestAddress kQueryStringPage{0x70001000U};
constexpr std::uint32_t kQueryStringSlotBytes = 1024;
constexpr std::uint32_t kArrayBuffer = 0x8892U;
constexpr std::uint32_t kElementArrayBuffer = 0x8893U;
constexpr std::uint32_t kStreamDraw = 0x88E0U;
constexpr std::uint32_t kByte = 0x1400U;
constexpr std::uint32_t kUnsignedByte = 0x1401U;
constexpr std::uint32_t kShort = 0x1402U;
constexpr std::uint32_t kUnsignedShort = 0x1403U;
constexpr std::uint32_t kFloat = 0x1406U;
constexpr std::uint32_t kFixed = 0x140CU;
constexpr std::size_t kMaximumVertexAttributes = 16U;

std::uint32_t QueryStringOffset(const std::uint32_t parameter) {
    switch (parameter) {
    case 0x1f00U: return 0;
    case 0x1f01U: return kQueryStringSlotBytes;
    case 0x1f02U: return kQueryStringSlotBytes * 2U;
    case 0x8b8cU: return kQueryStringSlotBytes * 3U;
    default: throw std::invalid_argument("unsupported GLES string query");
    }
}
[[nodiscard]] std::size_t VertexAttribScalarBytes(const std::uint32_t type) {
    switch (type) {
    case kByte: case kUnsignedByte: return 1U;
    case kShort: case kUnsignedShort: return 2U;
    case kFloat: case kFixed: return 4U;
    default:
        throw std::invalid_argument("GLES2 vertex attribute type is unsupported");
    }
}
[[nodiscard]] std::uint64_t ClientArrayBytes(const std::int32_t size,
                                             const std::uint32_t type,
                                             const std::int32_t stride,
                                             const std::uint32_t maximum_index) {
    const auto packed =
        static_cast<std::uint64_t>(size) * VertexAttribScalarBytes(type);
    const auto step = stride == 0 ? packed : static_cast<std::uint64_t>(stride);
    if (maximum_index != 0U &&
        step > ((std::numeric_limits<std::uint64_t>::max)() - packed) /
                   maximum_index) {
        throw std::length_error("GLES2 client array byte range overflows");
    }
    return step * maximum_index + packed;
}
[[nodiscard]] std::uint32_t MaximumGuestIndex(const std::span<const std::byte> bytes,
                                              const std::uint32_t type) {
    std::uint32_t maximum{};
    if (type == kUnsignedByte) {
        for (const auto value : bytes) {
            maximum = std::max(maximum, static_cast<std::uint32_t>(
                                            std::to_integer<std::uint8_t>(value)));
        }
        return maximum;
    }
    if (type != kUnsignedShort || bytes.size() % 2U != 0U) {
        throw std::invalid_argument("GLES2 draw index type is unsupported");
    }
    for (std::size_t offset = 0; offset < bytes.size(); offset += 2U) {
        maximum = std::max(
            maximum,
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U));
    }
    return maximum;
}

}  // namespace

class AndroidBoundaryGles::Impl final {
public:
    explicit Impl(memory::AddressSpace& address_space)
        : address_space_(address_space) {}

    std::optional<std::uint32_t> Dispatch(
        const std::string_view symbol,
        const std::array<std::uint32_t, 4>& args,
        const cpu::A32State& state, gles::AngleFrame* const frame) {
        const auto tid = state.ThreadId();
        if (symbol == "glGenBuffers" || symbol == "glGenTextures" ||
            symbol == "glGenFramebuffers" ||
            symbol == "glGenRenderbuffers") {
            auto call = PrepareCall(symbol, std::span(args).first<2>(), tid);
            auto& current = RequireFrame(frame, symbol);
            auto names = symbol == "glGenBuffers"
                             ? current.GenerateBuffers(args[0])
                         : symbol == "glGenTextures"
                             ? current.GenerateTextures(args[0])
                         : symbol == "glGenFramebuffers"
                             ? current.GenerateFramebuffers(args[0])
                             : current.GenerateRenderbuffers(args[0]);
            WriteWords(Pointer(call), names);
            return 0;
        }
        if (symbol == "glDeleteBuffers" || symbol == "glDeleteTextures" ||
            symbol == "glDeleteFramebuffers" ||
            symbol == "glDeleteRenderbuffers") {
            auto call = PrepareCall(symbol, std::span(args).first<2>(), tid);
            const auto names = ReadWords(Pointer(call));
            if (symbol == "glDeleteBuffers") {
                RequireFrame(frame, symbol).DeleteBuffers(names);
                const auto bound = transfer_state_.Snapshot();
                if (std::ranges::find(names, bound.array_buffer) != names.end()) {
                    transfer_state_.BindBuffer(0x8892U, 0);
                }
                if (std::ranges::find(names, bound.element_array_buffer) != names.end()) {
                    transfer_state_.BindBuffer(0x8893U, 0);
                }
            } else if (symbol == "glDeleteTextures") {
                RequireFrame(frame, symbol).DeleteTextures(names);
            } else if (symbol == "glDeleteFramebuffers") {
                RequireFrame(frame, symbol).DeleteFramebuffers(names);
            } else {
                RequireFrame(frame, symbol).DeleteRenderbuffers(names);
            }
            return 0;
        }
        if (symbol == "glBindBuffer") {
            auto next = transfer_state_;
            next.BindBuffer(args[0], args[1]);
            RequireFrame(frame, symbol).BindBuffer(args[0], args[1]);
            transfer_state_ = std::move(next);
            return 0;
        }
        if (symbol == "glBufferData") {
            auto call = PrepareCall(symbol, args, tid);
            const auto& data = Pointer(call);
            RequireFrame(frame, symbol).BufferData(
                args[0], args[1], OptionalBytes(data), args[3]);
            return 0;
        }
        if (symbol == "glActiveTexture") {
            RequireFrame(frame, symbol).ActiveTexture(args[0]);
            return 0;
        }
        if (symbol == "glBindTexture") {
            RequireFrame(frame, symbol).BindTexture(args[0], args[1]);
            return 0;
        }
        if (symbol == "glBindFramebuffer") {
            RequireFrame(frame, symbol).BindFramebuffer(args[0], args[1]);
            return 0;
        }
        if (symbol == "glBindRenderbuffer") {
            RequireFrame(frame, symbol).BindRenderbuffer(args[0], args[1]);
            return 0;
        }
        if (symbol == "glCheckFramebufferStatus") {
            return RequireFrame(frame, symbol).CheckFramebufferStatus(args[0]);
        }
        if (symbol == "glRenderbufferStorage") {
            RequireFrame(frame, symbol).RenderbufferStorage(
                args[0], args[1], std::bit_cast<std::int32_t>(args[2]),
                std::bit_cast<std::int32_t>(args[3]));
            return 0;
        }
        if (symbol == "glFramebufferTexture2D") {
            RequireFrame(frame, symbol).FramebufferTexture2D(
                args[0], args[1], args[2], args[3],
                std::bit_cast<std::int32_t>(StackWord(state, 0)));
            return 0;
        }
        if (symbol == "glFramebufferRenderbuffer") {
            RequireFrame(frame, symbol).FramebufferRenderbuffer(
                args[0], args[1], args[2], args[3]);
            return 0;
        }
        if (symbol == "glGenerateMipmap") {
            RequireFrame(frame, symbol).GenerateMipmap(args[0]);
            return 0;
        }
        if (symbol == "glPixelStorei") {
            const auto value = std::bit_cast<std::int32_t>(args[1]);
            auto next = transfer_state_;
            next.PixelStore(args[0], value);
            RequireFrame(frame, symbol).PixelStore(args[0], value);
            transfer_state_ = std::move(next);
            return 0;
        }
        if (symbol == "glTexParameteri") {
            RequireFrame(frame, symbol).TextureParameter(
                args[0], args[1], std::bit_cast<std::int32_t>(args[2]));
            return 0;
        }
        if (symbol == "glTexImage2D") {
            std::array<std::uint32_t, 9> all{args[0], args[1], args[2], args[3]};
            for (std::size_t index = 4; index < all.size(); ++index) {
                all[index] = StackWord(state,
                    static_cast<std::uint32_t>((index - 4U) * 4U));
            }
            auto call = PrepareCall(symbol, all, tid);
            const auto& pixels = Pointer(call);
            RequireFrame(frame, symbol).TextureImage2D(
                all[0], std::bit_cast<std::int32_t>(all[1]),
                std::bit_cast<std::int32_t>(all[2]),
                std::bit_cast<std::int32_t>(all[3]),
                std::bit_cast<std::int32_t>(all[4]),
                std::bit_cast<std::int32_t>(all[5]), all[6], all[7],
                OptionalBytes(pixels));
            return 0;
        }
        if (symbol == "glTexSubImage2D") {
            std::array<std::uint32_t, 9> all{args[0], args[1], args[2], args[3]};
            for (std::size_t index = 4; index < all.size(); ++index) {
                all[index] = StackWord(state,
                    static_cast<std::uint32_t>((index - 4U) * 4U));
            }
            auto call = PrepareCall(symbol, all, tid);
            RequireFrame(frame, symbol).TextureSubImage2D(
                all[0], std::bit_cast<std::int32_t>(all[1]),
                std::bit_cast<std::int32_t>(all[2]),
                std::bit_cast<std::int32_t>(all[3]),
                std::bit_cast<std::int32_t>(all[4]),
                std::bit_cast<std::int32_t>(all[5]), all[6], all[7],
                Pointer(call).Bytes());
            return 0;
        }
        if (symbol == "glEnableVertexAttribArray" ||
            symbol == "glDisableVertexAttribArray") {
            const auto index = args[0];
            RequireAttributeIndex(index);
            attributes_[index].enabled = symbol == "glEnableVertexAttribArray";
            RequireFrame(frame, symbol).SetVertexAttributeEnabled(
                index, attributes_[index].enabled);
            return 0;
        }
        if (symbol == "glVertexAttribPointer") {
            const auto index = args[0];
            RequireAttributeIndex(index);
            const auto size = std::bit_cast<std::int32_t>(args[1]);
            const auto type = args[2];
            const auto normalized = args[3] != 0;
            const auto stride = std::bit_cast<std::int32_t>(StackWord(state, 0));
            const auto pointer = StackWord(state, 4);
            if (size < 1 || size > 4) {
                throw std::invalid_argument(
                    "GLES2 vertex attribute size is outside 1..4");
            }
            if (stride < 0) {
                throw std::invalid_argument(
                    "GLES2 vertex attribute stride is negative");
            }
            static_cast<void>(VertexAttribScalarBytes(type));
            std::array<std::uint32_t, 6> all{
                args[0], args[1], args[2], args[3],
                static_cast<std::uint32_t>(stride), pointer};
            auto call = PrepareCall(symbol, all, tid);
            if (call.pointers.size() != 1 || !call.pointers.front().deferred) {
                throw std::logic_error(
                    "glVertexAttribPointer expected a deferred pointer");
            }
            const auto buffer = transfer_state_.Snapshot().array_buffer;
            attributes_[index] = {
                .size = size,
                .type = type,
                .normalized = normalized,
                .stride = stride,
                .pointer = pointer,
                .buffer = buffer,
                .enabled = attributes_[index].enabled,
                .defined = true,
            };
            if (buffer != 0U) {
                RequireFrame(frame, symbol).VertexAttributePointer(
                    index, size, type, normalized, stride, pointer);
            }
            return 0;
        }
        if (symbol == "glUniform1f") {
            RequireFrame(frame, symbol).Uniform1f(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<float>(args[1]));
            return 0;
        }
        if (symbol == "glUniform1i") {
            RequireFrame(frame, symbol).Uniform1i(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<std::int32_t>(args[1]));
            return 0;
        }
        if (symbol == "glUniform4f") {
            RequireFrame(frame, symbol).Uniform4f(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<float>(args[1]), std::bit_cast<float>(args[2]),
                std::bit_cast<float>(args[3]),
                std::bit_cast<float>(StackWord(state, 0)));
            return 0;
        }
        if (symbol == "glUniformMatrix3fv") {
            auto call = PrepareCall(symbol, args, tid);
            const auto values = ReadFloats(Pointer(call));
            RequireFrame(frame, symbol).UniformMatrix3(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<std::int32_t>(args[1]), args[2] != 0, values);
            return 0;
        }
        if (symbol == "glUniform1fv" || symbol == "glUniform2fv" ||
            symbol == "glUniform3fv" || symbol == "glUniform4fv" ||
            symbol == "glUniform1iv" || symbol == "glUniform2iv" ||
            symbol == "glUniform3iv" || symbol == "glUniform4iv") {
            auto call = PrepareCall(symbol, std::span(args).first<3>(), tid);
            const auto components =
                symbol == "glUniform1fv" || symbol == "glUniform1iv" ? 1U :
                symbol == "glUniform2fv" || symbol == "glUniform2iv" ? 2U :
                symbol == "glUniform3fv" || symbol == "glUniform3iv" ? 3U : 4U;
            const auto count = std::bit_cast<std::int32_t>(args[1]);
            if (symbol.ends_with("fv")) {
                RequireFrame(frame, symbol).UniformFloats(
                    std::bit_cast<std::int32_t>(args[0]), count, components,
                    ReadFloats(Pointer(call)));
            } else {
                RequireFrame(frame, symbol).UniformIntegers(
                    std::bit_cast<std::int32_t>(args[0]), count, components,
                    ReadIntegers(Pointer(call)));
            }
            return 0;
        }
        if (symbol == "glUniformMatrix4fv") {
            auto call = PrepareCall(symbol, args, tid);
            RequireFrame(frame, symbol).UniformMatrix4(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<std::int32_t>(args[1]), args[2] != 0,
                ReadFloats(Pointer(call)));
            return 0;
        }
        if (symbol == "glVertexAttrib4f") {
            RequireFrame(frame, symbol).VertexAttribute4f(
                args[0], std::bit_cast<float>(args[1]),
                std::bit_cast<float>(args[2]), std::bit_cast<float>(args[3]),
                std::bit_cast<float>(StackWord(state, 0)));
            return 0;
        }
        if (symbol == "glGetActiveAttrib" ||
            symbol == "glGetActiveUniform") {
            std::array<std::uint32_t, 7> all{
                args[0], args[1], args[2], args[3], StackWord(state, 0),
                StackWord(state, 4), StackWord(state, 8)};
            auto call = PrepareCall(symbol, all, tid);
            const auto result = symbol == "glGetActiveAttrib"
                ? RequireFrame(frame, symbol).GetActiveAttribute(all[0], all[1])
                : RequireFrame(frame, symbol).GetActiveUniform(all[0], all[1]);
            const auto length = WriteText(Pointer(call, 6), result.name);
            WriteWord(Pointer(call, 3), length);
            WriteWord(Pointer(call, 4),
                      std::bit_cast<std::uint32_t>(result.size));
            WriteWord(Pointer(call, 5), result.type);
            return 0;
        }
        if (symbol == "glGetProgramInfoLog" ||
            symbol == "glGetShaderInfoLog") {
            auto call = PrepareCall(symbol, args, tid);
            const auto log = symbol == "glGetProgramInfoLog"
                ? RequireFrame(frame, symbol).GetProgramInfoLog(args[0])
                : RequireFrame(frame, symbol).GetShaderInfoLog(args[0]);
            const auto length = WriteText(Pointer(call, 3), log);
            WriteWord(Pointer(call, 2), length);
            return 0;
        }
        if (symbol == "glGetIntegerv") {
            auto call = PrepareCall(symbol, std::span(args).first<2>(), tid);
            auto& output = Pointer(call);
            const auto values = RequireFrame(frame, symbol).GetIntegers(
                args[0], output.WritableBytes().size() / 4U);
            std::vector<std::uint32_t> words;
            words.reserve(values.size());
            for (const auto value : values) {
                words.push_back(std::bit_cast<std::uint32_t>(value));
            }
            WriteWords(output, words);
            return 0;
        }
        if (symbol == "glGetString") {
            return WriteQueryString(args[0],
                                    RequireFrame(frame, symbol).GetString(args[0]), tid);
        }
        if (symbol == "glGetError") return RequireFrame(frame, symbol).GetError();
        if (symbol == "glEnable" || symbol == "glDisable") {
            RequireFrame(frame, symbol).SetCapability(args[0], symbol == "glEnable");
            return 0;
        }
        if (symbol == "glBlendFunc") {
            RequireFrame(frame, symbol).BlendFunction(args[0], args[1]);
            return 0;
        }
        if (symbol == "glBlendColor") {
            RequireFrame(frame, symbol).BlendColor(
                std::bit_cast<float>(args[0]), std::bit_cast<float>(args[1]),
                std::bit_cast<float>(args[2]), std::bit_cast<float>(args[3]));
            return 0;
        }
        if (symbol == "glBlendEquation") {
            RequireFrame(frame, symbol).BlendEquation(args[0]);
            return 0;
        }
        if (symbol == "glSampleCoverage") {
            RequireFrame(frame, symbol).SampleCoverage(
                std::bit_cast<float>(args[0]), args[1] != 0U);
            return 0;
        }
        if (symbol == "glFlush") {
            RequireFrame(frame, symbol).Flush();
            return 0;
        }
        if (symbol == "glDrawArrays") {
            auto& current = RequireFrame(frame, symbol);
            const auto first = std::bit_cast<std::int32_t>(args[1]);
            const auto count = std::bit_cast<std::int32_t>(args[2]);
            if (first < 0 || count < 0) {
                throw std::invalid_argument("GLES2 draw array range is negative");
            }
            if (count == 0) return 0;
            const auto maximum = static_cast<std::uint64_t>(first) +
                                 static_cast<std::uint64_t>(count) - 1U;
            if (maximum > (std::numeric_limits<std::uint32_t>::max)()) {
                throw std::length_error("GLES2 draw array index overflows");
            }
            StageClientAttributes(current, static_cast<std::uint32_t>(maximum),
                                  tid);
            current.DrawArrays(args[0], first, count);
            RestoreBufferBindings(current);
            return 0;
        }
        if (symbol == "glDrawElements") {
            auto& current = RequireFrame(frame, symbol);
            const auto count = std::bit_cast<std::int32_t>(args[1]);
            const auto type = args[2];
            if (count < 0) {
                throw std::invalid_argument("GLES2 draw element count is negative");
            }
            if (count == 0) return 0;
            auto call = PrepareCall(symbol, args, tid);
            if (call.pointers.size() != 1) {
                throw std::logic_error("glDrawElements expected one pointer");
            }
            const auto element_buffer =
                transfer_state_.Snapshot().element_array_buffer;
            if (element_buffer != 0U) {
                if (!call.pointers.front().deferred) {
                    throw std::logic_error(
                        "glDrawElements expected a deferred index offset");
                }
                if (HasEnabledClientAttribute()) {
                    throw std::runtime_error(
                        "GLES2 cannot stage client arrays from an opaque element buffer");
                }
                current.DrawElements(args[0], count, type, args[3]);
                return 0;
            }
            if (call.pointers.front().deferred ||
                !call.pointers.front().transfer.has_value()) {
                throw std::logic_error(
                    "glDrawElements expected transferred client indices");
            }
            const auto indices = call.pointers.front().transfer->Bytes();
            const auto maximum = MaximumGuestIndex(indices, type);
            StageClientAttributes(current, maximum, tid);
            EnsureIndexStagingBuffer(current);
            current.BindBuffer(kElementArrayBuffer, index_staging_buffer_);
            current.BufferData(kElementArrayBuffer,
                               static_cast<std::uint32_t>(indices.size()),
                               indices, kStreamDraw);
            current.DrawElements(args[0], count, type, 0U);
            RestoreBufferBindings(current);
            return 0;
        }
        if (symbol == "glReadPixels") {
            std::array<std::uint32_t, 7> all{
                args[0], args[1], args[2], args[3],
                StackWord(state, 0), StackWord(state, 4), StackWord(state, 8)};
            auto call = PrepareCall(symbol, all, tid);
            auto& output = Pointer(call);
            RequireFrame(frame, symbol).ReadPixels(
                std::bit_cast<std::int32_t>(all[0]),
                std::bit_cast<std::int32_t>(all[1]),
                std::bit_cast<std::int32_t>(all[2]),
                std::bit_cast<std::int32_t>(all[3]), all[4], all[5],
                output.WritableBytes());
            output.Commit();
            return 0;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool HasEnabledVertexAttribute() const noexcept {
        return std::ranges::any_of(attributes_, [](const Attribute& attribute) {
            return attribute.enabled && attribute.defined;
        });
    }

    void Reset() noexcept {
        transfer_state_ = {};
        attributes_ = {};
        staging_buffers_.clear();
        client_array_staging_.clear();
        index_staging_buffer_ = 0;
    }

private:
    struct Attribute final {
        std::int32_t size{};
        std::uint32_t type{};
        bool normalized{};
        std::int32_t stride{};
        std::uint32_t pointer{};
        std::uint32_t buffer{};
        bool enabled{};
        bool defined{};
    };

    static void RequireAttributeIndex(const std::uint32_t index) {
        if (index >= kMaximumVertexAttributes) {
            throw std::invalid_argument(
                "GLES2 vertex attribute index is outside 0..15");
        }
    }

    [[nodiscard]] bool HasEnabledClientAttribute() const noexcept {
        return std::ranges::any_of(attributes_, [](const Attribute& attribute) {
            return attribute.enabled && attribute.defined &&
                   attribute.buffer == 0U;
        });
    }

    void EnsureStagingBuffers(gles::AngleFrame& frame) {
        if (!staging_buffers_.empty()) return;
        staging_buffers_ = frame.GenerateBuffers(kMaximumVertexAttributes);
        client_array_staging_.resize(kMaximumVertexAttributes);
    }

    void EnsureIndexStagingBuffer(gles::AngleFrame& frame) {
        if (index_staging_buffer_ != 0U) return;
        const auto buffers = frame.GenerateBuffers(1);
        index_staging_buffer_ = buffers.front();
    }

    void StageClientAttributes(gles::AngleFrame& frame,
                               const std::uint32_t maximum_index,
                               const std::uint64_t thread_id) {
        if (!HasEnabledClientAttribute()) return;
        EnsureStagingBuffers(frame);
        for (std::size_t index = 0; index < attributes_.size(); ++index) {
            const auto& attribute = attributes_[index];
            if (!attribute.enabled || !attribute.defined ||
                attribute.buffer != 0U) {
                continue;
            }
            const auto bytes = ClientArrayBytes(
                attribute.size, attribute.type, attribute.stride, maximum_index);
            const auto transfer = gles::PrepareGuestInput(
                address_space_, memory::GuestAddress{attribute.pointer}, bytes,
                false, client_array_staging_[index], thread_id);
            frame.BindBuffer(kArrayBuffer, staging_buffers_[index]);
            frame.BufferData(kArrayBuffer,
                             static_cast<std::uint32_t>(transfer.size()),
                             transfer, kStreamDraw);
            frame.VertexAttributePointer(
                static_cast<std::uint32_t>(index), attribute.size,
                attribute.type, attribute.normalized, attribute.stride, 0U);
        }
    }

    void RestoreBufferBindings(gles::AngleFrame& frame) {
        const auto bound = transfer_state_.Snapshot();
        frame.BindBuffer(kArrayBuffer, bound.array_buffer);
        frame.BindBuffer(kElementArrayBuffer, bound.element_array_buffer);
    }

    std::uint32_t StackWord(const cpu::A32State& state,
                            const std::uint32_t offset) const {
        std::array<std::byte, 4> bytes{};
        address_space_.Read(memory::GuestAddress{
            state.Register(cpu::CoreRegister::sp) + offset}, bytes,
            state.ThreadId());
        std::uint32_t value{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            value |= static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[index])) << (index * 8U);
        }
        return value;
    }

    gles::PreparedGlesCall PrepareCall(
        const std::string_view symbol, const std::span<const std::uint32_t> args,
        const std::uint64_t tid) {
        const auto id = gles::GlesDispatchTable::Find(symbol);
        if (!id.has_value()) {
            throw std::logic_error("GLES resource handler is not cataloged");
        }
        return gles::PrepareGles2Call(
            address_space_, *id, args, tid, &transfer_state_);
    }

    static gles::GuestBuffer& Pointer(gles::PreparedGlesCall& call) {
        if (call.pointers.size() != 1 ||
            !call.pointers.front().transfer.has_value()) {
            throw std::logic_error(
                "GLES resource handler expected one transferred pointer");
        }
        return *call.pointers.front().transfer;
    }

    static gles::GuestBuffer& Pointer(gles::PreparedGlesCall& call,
                                      const std::size_t parameter_index) {
        const auto found = std::ranges::find_if(
            call.pointers, [parameter_index](const auto& pointer) {
                return pointer.parameter_index == parameter_index;
            });
        if (found == call.pointers.end() || !found->transfer.has_value()) {
            throw std::logic_error(
                "GLES resource handler expected a transferred parameter");
        }
        return *found->transfer;
    }

    static std::vector<std::uint32_t> ReadWords(
        const gles::GuestBuffer& transfer) {
        const auto bytes = transfer.Bytes();
        if (bytes.size() % 4U != 0) {
            throw std::logic_error("GLES name array is misaligned");
        }
        std::vector<std::uint32_t> words(bytes.size() / 4U);
        for (std::size_t word = 0; word < words.size(); ++word) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                words[word] |= static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[word * 4U + byte]))
                    << (byte * 8U);
            }
        }
        return words;
    }

    static std::vector<float> ReadFloats(const gles::GuestBuffer& transfer) {
        const auto words = ReadWords(transfer);
        std::vector<float> values;
        values.reserve(words.size());
        for (const auto word : words) {
            values.push_back(std::bit_cast<float>(word));
        }
        return values;
    }

    static std::vector<std::int32_t> ReadIntegers(
        const gles::GuestBuffer& transfer) {
        const auto words = ReadWords(transfer);
        std::vector<std::int32_t> values;
        values.reserve(words.size());
        for (const auto word : words) {
            values.push_back(std::bit_cast<std::int32_t>(word));
        }
        return values;
    }

    static void WriteWord(gles::GuestBuffer& transfer,
                          const std::uint32_t word) {
        if (transfer.IsNull()) return;
        WriteWords(transfer, std::span(&word, 1));
    }

    static std::uint32_t WriteText(gles::GuestBuffer& transfer,
                                   const std::string_view text) {
        auto bytes = transfer.WritableBytes();
        const auto length = bytes.empty()
            ? 0U
            : std::min(text.size(), bytes.size() - 1U);
        std::ranges::fill(bytes, std::byte{});
        for (std::size_t index = 0; index < length; ++index) {
            bytes[index] = static_cast<std::byte>(
                static_cast<unsigned char>(text[index]));
        }
        transfer.Commit();
        return static_cast<std::uint32_t>(length);
    }

    static void WriteWords(gles::GuestBuffer& transfer,
                           const std::span<const std::uint32_t> words) {
        auto bytes = transfer.WritableBytes();
        if (bytes.size() != words.size() * 4U) {
            throw std::logic_error("GLES name output size differs");
        }
        for (std::size_t word = 0; word < words.size(); ++word) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                bytes[word * 4U + byte] =
                    static_cast<std::byte>(words[word] >> (byte * 8U));
            }
        }
        transfer.Commit();
    }

    static std::optional<std::span<const std::byte>> OptionalBytes(
        const gles::GuestBuffer& transfer) {
        if (transfer.IsNull()) return std::nullopt;
        return transfer.Bytes();
    }

    static gles::AngleFrame& RequireFrame(
        gles::AngleFrame* const frame, const std::string_view operation) {
        if (frame == nullptr) {
            throw std::runtime_error(
                std::string(operation) + " has no current ANGLE frame");
        }
        return *frame;
    }

    void EnsureQueryStringPage() {
        if (query_string_page_mapped_) return;
        if (address_space_.PageSize() < kQueryStringSlotBytes * 4U) {
            throw std::length_error("guest page is too small for GLES query strings");
        }
        address_space_.Map({kQueryStringPage, address_space_.PageSize()},
                           memory::PageProtection::read |
                               memory::PageProtection::write);
        address_space_.Protect({kQueryStringPage, address_space_.PageSize()},
                               memory::PageProtection::read);
        query_string_page_mapped_ = true;
    }

    std::uint32_t WriteQueryString(const std::uint32_t parameter,
                                   const std::string_view value,
                                   const std::uint64_t tid) {
        const auto offset = QueryStringOffset(parameter);
        if (value.size() >= kQueryStringSlotBytes) {
            throw std::length_error("ANGLE query string exceeds its guest slot");
        }
        EnsureQueryStringPage();
        std::vector<std::byte> bytes;
        bytes.reserve(value.size() + 1U);
        for (const auto character : value) {
            bytes.push_back(static_cast<std::byte>(
                static_cast<unsigned char>(character)));
        }
        bytes.push_back(std::byte{});
        const auto page = memory::GuestRange{kQueryStringPage,
                                              address_space_.PageSize()};
        address_space_.Protect(page, memory::PageProtection::read |
                                         memory::PageProtection::write);
        try {
            address_space_.Write(kQueryStringPage.Add(offset), bytes, tid);
        } catch (...) {
            address_space_.Protect(page, memory::PageProtection::read);
            throw;
        }
        address_space_.Protect(page, memory::PageProtection::read);
        return kQueryStringPage.Add(offset).Value();
    }

    memory::AddressSpace& address_space_;
    gles::GlesTransferState transfer_state_;
    std::array<Attribute, kMaximumVertexAttributes> attributes_{};
    std::vector<std::uint32_t> staging_buffers_;
    std::vector<std::vector<std::byte>> client_array_staging_;
    std::uint32_t index_staging_buffer_{};
    bool query_string_page_mapped_{};
};

AndroidBoundaryGles::AndroidBoundaryGles(memory::AddressSpace& address_space)
    : impl_(std::make_unique<Impl>(address_space)) {}
AndroidBoundaryGles::~AndroidBoundaryGles() = default;

std::optional<std::uint32_t> AndroidBoundaryGles::Dispatch(
    const std::string_view symbol,
    const std::array<std::uint32_t, 4>& arguments,
    const cpu::A32State& state, gles::AngleFrame* const frame) {
    return impl_->Dispatch(symbol, arguments, state, frame);
}

bool AndroidBoundaryGles::HasEnabledVertexAttribute() const noexcept {
    return impl_->HasEnabledVertexAttribute();
}

void AndroidBoundaryGles::Reset() noexcept { impl_->Reset(); }

}  // namespace ogplay::runtime

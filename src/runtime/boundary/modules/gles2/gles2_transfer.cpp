#include "gles2_module.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/gles/gles_call_preparation.h"
#include "ogplay/gles/guest_transfer.h"

namespace ogplay::runtime {
namespace {

[[nodiscard]] std::int32_t Signed(const std::uint32_t word) noexcept {
    return std::bit_cast<std::int32_t>(word);
}

[[nodiscard]] gles::PreparedGlesCall Prepare(BoundaryCallServices& calls,
                                             GraphicsBoundaryContext& graphics,
                                             const gles::GlesThunkId id, const A32CallFrame& call) {
    return gles::PrepareGles2Call(calls.address_space, id, call.Arguments(), call.ThreadId(),
                                  &graphics.gl_context.Shared().transfer);
}

[[nodiscard]] gles::GuestBuffer& OnlyPointer(gles::PreparedGlesCall& call) {
    if (call.pointers.size() != 1U || !call.pointers.front().transfer.has_value()) {
        throw std::logic_error("GLES2 transfer handler expected one prepared pointer");
    }
    return *call.pointers.front().transfer;
}

[[nodiscard]] std::uint32_t ReadWord(const gles::GuestBuffer& input) {
    const auto bytes = input.Bytes();
    if (bytes.size() != sizeof(std::uint32_t)) {
        throw std::logic_error("GLES2 scalar input has the wrong size");
    }
    std::uint32_t word{};
    for (std::size_t byte = 0; byte < sizeof(word); ++byte) {
        word |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[byte]))
                << (byte * 8U);
    }
    return word;
}

void WriteWords(gles::GuestBuffer& output, const std::span<const std::uint32_t> words) {
    auto bytes = output.WritableBytes();
    if (bytes.size() != words.size() * sizeof(std::uint32_t)) {
        throw std::logic_error("GLES2 query output has the wrong size");
    }
    for (std::size_t index = 0; index < words.size(); ++index) {
        for (std::size_t byte = 0; byte < sizeof(std::uint32_t); ++byte) {
            bytes[index * sizeof(std::uint32_t) + byte] =
                static_cast<std::byte>(words[index] >> (byte * 8U));
        }
    }
    output.Commit();
}

void WriteInteger(gles::GuestBuffer& output, const std::int32_t value) {
    const auto word = std::bit_cast<std::uint32_t>(value);
    WriteWords(output, std::span(&word, 1U));
}

void WriteFloats(gles::GuestBuffer& output, const std::span<const float> values) {
    std::vector<std::uint32_t> words;
    words.reserve(values.size());
    for (const auto value : values) {
        words.push_back(std::bit_cast<std::uint32_t>(value));
    }
    WriteWords(output, words);
}

void WriteBooleans(gles::GuestBuffer& output, const std::span<const std::uint8_t> values) {
    auto bytes = output.WritableBytes();
    if (bytes.size() != values.size()) {
        throw std::logic_error("GLES2 boolean query output has the wrong size");
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        bytes[index] = static_cast<std::byte>(values[index]);
    }
    output.Commit();
}

} // namespace

std::uint32_t Gles2Module::BufferSubData(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 13U, call);
    graphics_.RequireFrame("glBufferSubData")
        .BufferSubData(arguments[0], Signed(arguments[1]), OnlyPointer(prepared).Bytes());
    return 0U;
}

std::uint32_t Gles2Module::CompressedTexImage2D(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 21U, call);
    graphics_.RequireFrame("glCompressedTexImage2D")
        .CompressedTextureImage2D(arguments[0], Signed(arguments[1]), arguments[2],
                                  Signed(arguments[3]), Signed(arguments[4]), Signed(arguments[5]),
                                  OnlyPointer(prepared).Bytes());
    if (Signed(arguments[1]) == 0) {
        graphics_.gl_context.Shared().SetTextureBaseFormat(arguments[0], arguments[2]);
    }
    return 0U;
}

std::uint32_t Gles2Module::CompressedTexSubImage2D(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 22U, call);
    graphics_.RequireFrame("glCompressedTexSubImage2D")
        .CompressedTextureSubImage2D(arguments[0], Signed(arguments[1]), Signed(arguments[2]),
                                     Signed(arguments[3]), Signed(arguments[4]),
                                     Signed(arguments[5]), arguments[6],
                                     OnlyPointer(prepared).Bytes());
    return 0U;
}

std::uint32_t Gles2Module::CopyTexImage2D(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    graphics_.RequireFrame("glCopyTexImage2D")
        .CopyTextureImage2D(arguments[0], Signed(arguments[1]), arguments[2], Signed(arguments[3]),
                            Signed(arguments[4]), Signed(arguments[5]), Signed(arguments[6]),
                            Signed(arguments[7]));
    if (Signed(arguments[1]) == 0) {
        graphics_.gl_context.Shared().SetTextureBaseFormat(arguments[0], arguments[2]);
    }
    return 0U;
}

std::uint32_t Gles2Module::CopyTexSubImage2D(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    graphics_.RequireFrame("glCopyTexSubImage2D")
        .CopyTextureSubImage2D(arguments[0], Signed(arguments[1]), Signed(arguments[2]),
                               Signed(arguments[3]), Signed(arguments[4]), Signed(arguments[5]),
                               Signed(arguments[6]), Signed(arguments[7]));
    return 0U;
}

std::uint32_t Gles2Module::GetBooleanv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 58U, call);
    auto& output = OnlyPointer(prepared);
    const auto values =
        graphics_.RequireFrame("glGetBooleanv").GetBooleans(arguments[0], output.Size());
    WriteBooleans(output, values);
    return 0U;
}

std::uint32_t Gles2Module::GetBufferParameteriv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 59U, call);
    const auto value = graphics_.RequireFrame("glGetBufferParameteriv")
                           .GetBufferParameter(arguments[0], arguments[1]);
    WriteInteger(OnlyPointer(prepared), value);
    return 0U;
}

std::uint32_t Gles2Module::GetFloatv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 61U, call);
    auto& output = OnlyPointer(prepared);
    const auto values =
        graphics_.RequireFrame("glGetFloatv").GetFloats(arguments[0], output.Size() / 4U);
    WriteFloats(output, values);
    return 0U;
}

std::uint32_t Gles2Module::GetFramebufferAttachmentParameteriv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 62U, call);
    const auto value =
        graphics_.RequireFrame("glGetFramebufferAttachmentParameteriv")
            .GetFramebufferAttachmentParameter(arguments[0], arguments[1], arguments[2]);
    WriteInteger(OnlyPointer(prepared), value);
    return 0U;
}

std::uint32_t Gles2Module::GetRenderbufferParameteriv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 66U, call);
    const auto value = graphics_.RequireFrame("glGetRenderbufferParameteriv")
                           .GetRenderbufferParameter(arguments[0], arguments[1]);
    WriteInteger(OnlyPointer(prepared), value);
    return 0U;
}

std::uint32_t Gles2Module::GetTexParameterfv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 72U, call);
    const auto value = graphics_.RequireFrame("glGetTexParameterfv")
                           .GetTextureParameterFloat(arguments[0], arguments[1]);
    WriteFloats(OnlyPointer(prepared), std::span(&value, 1U));
    return 0U;
}

std::uint32_t Gles2Module::GetTexParameteriv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 73U, call);
    const auto value = graphics_.RequireFrame("glGetTexParameteriv")
                           .GetTextureParameterInteger(arguments[0], arguments[1]);
    WriteInteger(OnlyPointer(prepared), value);
    return 0U;
}

std::uint32_t Gles2Module::TexParameterf(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    graphics_.RequireFrame("glTexParameterf")
        .TextureParameterFloat(arguments[0], arguments[1], std::bit_cast<float>(arguments[2]));
    return 0U;
}

std::uint32_t Gles2Module::TexParameterfv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 107U, call);
    graphics_.RequireFrame("glTexParameterfv")
        .TextureParameterFloat(arguments[0], arguments[1],
                               std::bit_cast<float>(ReadWord(OnlyPointer(prepared))));
    return 0U;
}

std::uint32_t Gles2Module::TexParameteriv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 109U, call);
    graphics_.RequireFrame("glTexParameteriv")
        .TextureParameter(arguments[0], arguments[1], Signed(ReadWord(OnlyPointer(prepared))));
    return 0U;
}

} // namespace ogplay::runtime

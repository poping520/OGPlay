#include "gles2_module.h"

#include "runtime/boundary/services/gles_transfer_io.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/gles/gles_call_preparation.h"
#include "ogplay/gles/guest_transfer.h"

namespace ogplay::runtime {
namespace {

[[nodiscard]] std::int32_t Signed(const std::uint32_t word) noexcept {
    return std::bit_cast<std::int32_t>(word);
}

[[nodiscard]] gles::PreparedGlesCall
Prepare(BoundaryCallServices& calls, GraphicsBoundaryContext& graphics, const gles::GlesThunkId id,
        const A32CallFrame& call,
        const std::uint64_t size_limit = gles::kDefaultGuestTransferLimit) {
    return gles::PrepareGles2Call(calls.address_space, id, call.Arguments(), call.ThreadId(),
                                  &graphics.gl_context.Shared().transfer, size_limit);
}

[[nodiscard]] gles::GuestBuffer& Pointer(gles::PreparedGlesCall& call,
                                         const std::size_t parameter_index) {
    const auto found = std::ranges::find_if(call.pointers, [parameter_index](const auto& pointer) {
        return pointer.parameter_index == parameter_index;
    });
    if (found == call.pointers.end() || !found->transfer.has_value()) {
        throw std::logic_error("GLES2 shader handler expected a prepared pointer");
    }
    return *found->transfer;
}

[[nodiscard]] std::vector<std::uint32_t> ReadWords(const gles::GuestBuffer& input) {
    return gles_io::ReadWordsAligned(input,
                                     "GLES2 shader input is not word aligned");
}

[[nodiscard]] std::vector<float> ReadFloats(const gles::GuestBuffer& input) {
    return gles_io::ValuesFromWords<float>(ReadWords(input));
}

[[nodiscard]] std::string ReadCString(const gles::GuestBuffer& input) {
    const auto bytes = input.Bytes();
    const auto end = std::ranges::find(bytes, std::byte{});
    if (end == bytes.end()) {
        throw std::logic_error("prepared GLES2 string lost its terminator");
    }
    std::string result;
    result.reserve(static_cast<std::size_t>(end - bytes.begin()));
    for (auto iterator = bytes.begin(); iterator != end; ++iterator) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(*iterator)));
    }
    return result;
}

void WriteWords(gles::GuestBuffer& output, const std::span<const std::uint32_t> words) {
    auto bytes = output.WritableBytes();
    if (words.size() * sizeof(std::uint32_t) > bytes.size() ||
        bytes.size() % sizeof(std::uint32_t) != 0U) {
        throw std::logic_error("GLES2 shader output has the wrong size");
    }
    std::ranges::fill(bytes, std::byte{});
    gles_io::StoreWordsLE(bytes, words);
    output.Commit();
}

void WriteInteger(gles::GuestBuffer& output, const std::int32_t value) {
    if (output.IsNull())
        return;
    const auto word = std::bit_cast<std::uint32_t>(value);
    WriteWords(output, std::span(&word, 1U));
}

void WriteIntegers(gles::GuestBuffer& output, const std::span<const std::int32_t> values) {
    WriteWords(output, gles_io::WordsFromValues(values));
}

void WriteFloats(gles::GuestBuffer& output, const std::span<const float> values) {
    WriteWords(output, gles_io::WordsFromValues(values));
}

} // namespace

std::uint32_t Gles2Module::BindAttribLocation(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 2U, call, kMaximumGlesNameBytes);
    graphics_.RequireFrame("glBindAttribLocation")
        .BindAttribLocation(arguments[0], arguments[1], ReadCString(Pointer(prepared, 2U)));
    return 0U;
}

std::uint32_t Gles2Module::DetachShader(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    graphics_.RequireFrame("glDetachShader").DetachShader(arguments[0], arguments[1]);
    return 0U;
}

std::uint32_t Gles2Module::GetAttachedShaders(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 56U, call);
    const auto shaders = graphics_.RequireFrame("glGetAttachedShaders")
                             .GetAttachedShaders(arguments[0], arguments[1]);
    WriteWords(Pointer(prepared, 3U), shaders);
    WriteInteger(Pointer(prepared, 2U), static_cast<std::int32_t>(shaders.size()));
    return 0U;
}

std::uint32_t Gles2Module::GetShaderPrecisionFormat(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 68U, call);
    const auto result = graphics_.RequireFrame("glGetShaderPrecisionFormat")
                            .GetShaderPrecisionFormat(arguments[0], arguments[1]);
    WriteIntegers(Pointer(prepared, 2U), result.range);
    WriteInteger(Pointer(prepared, 3U), result.precision);
    return 0U;
}

std::uint32_t Gles2Module::GetShaderSource(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 69U, call);
    auto& source = Pointer(prepared, 3U);
    const auto text =
        graphics_.RequireFrame("glGetShaderSource").GetShaderSource(arguments[0], source.Size());
    const auto length = gles_io::WriteText(source, text);
    WriteInteger(Pointer(prepared, 2U), static_cast<std::int32_t>(length));
    return 0U;
}

std::uint32_t Gles2Module::GetUniformfv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 75U, call);
    auto& output = Pointer(prepared, 2U);
    const auto values = graphics_.RequireFrame("glGetUniformfv")
                            .GetUniformFloats(arguments[0], Signed(arguments[1]),
                                              output.Size() / sizeof(std::uint32_t));
    WriteFloats(output, values);
    return 0U;
}

std::uint32_t Gles2Module::GetUniformiv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 76U, call);
    auto& output = Pointer(prepared, 2U);
    const auto values = graphics_.RequireFrame("glGetUniformiv")
                            .GetUniformIntegers(arguments[0], Signed(arguments[1]),
                                                output.Size() / sizeof(std::uint32_t));
    WriteIntegers(output, values);
    return 0U;
}

std::uint32_t Gles2Module::ReleaseShaderCompiler(const A32CallFrame& call) {
    static_cast<void>(call);
    graphics_.RequireFrame("glReleaseShaderCompiler").ReleaseShaderCompiler();
    return 0U;
}

std::uint32_t Gles2Module::ShaderBinary(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 97U, call);
    const auto shaders = ReadWords(Pointer(prepared, 1U));
    graphics_.RequireFrame("glShaderBinary")
        .ShaderBinary(shaders, arguments[2], Pointer(prepared, 3U).Bytes());
    return 0U;
}

std::uint32_t Gles2Module::Uniform2f(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    const std::array values{std::bit_cast<float>(arguments[1]), std::bit_cast<float>(arguments[2])};
    graphics_.RequireFrame("glUniform2f").UniformFloats(Signed(arguments[0]), 1, 2U, values);
    return 0U;
}

std::uint32_t Gles2Module::Uniform2i(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    const std::array values{Signed(arguments[1]), Signed(arguments[2])};
    graphics_.RequireFrame("glUniform2i").UniformIntegers(Signed(arguments[0]), 1, 2U, values);
    return 0U;
}

std::uint32_t Gles2Module::Uniform3f(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    const std::array values{std::bit_cast<float>(arguments[1]), std::bit_cast<float>(arguments[2]),
                            std::bit_cast<float>(arguments[3])};
    graphics_.RequireFrame("glUniform3f").UniformFloats(Signed(arguments[0]), 1, 3U, values);
    return 0U;
}

std::uint32_t Gles2Module::Uniform3i(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    const std::array values{Signed(arguments[1]), Signed(arguments[2]), Signed(arguments[3])};
    graphics_.RequireFrame("glUniform3i").UniformIntegers(Signed(arguments[0]), 1, 3U, values);
    return 0U;
}

std::uint32_t Gles2Module::Uniform4i(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    const std::array values{Signed(arguments[1]), Signed(arguments[2]), Signed(arguments[3]),
                            Signed(arguments[4])};
    graphics_.RequireFrame("glUniform4i").UniformIntegers(Signed(arguments[0]), 1, 4U, values);
    return 0U;
}

std::uint32_t Gles2Module::UniformMatrix2fv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 127U, call);
    graphics_.RequireFrame("glUniformMatrix2fv")
        .UniformMatrix2(Signed(arguments[0]), Signed(arguments[1]), arguments[2] != 0U,
                        ReadFloats(Pointer(prepared, 3U)));
    return 0U;
}

std::uint32_t Gles2Module::ValidateProgram(const A32CallFrame& call) {
    graphics_.RequireFrame("glValidateProgram").ValidateProgram(call.Argument(0));
    return 0U;
}

} // namespace ogplay::runtime

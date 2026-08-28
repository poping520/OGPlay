#include "gles2_module.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "ogplay/gles/gles_call_preparation.h"
#include "ogplay/gles/guest_transfer.h"
#include "runtime/boundary/services/gles_transfer_io.h"

namespace ogplay::runtime {
namespace {

constexpr std::uint32_t kCurrentVertexAttribute = 0x8626U;
constexpr std::uint32_t kVertexAttributeArrayPointer = 0x8645U;

[[nodiscard]] gles::PreparedGlesCall Prepare(BoundaryCallServices& calls,
                                             GraphicsBoundaryContext& graphics,
                                             const gles::GlesThunkId id, const A32CallFrame& call) {
    return gles::PrepareGles2Call(calls.address_space, id, call.Arguments(), call.ThreadId(),
                                  &graphics.gl_context.Shared().transfer);
}

[[nodiscard]] gles::GuestBuffer& OnlyPointer(gles::PreparedGlesCall& call) {
    if (call.pointers.size() != 1U || !call.pointers.front().transfer.has_value()) {
        throw std::logic_error("GLES2 vertex handler expected one prepared pointer");
    }
    return *call.pointers.front().transfer;
}

[[nodiscard]] std::vector<std::uint32_t> ReadWords(const gles::GuestBuffer& input) {
    return gles_io::ReadWordsAligned(input,
                                     "GLES2 vertex input is not word aligned");
}

[[nodiscard]] std::vector<float> ReadFloats(const gles::GuestBuffer& input) {
    return gles_io::ValuesFromWords<float>(ReadWords(input));
}

void WriteWords(gles::GuestBuffer& output, const std::span<const std::uint32_t> words) {
    gles_io::WriteWordsExact(output, words,
                             "GLES2 vertex output has the wrong size");
}

void WriteFloats(gles::GuestBuffer& output, const std::span<const float> values) {
    WriteWords(output, gles_io::WordsFromValues(values));
}

void WriteIntegers(gles::GuestBuffer& output, const std::span<const std::int32_t> values) {
    WriteWords(output, gles_io::WordsFromValues(values));
}

void SetAttribute(GraphicsBoundaryContext& graphics, const std::uint32_t index,
                  const std::span<const float> values, const std::string_view operation) {
    if (values.empty() || values.size() > 4U) {
        throw std::logic_error("GLES2 constant attribute has an invalid shape");
    }
    std::array<float, 4> expanded{0.0F, 0.0F, 0.0F, 1.0F};
    std::ranges::copy(values, expanded.begin());
    graphics.RequireFrame(operation).VertexAttribute4f(index, expanded[0], expanded[1], expanded[2],
                                                       expanded[3]);
    graphics.gles_dispatch.SetVertexAttributeValue(index, expanded);
}

} // namespace

std::uint32_t Gles2Module::GetVertexAttribPointerv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 77U, call);
    if (arguments[1] != kVertexAttributeArrayPointer) {
        throw std::invalid_argument("glGetVertexAttribPointerv pname is unsupported");
    }
    static_cast<void>(graphics_.RequireFrame("glGetVertexAttribPointerv"));
    const auto pointer = graphics_.gles_dispatch.VertexAttributePointerIdentity(arguments[0]);
    WriteWords(OnlyPointer(prepared), std::span(&pointer, 1U));
    return 0U;
}

std::uint32_t Gles2Module::GetVertexAttribfv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 78U, call);
    auto& output = OnlyPointer(prepared);
    std::vector<float> values;
    if (arguments[1] == kCurrentVertexAttribute) {
        values = graphics_.RequireFrame("glGetVertexAttribfv")
                     .GetVertexAttributeFloats(arguments[0], arguments[1],
                                               output.Size() / sizeof(std::uint32_t));
    } else {
        static_cast<void>(graphics_.RequireFrame("glGetVertexAttribfv"));
        values.push_back(static_cast<float>(
            graphics_.gles_dispatch.VertexAttributeParameter(arguments[0], arguments[1])));
    }
    WriteFloats(output, values);
    return 0U;
}

std::uint32_t Gles2Module::GetVertexAttribiv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 79U, call);
    auto& output = OnlyPointer(prepared);
    std::vector<std::int32_t> values;
    if (arguments[1] == kCurrentVertexAttribute) {
        values = graphics_.RequireFrame("glGetVertexAttribiv")
                     .GetVertexAttributeIntegers(arguments[0], arguments[1],
                                                 output.Size() / sizeof(std::uint32_t));
    } else {
        static_cast<void>(graphics_.RequireFrame("glGetVertexAttribiv"));
        values.push_back(
            graphics_.gles_dispatch.VertexAttributeParameter(arguments[0], arguments[1]));
    }
    WriteIntegers(output, values);
    return 0U;
}

std::uint32_t Gles2Module::VertexAttrib1f(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    const std::array values{std::bit_cast<float>(arguments[1])};
    SetAttribute(graphics_, arguments[0], values, "glVertexAttrib1f");
    return 0U;
}

std::uint32_t Gles2Module::VertexAttrib1fv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 133U, call);
    SetAttribute(graphics_, arguments[0], ReadFloats(OnlyPointer(prepared)), "glVertexAttrib1fv");
    return 0U;
}

std::uint32_t Gles2Module::VertexAttrib2f(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    const std::array values{std::bit_cast<float>(arguments[1]), std::bit_cast<float>(arguments[2])};
    SetAttribute(graphics_, arguments[0], values, "glVertexAttrib2f");
    return 0U;
}

std::uint32_t Gles2Module::VertexAttrib2fv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 135U, call);
    SetAttribute(graphics_, arguments[0], ReadFloats(OnlyPointer(prepared)), "glVertexAttrib2fv");
    return 0U;
}

std::uint32_t Gles2Module::VertexAttrib3f(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    const std::array values{std::bit_cast<float>(arguments[1]), std::bit_cast<float>(arguments[2]),
                            std::bit_cast<float>(arguments[3])};
    SetAttribute(graphics_, arguments[0], values, "glVertexAttrib3f");
    return 0U;
}

std::uint32_t Gles2Module::VertexAttrib3fv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 137U, call);
    SetAttribute(graphics_, arguments[0], ReadFloats(OnlyPointer(prepared)), "glVertexAttrib3fv");
    return 0U;
}

std::uint32_t Gles2Module::VertexAttrib4fv(const A32CallFrame& call) {
    const auto arguments = call.Arguments();
    auto prepared = Prepare(calls_, graphics_, 139U, call);
    SetAttribute(graphics_, arguments[0], ReadFloats(OnlyPointer(prepared)), "glVertexAttrib4fv");
    return 0U;
}

} // namespace ogplay::runtime

#include "runtime/boundary/modules/gles1/gles1_module.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ogplay/memory/address_space.h"

namespace ogplay::runtime {
namespace {

constexpr std::uint32_t kTexture0 = 0x84C0U;

[[nodiscard]] std::uint64_t ScalarBytes(const std::uint32_t type) {
    switch (type) {
    case 0x1400U:  // GL_BYTE
    case 0x1401U: return 1U;  // GL_UNSIGNED_BYTE
    case 0x1402U:  // GL_SHORT
    case 0x1403U: return 2U;  // GL_UNSIGNED_SHORT
    case 0x1406U:  // GL_FLOAT
    case 0x140CU: return 4U;  // GL_FIXED
    default: throw std::invalid_argument("GLES1 Bounds array type is unsupported");
    }
}

[[nodiscard]] std::uint64_t ClientBytes(const std::int32_t size,
                                        const std::uint32_t type,
                                        const std::int32_t stride,
                                        const std::int32_t count) {
    if (count < 0) {
        throw std::invalid_argument("GLES1 Bounds count cannot be negative");
    }
    if (count == 0) return 0U;
    const auto packed = static_cast<std::uint64_t>(size) * ScalarBytes(type);
    const auto step = stride == 0 ? packed : static_cast<std::uint64_t>(stride);
    const auto tail = static_cast<std::uint64_t>(count - 1);
    if (tail != 0U && step >
            ((std::numeric_limits<std::uint64_t>::max)() - packed) / tail) {
        throw std::length_error("GLES1 Bounds client range overflows");
    }
    return tail * step + packed;
}

}  // namespace

std::uint32_t Gles1Module::InvokeBounds(
    const A32CallFrame& call, const std::uint32_t array,
    const std::uint32_t client_texture, const std::int32_t size,
    const std::uint32_t type, const std::int32_t stride,
    const std::uint32_t pointer, const std::int32_t count,
    const std::string_view operation) {
    auto next = draw_state_.PreparePointer(
        array, client_texture, size, type, stride, pointer,
        core_state_.TransferState().Snapshot().array_buffer);
    const auto bytes = ClientBytes(size, type, stride, count);
    if (next.buffer == 0U && pointer != 0U && bytes != 0U) {
        calls_.address_space.Validate(
            {memory::GuestAddress{pointer}, bytes}, memory::AccessType::read,
            call.ThreadId());
    }
    static_cast<void>(graphics_.RequireFrame(operation));
    draw_state_.CommitPointer(array, client_texture, next);
    return 0U;
}

std::uint32_t Gles1Module::ColorPointerBounds(const A32CallFrame& call) {
    return InvokeBounds(call, detail::kGles1ColorArray, kTexture0,
                        call.Scalar<std::int32_t>(0), call.Argument(1),
                        call.Scalar<std::int32_t>(2), call.Argument(3),
                        call.Scalar<std::int32_t>(4), "glColorPointerBounds");
}

std::uint32_t Gles1Module::NormalPointerBounds(const A32CallFrame& call) {
    return InvokeBounds(call, detail::kGles1NormalArray, kTexture0, 3,
                        call.Argument(0), call.Scalar<std::int32_t>(1),
                        call.Argument(2), call.Scalar<std::int32_t>(3),
                        "glNormalPointerBounds");
}

std::uint32_t Gles1Module::TexCoordPointerBounds(const A32CallFrame& call) {
    return InvokeBounds(call, detail::kGles1TextureCoordArray,
                        legacy_state_.ClientActiveTexture(),
                        call.Scalar<std::int32_t>(0), call.Argument(1),
                        call.Scalar<std::int32_t>(2), call.Argument(3),
                        call.Scalar<std::int32_t>(4),
                        "glTexCoordPointerBounds");
}

std::uint32_t Gles1Module::VertexPointerBounds(const A32CallFrame& call) {
    return InvokeBounds(call, detail::kGles1VertexArray, kTexture0,
                        call.Scalar<std::int32_t>(0), call.Argument(1),
                        call.Scalar<std::int32_t>(2), call.Argument(3),
                        call.Scalar<std::int32_t>(4), "glVertexPointerBounds");
}

std::uint32_t Gles1Module::PointSizePointerOesBounds(
    const A32CallFrame& call) {
    return InvokeBounds(call, detail::kGles1PointSizeArray, kTexture0, 1,
                        call.Argument(0), call.Scalar<std::int32_t>(1),
                        call.Argument(2), call.Scalar<std::int32_t>(3),
                        "glPointSizePointerOESBounds");
}

std::uint32_t Gles1Module::MatrixIndexPointerOesBounds(
    const A32CallFrame& call) {
    return InvokeBounds(call, detail::kGles1MatrixIndexArray, kTexture0,
                        call.Scalar<std::int32_t>(0), call.Argument(1),
                        call.Scalar<std::int32_t>(2), call.Argument(3),
                        call.Scalar<std::int32_t>(4),
                        "glMatrixIndexPointerOESBounds");
}

std::uint32_t Gles1Module::WeightPointerOesBounds(const A32CallFrame& call) {
    return InvokeBounds(call, detail::kGles1WeightArray, kTexture0,
                        call.Scalar<std::int32_t>(0), call.Argument(1),
                        call.Scalar<std::int32_t>(2), call.Argument(3),
                        call.Scalar<std::int32_t>(4),
                        "glWeightPointerOESBounds");
}

}  // namespace ogplay::runtime

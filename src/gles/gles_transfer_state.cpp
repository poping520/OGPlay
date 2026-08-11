#include "ogplay/gles/gles_transfer_state.h"

#include <array>
#include <limits>
#include <string_view>

namespace ogplay::gles {
namespace {

constexpr std::uint32_t kPackAlignment = 0x0D05;
constexpr std::uint32_t kUnpackAlignment = 0x0CF5;
constexpr std::uint32_t kArrayBuffer = 0x8892;
constexpr std::uint32_t kElementArrayBuffer = 0x8893;

[[nodiscard]] std::uint64_t CheckedMultiply(const std::uint64_t left,
                                            const std::uint64_t right) {
    if (left != 0 && right > (std::numeric_limits<std::uint64_t>::max)() / left) {
        throw GlesTransferStateError("GLES transfer size multiplication overflow");
    }
    return left * right;
}

[[nodiscard]] std::uint64_t CheckedAdd(const std::uint64_t left,
                                       const std::uint64_t right) {
    if (right > (std::numeric_limits<std::uint64_t>::max)() - left) {
        throw GlesTransferStateError("GLES transfer size addition overflow");
    }
    return left + right;
}

[[nodiscard]] std::uint64_t AlignUp(const std::uint64_t value,
                                    const std::uint32_t alignment) {
    const auto mask = static_cast<std::uint64_t>(alignment - 1U);
    if (value > (std::numeric_limits<std::uint64_t>::max)() - mask) {
        throw GlesTransferStateError("GLES pixel row alignment overflow");
    }
    return (value + mask) & ~mask;
}

[[nodiscard]] std::uint64_t Components(const std::uint32_t format) {
    switch (format) {
    case 0x1906:  // GL_ALPHA
    case 0x1909:  // GL_LUMINANCE
        return 1;
    case 0x190A:  // GL_LUMINANCE_ALPHA
        return 2;
    case 0x1907:  // GL_RGB
        return 3;
    case 0x1908:  // GL_RGBA
    case 0x80E1:  // GL_BGRA_EXT
        return 4;
    default:
        throw GlesTransferStateError("unsupported GLES pixel format");
    }
}

[[nodiscard]] std::uint64_t PixelSize(const std::uint32_t format,
                                      const std::uint32_t type) {
    switch (type) {
    case 0x1401:  // GL_UNSIGNED_BYTE
        return Components(format);
    case 0x1403:  // GL_UNSIGNED_SHORT
    case 0x8D61:  // GL_HALF_FLOAT_OES
        return CheckedMultiply(Components(format), 2);
    case 0x1405:  // GL_UNSIGNED_INT
    case 0x1406:  // GL_FLOAT
        return CheckedMultiply(Components(format), 4);
    case 0x8363:  // GL_UNSIGNED_SHORT_5_6_5
        if (format != 0x1907) {
            throw GlesTransferStateError("GLES 5_6_5 pixels require GL_RGB");
        }
        return 2;
    case 0x8033:  // GL_UNSIGNED_SHORT_4_4_4_4
    case 0x8034:  // GL_UNSIGNED_SHORT_5_5_5_1
        if (format != 0x1908 && format != 0x80E1) {
            throw GlesTransferStateError(
                "GLES packed RGBA pixels require GL_RGBA or GL_BGRA_EXT");
        }
        return 2;
    default:
        throw GlesTransferStateError("unsupported GLES pixel type");
    }
}

[[nodiscard]] std::uint64_t PixelBytes(
    const std::int32_t width, const std::int32_t height,
    const std::uint32_t format, const std::uint32_t type,
    const std::uint32_t alignment) {
    if (width < 0 || height < 0) {
        throw GlesTransferStateError("GLES pixel dimensions must not be negative");
    }
    if (width == 0 || height == 0) {
        return 0;
    }
    const auto row = CheckedMultiply(static_cast<std::uint32_t>(width),
                                     PixelSize(format, type));
    const auto stride = AlignUp(row, alignment);
    return CheckedAdd(
        CheckedMultiply(static_cast<std::uint32_t>(height - 1), stride), row);
}

[[nodiscard]] std::uint64_t IndexSize(const std::uint32_t type) {
    switch (type) {
    case 0x1401:  // GL_UNSIGNED_BYTE
        return 1;
    case 0x1403:  // GL_UNSIGNED_SHORT
        return 2;
    case 0x1405:  // GL_UNSIGNED_INT / OES_element_index_uint
        return 4;
    default:
        throw GlesTransferStateError("unsupported GLES element index type");
    }
}

[[nodiscard]] std::uint64_t BuiltinQueryCount(const std::uint32_t pname) {
    switch (pname) {
    case 0x0B70:  // GL_DEPTH_RANGE
    case 0x0D3A:  // GL_MAX_VIEWPORT_DIMS
    case 0x846D:  // GL_ALIASED_POINT_SIZE_RANGE
    case 0x846E:  // GL_ALIASED_LINE_WIDTH_RANGE
        return 2;
    case 0x0BA2:  // GL_VIEWPORT
    case 0x0C10:  // GL_SCISSOR_BOX
    case 0x0C22:  // GL_COLOR_CLEAR_VALUE
    case 0x0C23:  // GL_COLOR_WRITEMASK
    case 0x8005:  // GL_BLEND_COLOR
        return 4;
    default:
        return 1;
    }
}

[[nodiscard]] bool IsKnownScalarQuery(const std::uint32_t pname) noexcept {
    constexpr std::array known{
        UINT32_C(0x0B44), UINT32_C(0x0B45), UINT32_C(0x0B46),
        UINT32_C(0x0B71), UINT32_C(0x0B72), UINT32_C(0x0B73),
        UINT32_C(0x0B74), UINT32_C(0x0B90), UINT32_C(0x0B91),
        UINT32_C(0x0B92), UINT32_C(0x0B94), UINT32_C(0x0B95),
        UINT32_C(0x0B97), UINT32_C(0x0BA2), UINT32_C(0x0C10),
        UINT32_C(0x0C11), UINT32_C(0x0C22), UINT32_C(0x0C23),
        UINT32_C(0x0CF5), UINT32_C(0x0D05), UINT32_C(0x0D33),
        UINT32_C(0x0D3A), UINT32_C(0x0D50), UINT32_C(0x0D52),
        UINT32_C(0x0D53), UINT32_C(0x0D54), UINT32_C(0x0D55),
        UINT32_C(0x0D56), UINT32_C(0x0D57), UINT32_C(0x0D70),
        UINT32_C(0x0D80), UINT32_C(0x0D81), UINT32_C(0x8005),
        UINT32_C(0x80A8), UINT32_C(0x80A9), UINT32_C(0x80AA),
        UINT32_C(0x80AB), UINT32_C(0x846D), UINT32_C(0x846E),
        UINT32_C(0x86A2), UINT32_C(0x86A4),
        UINT32_C(0x8069), UINT32_C(0x84E0), UINT32_C(0x8514),
        UINT32_C(0x86A5),
        UINT32_C(0x8869), UINT32_C(0x8894), UINT32_C(0x8895),
        UINT32_C(0x8B4C),
        UINT32_C(0x8B4D), UINT32_C(0x8B4F), UINT32_C(0x8B80),
        UINT32_C(0x8B8D), UINT32_C(0x8CA6), UINT32_C(0x8CA7),
        UINT32_C(0x8CA8), UINT32_C(0x8CA9), UINT32_C(0x8CDF),
        UINT32_C(0x8D40), UINT32_C(0x8DF9),
    };
    for (const auto candidate : known) {
        if (candidate == pname) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::uint64_t VertexAttribCount(const std::uint32_t pname) {
    switch (pname) {
    case 0x8626:  // GL_CURRENT_VERTEX_ATTRIB
        return 4;
    case 0x8622:  // GL_VERTEX_ATTRIB_ARRAY_ENABLED
    case 0x8623:  // GL_VERTEX_ATTRIB_ARRAY_SIZE
    case 0x8624:  // GL_VERTEX_ATTRIB_ARRAY_STRIDE
    case 0x8625:  // GL_VERTEX_ATTRIB_ARRAY_TYPE
    case 0x886A:  // GL_VERTEX_ATTRIB_ARRAY_NORMALIZED
    case 0x889F:  // GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING
        return 1;
    default:
        throw GlesTransferStateError("unsupported GLES vertex attribute query");
    }
}

[[nodiscard]] std::uint64_t CoreTextureParameterCount(
    const std::uint32_t pname) {
    switch (pname) {
    case 0x2800:  // GL_TEXTURE_MAG_FILTER
    case 0x2801:  // GL_TEXTURE_MIN_FILTER
    case 0x2802:  // GL_TEXTURE_WRAP_S
    case 0x2803:  // GL_TEXTURE_WRAP_T
        return 1;
    default:
        throw GlesTransferStateError("unsupported GLES texture parameter shape");
    }
}

}  // namespace

void GlesTransferState::PixelStore(const std::uint32_t pname,
                                   const std::int32_t alignment) {
    if (alignment != 1 && alignment != 2 && alignment != 4 && alignment != 8) {
        throw GlesTransferStateError("GLES pixel alignment must be 1, 2, 4 or 8");
    }
    if (pname == kPackAlignment) {
        pack_alignment_ = static_cast<std::uint32_t>(alignment);
    } else if (pname == kUnpackAlignment) {
        unpack_alignment_ = static_cast<std::uint32_t>(alignment);
    } else {
        throw GlesTransferStateError("unsupported GLES pixel store name");
    }
}

void GlesTransferState::BindBuffer(const std::uint32_t target,
                                   const std::uint32_t buffer) {
    if (target == kArrayBuffer) {
        array_buffer_ = buffer;
    } else if (target == kElementArrayBuffer) {
        element_array_buffer_ = buffer;
    } else {
        throw GlesTransferStateError("unsupported GLES transfer buffer target");
    }
}

void GlesTransferState::SetQueryElementCount(const std::uint32_t pname,
                                             const std::uint64_t count) {
    if (count == 0) {
        throw GlesTransferStateError("GLES query element count must not be zero");
    }
    query_counts_[pname] = count;
}

void GlesTransferState::SetUniformElementCount(const std::uint32_t program,
                                               const std::int32_t location,
                                               const std::uint64_t count) {
    if (count == 0) {
        throw GlesTransferStateError("GLES uniform element count must not be zero");
    }
    uniform_counts_[{program, location}] = count;
}

void GlesTransferState::ClearUniformElementCounts(
    const std::uint32_t program) noexcept {
    for (auto iterator = uniform_counts_.begin(); iterator != uniform_counts_.end();) {
        if (iterator->first.first == program) {
            iterator = uniform_counts_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

GlesTransferStateSnapshot GlesTransferState::Snapshot() const noexcept {
    return {.pack_alignment = pack_alignment_,
            .unpack_alignment = unpack_alignment_,
            .array_buffer = array_buffer_,
            .element_array_buffer = element_array_buffer_,
            .query_shapes = query_counts_.size(),
            .uniform_shapes = uniform_counts_.size()};
}

std::optional<GlesLengthResolution> GlesTransferState::Resolve(
    const GlesLengthRequest& request) const {
    if (request.expression == "pixel_bytes(width,height,format,type)") {
        std::size_t width_index{};
        std::size_t format_index{};
        std::size_t type_index{};
        std::uint32_t alignment = unpack_alignment_;
        if (request.function_name == "glReadPixels") {
            width_index = 2;
            format_index = 4;
            type_index = 5;
            alignment = pack_alignment_;
        } else if (request.function_name == "glTexImage2D") {
            width_index = 3;
            format_index = 6;
            type_index = 7;
        } else if (request.function_name == "glTexSubImage2D") {
            width_index = 4;
            format_index = 6;
            type_index = 7;
        } else {
            throw GlesTransferStateError("pixel length used by an unknown GLES call");
        }
        if (request.arguments.size() <= type_index) {
            throw GlesTransferStateError("GLES pixel call has an invalid argument shape");
        }
        return GlesLengthResolution{
            GlesLengthDisposition::transfer,
            PixelBytes(static_cast<std::int32_t>(request.arguments[width_index]),
                       static_cast<std::int32_t>(request.arguments[width_index + 1]),
                       request.arguments[format_index],
                       request.arguments[type_index], alignment)};
    }
    if (request.expression == "index_bytes(count,type)") {
        if (request.arguments.size() != 4) {
            throw GlesTransferStateError("GLES indexed draw has an invalid argument shape");
        }
        if (element_array_buffer_ != 0) {
            return GlesLengthResolution{GlesLengthDisposition::deferred, 0};
        }
        const auto count = static_cast<std::int32_t>(request.arguments[1]);
        if (count < 0) {
            throw GlesTransferStateError("GLES index count must not be negative");
        }
        return GlesLengthResolution{
            GlesLengthDisposition::transfer,
            CheckedMultiply(static_cast<std::uint32_t>(count),
                            IndexSize(request.arguments[2]))};
    }
    if (request.expression == "client_array(index,size,type,stride)") {
        return GlesLengthResolution{GlesLengthDisposition::deferred, 0};
    }
    if (request.expression == "state_count(pname)") {
        if (request.arguments.empty()) {
            throw GlesTransferStateError("GLES state query has no pname");
        }
        const auto pname = request.arguments[0];
        if (const auto found = query_counts_.find(pname);
            found != query_counts_.end()) {
            return GlesLengthResolution{GlesLengthDisposition::transfer,
                                        found->second};
        }
        if (!IsKnownScalarQuery(pname)) {
            throw GlesTransferStateError("unsupported GLES state query");
        }
        return GlesLengthResolution{GlesLengthDisposition::transfer,
                                    BuiltinQueryCount(pname)};
    }
    if (request.expression == "tex_parameter_count(pname)") {
        if (request.arguments.size() < 2) {
            throw GlesTransferStateError("GLES texture parameter has no pname");
        }
        const auto pname = request.arguments[1];
        if (const auto found = query_counts_.find(pname);
            found != query_counts_.end()) {
            return GlesLengthResolution{GlesLengthDisposition::transfer,
                                        found->second};
        }
        return GlesLengthResolution{GlesLengthDisposition::transfer,
                                    CoreTextureParameterCount(pname)};
    }
    if (request.expression == "vertex_attrib_count(pname)") {
        if (request.arguments.size() < 2) {
            throw GlesTransferStateError("GLES vertex query has no pname");
        }
        return GlesLengthResolution{GlesLengthDisposition::transfer,
                                    VertexAttribCount(request.arguments[1])};
    }
    if (request.expression == "uniform_value_count(program,location)") {
        if (request.arguments.size() < 2) {
            throw GlesTransferStateError("GLES uniform query has invalid arguments");
        }
        const auto key = std::pair{
            request.arguments[0], static_cast<std::int32_t>(request.arguments[1])};
        if (const auto found = uniform_counts_.find(key);
            found != uniform_counts_.end()) {
            return GlesLengthResolution{GlesLengthDisposition::transfer,
                                        found->second};
        }
        throw GlesTransferStateError("GLES uniform shape is not registered");
    }
    return std::nullopt;
}

}  // namespace ogplay::gles

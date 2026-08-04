#include "ogplay/loader/dex.h"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace ogplay::loader {
namespace {

constexpr std::uint32_t kDexHeaderSize = 0x70;
constexpr std::uint32_t kDexEndianConstant = 0x12345678;

[[noreturn]] void Fail(const DexErrorReason reason, const std::size_t offset,
                       std::string message) {
    throw DexError(reason, offset, std::move(message));
}

class Reader final {
public:
    explicit Reader(const std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::uint16_t U16(const std::size_t offset) const {
        Require(offset, 2);
        return static_cast<std::uint16_t>(bytes_[offset]) |
               static_cast<std::uint16_t>(bytes_[offset + 1]) << 8U;
    }

    [[nodiscard]] std::uint32_t U32(const std::size_t offset) const {
        Require(offset, 4);
        return static_cast<std::uint32_t>(bytes_[offset]) |
               static_cast<std::uint32_t>(bytes_[offset + 1]) << 8U |
               static_cast<std::uint32_t>(bytes_[offset + 2]) << 16U |
               static_cast<std::uint32_t>(bytes_[offset + 3]) << 24U;
    }

    void Require(const std::size_t offset, const std::size_t size) const {
        if (offset > bytes_.size() || size > bytes_.size() - offset) {
            Fail(DexErrorReason::truncated, offset, "DEX input is truncated");
        }
    }

private:
    std::span<const std::uint8_t> bytes_;
};

void RequireRange(const std::uint32_t offset, const std::uint32_t count,
                  const std::uint32_t item_size,
                  const std::size_t file_size, const char* name) {
    if ((count == 0) != (offset == 0)) {
        Fail(DexErrorReason::invalid_range, offset,
             std::string("DEX ") + name + " size/offset pair is inconsistent");
    }
    if (count == 0) return;
    if ((offset & 3U) != 0) {
        Fail(DexErrorReason::invalid_range, offset,
             std::string("DEX ") + name + " offset is not aligned");
    }
    const auto bytes = static_cast<std::uint64_t>(count) * item_size;
    if (offset > file_size || bytes > file_size - offset) {
        Fail(DexErrorReason::invalid_range, offset,
             std::string("DEX ") + name + " range exceeds file");
    }
}

[[nodiscard]] std::optional<std::uint32_t> FixedItemSize(
    const std::uint16_t type) {
    switch (static_cast<DexMapItemType>(type)) {
    case DexMapItemType::header:
        return 0x70;
    case DexMapItemType::string_id:
    case DexMapItemType::type_id:
    case DexMapItemType::call_site_id:
        return 4;
    case DexMapItemType::proto_id:
        return 12;
    case DexMapItemType::field_id:
    case DexMapItemType::method_id:
    case DexMapItemType::method_handle:
        return 8;
    case DexMapItemType::class_def:
        return 32;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] DexHeader ReadHeader(const Reader& reader,
                                   const std::span<const std::uint8_t> bytes) {
    reader.Require(0, kDexHeaderSize);
    if (bytes[0] != 'd' || bytes[1] != 'e' || bytes[2] != 'x' ||
        bytes[3] != '\n' || bytes[7] != 0) {
        Fail(DexErrorReason::invalid_magic, 0, "DEX magic is invalid");
    }
    const std::string version(bytes.begin() + 4, bytes.begin() + 7);
    if (version < "035" || version > "040") {
        Fail(DexErrorReason::unsupported_version, 4,
             "DEX version is outside the supported 035..040 range");
    }

    DexHeader header;
    header.version = version;
    header.checksum = reader.U32(8);
    std::copy_n(bytes.begin() + 12, header.signature.size(),
                header.signature.begin());
    header.file_size = reader.U32(32);
    header.header_size = reader.U32(36);
    header.endian_tag = reader.U32(40);
    header.link_size = reader.U32(44);
    header.link_offset = reader.U32(48);
    header.map_offset = reader.U32(52);
    header.string_ids_size = reader.U32(56);
    header.string_ids_offset = reader.U32(60);
    header.type_ids_size = reader.U32(64);
    header.type_ids_offset = reader.U32(68);
    header.proto_ids_size = reader.U32(72);
    header.proto_ids_offset = reader.U32(76);
    header.field_ids_size = reader.U32(80);
    header.field_ids_offset = reader.U32(84);
    header.method_ids_size = reader.U32(88);
    header.method_ids_offset = reader.U32(92);
    header.class_defs_size = reader.U32(96);
    header.class_defs_offset = reader.U32(100);
    header.data_size = reader.U32(104);
    header.data_offset = reader.U32(108);
    return header;
}

void ValidateHeader(const DexHeader& header, const std::size_t file_size) {
    if (header.file_size != file_size || header.header_size != kDexHeaderSize) {
        Fail(DexErrorReason::invalid_header, 32,
             "DEX file or header size is invalid");
    }
    if (header.endian_tag != kDexEndianConstant) {
        Fail(DexErrorReason::invalid_endian, 40,
             "DEX reverse or unknown endian tag is unsupported");
    }
    RequireRange(header.link_offset, header.link_size, 1, file_size, "link");
    RequireRange(header.string_ids_offset, header.string_ids_size, 4,
                 file_size, "string_ids");
    RequireRange(header.type_ids_offset, header.type_ids_size, 4,
                 file_size, "type_ids");
    RequireRange(header.proto_ids_offset, header.proto_ids_size, 12,
                 file_size, "proto_ids");
    RequireRange(header.field_ids_offset, header.field_ids_size, 8,
                 file_size, "field_ids");
    RequireRange(header.method_ids_offset, header.method_ids_size, 8,
                 file_size, "method_ids");
    RequireRange(header.class_defs_offset, header.class_defs_size, 32,
                 file_size, "class_defs");
    RequireRange(header.data_offset, header.data_size, 1, file_size, "data");
    if (header.data_size == 0 || header.map_offset < header.data_offset ||
        header.map_offset >= file_size || (header.map_offset & 3U) != 0) {
        Fail(DexErrorReason::invalid_header, 52,
             "DEX map must be aligned inside the data section");
    }
    const auto data_end = static_cast<std::uint64_t>(header.data_offset) +
                          header.data_size;
    if (data_end != file_size || header.map_offset >= data_end) {
        Fail(DexErrorReason::invalid_range, header.data_offset,
             "DEX data section must end at file boundary");
    }
}

[[nodiscard]] std::vector<DexMapItem> ReadMap(
    const Reader& reader, const DexHeader& header,
    const std::size_t file_size) {
    const auto count = reader.U32(header.map_offset);
    if (count == 0 || count >
                          (std::numeric_limits<std::uint32_t>::max() - 4U) /
                              12U) {
        Fail(DexErrorReason::invalid_map, header.map_offset,
             "DEX map count is invalid");
    }
    const auto map_bytes = 4ULL + static_cast<std::uint64_t>(count) * 12ULL;
    if (map_bytes > file_size - header.map_offset) {
        Fail(DexErrorReason::invalid_map, header.map_offset,
             "DEX map range exceeds file");
    }

    std::vector<DexMapItem> items;
    items.reserve(count);
    std::map<std::uint16_t, std::size_t> seen;
    std::uint32_t previous_offset{};
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto at = static_cast<std::size_t>(header.map_offset) + 4ULL +
                        static_cast<std::size_t>(index) * 12ULL;
        const auto type = reader.U16(at);
        const auto unused = reader.U16(at + 2);
        const auto size = reader.U32(at + 4);
        const auto offset = reader.U32(at + 8);
        if (unused != 0 || size == 0 || !seen.emplace(type, at).second ||
            (index != 0 && offset <= previous_offset) || offset >= file_size) {
            Fail(DexErrorReason::invalid_map, at,
                 "DEX map item is duplicate, unordered or malformed");
        }
        if (const auto fixed = FixedItemSize(type); fixed.has_value()) {
            const auto bytes = static_cast<std::uint64_t>(size) * *fixed;
            if ((offset & 3U) != 0 || bytes > file_size - offset) {
                Fail(DexErrorReason::invalid_map, at,
                     "DEX fixed map item range is invalid");
            }
        }
        items.push_back({type, size, offset});
        previous_offset = offset;
    }
    return items;
}

void RequireMapMatch(const DexImage& image, const DexMapItemType type,
                     const std::uint32_t size, const std::uint32_t offset) {
    const auto found = image.FindMapItem(type);
    if (size == 0) {
        if (found.has_value()) {
            Fail(DexErrorReason::invalid_map, found->offset,
                 "empty DEX header section appears in map");
        }
        return;
    }
    if (!found.has_value() || found->size != size || found->offset != offset) {
        Fail(DexErrorReason::invalid_map, offset,
             "DEX header and map section facts disagree");
    }
}

void ValidateMap(const DexImage& image) {
    RequireMapMatch(image, DexMapItemType::header, 1, 0);
    RequireMapMatch(image, DexMapItemType::string_id,
                    image.header.string_ids_size,
                    image.header.string_ids_offset);
    RequireMapMatch(image, DexMapItemType::type_id,
                    image.header.type_ids_size,
                    image.header.type_ids_offset);
    RequireMapMatch(image, DexMapItemType::proto_id,
                    image.header.proto_ids_size,
                    image.header.proto_ids_offset);
    RequireMapMatch(image, DexMapItemType::field_id,
                    image.header.field_ids_size,
                    image.header.field_ids_offset);
    RequireMapMatch(image, DexMapItemType::method_id,
                    image.header.method_ids_size,
                    image.header.method_ids_offset);
    RequireMapMatch(image, DexMapItemType::class_def,
                    image.header.class_defs_size,
                    image.header.class_defs_offset);
    RequireMapMatch(image, DexMapItemType::map_list, 1,
                    image.header.map_offset);
}

}  // namespace

DexError::DexError(const DexErrorReason reason, const std::size_t offset,
                   std::string message)
    : std::runtime_error(std::move(message)), reason_(reason), offset_(offset) {}
DexErrorReason DexError::Reason() const noexcept { return reason_; }
std::size_t DexError::Offset() const noexcept { return offset_; }

std::optional<DexMapItem> DexImage::FindMapItem(
    const DexMapItemType type) const noexcept {
    const auto value = static_cast<std::uint16_t>(type);
    const auto found = std::find_if(
        map_items.begin(), map_items.end(),
        [value](const DexMapItem& item) { return item.type == value; });
    return found == map_items.end() ? std::nullopt
                                    : std::optional<DexMapItem>{*found};
}

DexImage ParseDex(const std::span<const std::uint8_t> bytes) {
    const Reader reader(bytes);
    DexImage image;
    image.header = ReadHeader(reader, bytes);
    ValidateHeader(image.header, bytes.size());
    image.map_items = ReadMap(reader, image.header, bytes.size());
    ValidateMap(image);
    return image;
}

}  // namespace ogplay::loader

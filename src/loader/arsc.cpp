#include "ogplay/loader/arsc.h"

#include <algorithm>

namespace ogplay::loader {
namespace {

constexpr std::uint16_t kResStringPoolType = 0x0001;
constexpr std::uint16_t kResTableType = 0x0002;
constexpr std::uint16_t kResTablePackageType = 0x0200;
constexpr std::uint16_t kResTableTypeType = 0x0201;
constexpr std::uint16_t kResTableTypeSpecType = 0x0202;
// KitKat-era tables may also carry library chunks; skipped structurally.

constexpr std::uint32_t kNoEntry = 0xFFFFFFFFU;
constexpr std::uint32_t kStringPoolUtf8Flag = 1U << 8U;
constexpr std::uint8_t kValueTypeString = 0x03;
constexpr std::uint16_t kEntryFlagComplex = 0x0001;

[[noreturn]] void Fail(const std::size_t offset, const char* message) {
    throw DexError(DexErrorReason::invalid_range, offset, message);
}

class Reader final {
public:
    explicit Reader(const std::span<const std::uint8_t> bytes)
        : bytes_(bytes) {}

    [[nodiscard]] std::uint8_t U8(const std::size_t offset) const {
        Require(offset, 1);
        return bytes_[offset];
    }
    [[nodiscard]] std::uint16_t U16(const std::size_t offset) const {
        Require(offset, 2);
        return static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(bytes_[offset]) |
            static_cast<std::uint32_t>(bytes_[offset + 1]) << 8U);
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
            Fail(offset, "resources.arsc chunk is truncated");
        }
    }
    [[nodiscard]] std::size_t Size() const noexcept { return bytes_.size(); }

private:
    std::span<const std::uint8_t> bytes_;
};

struct Chunk final {
    std::uint16_t type{};
    std::uint16_t header_size{};
    std::uint32_t size{};
    std::size_t offset{};

    [[nodiscard]] std::size_t Body() const { return offset + header_size; }
    [[nodiscard]] std::size_t End() const { return offset + size; }
};

[[nodiscard]] Chunk ReadChunk(const Reader& reader, const std::size_t offset) {
    Chunk chunk;
    chunk.type = reader.U16(offset);
    chunk.header_size = reader.U16(offset + 2);
    chunk.size = reader.U32(offset + 4);
    chunk.offset = offset;
    if (chunk.size < chunk.header_size || chunk.header_size < 8) {
        Fail(offset, "resources.arsc chunk sizes are inconsistent");
    }
    reader.Require(offset, chunk.size);
    return chunk;
}

[[nodiscard]] std::vector<std::string> ReadStringPool(const Reader& reader,
                                                      const Chunk& chunk) {
    const auto base = chunk.offset;
    const auto string_count = reader.U32(base + 8);
    const auto flags = reader.U32(base + 16);
    const auto strings_start = reader.U32(base + 20);
    const bool utf8 = (flags & kStringPoolUtf8Flag) != 0;
    if (string_count > 0x100000U) {
        Fail(base, "resources.arsc string pool is implausibly large");
    }
    std::vector<std::string> strings;
    strings.reserve(string_count);
    for (std::uint32_t index = 0; index < string_count; ++index) {
        const auto entry_offset =
            reader.U32(base + 28 + static_cast<std::size_t>(index) * 4);
        std::size_t cursor = base + strings_start + entry_offset;
        if (cursor >= chunk.End()) {
            Fail(cursor, "resources.arsc string offset escapes the pool");
        }
        std::string value;
        if (utf8) {
            // UTF-8 pools store (utf16 length, utf8 length) as 1-2 bytes each.
            const auto skip_length = [&](std::size_t& at) -> std::uint32_t {
                std::uint32_t length = reader.U8(at++);
                if ((length & 0x80U) != 0) {
                    length = ((length & 0x7fU) << 8U) | reader.U8(at++);
                }
                return length;
            };
            static_cast<void>(skip_length(cursor));
            const auto byte_length = skip_length(cursor);
            reader.Require(cursor, byte_length);
            for (std::uint32_t byte = 0; byte < byte_length; ++byte) {
                value.push_back(
                    static_cast<char>(reader.U8(cursor + byte)));
            }
        } else {
            std::uint32_t length = reader.U16(cursor);
            cursor += 2;
            if ((length & 0x8000U) != 0) {
                length = ((length & 0x7fffU) << 16U) | reader.U16(cursor);
                cursor += 2;
            }
            reader.Require(cursor, static_cast<std::size_t>(length) * 2);
            for (std::uint32_t unit = 0; unit < length; ++unit) {
                const auto code = reader.U16(cursor + unit * 2);
                if (code < 0x80) {
                    value.push_back(static_cast<char>(code));
                } else {
                    value.push_back('?');
                }
            }
        }
        strings.push_back(std::move(value));
    }
    return strings;
}

[[nodiscard]] std::string ReadPackageName(const Reader& reader,
                                          const std::size_t offset) {
    std::string name;
    for (std::size_t unit = 0; unit < 128; ++unit) {
        const auto code = reader.U16(offset + unit * 2);
        if (code == 0) break;
        name.push_back(code < 0x80 ? static_cast<char>(code) : '?');
    }
    return name;
}

}  // namespace

const ArscEntry* ArscTable::FindById(
    const std::uint32_t resource_id) const noexcept {
    for (const auto& entry : entries) {
        if (entry.resource_id == resource_id) return &entry;
    }
    return nullptr;
}

const ArscEntry* ArscTable::FindByName(
    const std::string_view type_name,
    const std::string_view entry_name) const noexcept {
    for (const auto& entry : entries) {
        if (entry.type_name == type_name && entry.entry_name == entry_name) {
            return &entry;
        }
    }
    return nullptr;
}

ArscTable ParseArsc(const std::span<const std::uint8_t> bytes) {
    const Reader reader(bytes);
    const auto table = ReadChunk(reader, 0);
    if (table.type != kResTableType) {
        Fail(0, "resources.arsc does not start with a resource table chunk");
    }
    const auto package_count = reader.U32(8);
    if (package_count == 0) {
        Fail(8, "resources.arsc declares no packages");
    }

    ArscTable result;
    std::vector<std::string> global_strings;

    std::size_t cursor = table.Body();
    while (cursor < table.End()) {
        const auto chunk = ReadChunk(reader, cursor);
        if (chunk.type == kResStringPoolType && global_strings.empty()) {
            global_strings = ReadStringPool(reader, chunk);
        } else if (chunk.type == kResTablePackageType) {
            const auto package_base = chunk.offset;
            result.package_id = reader.U32(package_base + 8);
            result.package_name = ReadPackageName(reader, package_base + 12);
            const auto type_strings_offset =
                reader.U32(package_base + 268);
            // key strings offset at +276.
            std::vector<std::string> type_names;
            std::vector<std::string> key_names;
            std::size_t inner = package_base + chunk.header_size;
            if (type_strings_offset != 0) {
                const auto pool = ReadChunk(
                    reader, package_base + type_strings_offset);
                type_names = ReadStringPool(reader, pool);
            }
            const auto key_strings_offset = reader.U32(package_base + 276);
            if (key_strings_offset != 0) {
                const auto pool = ReadChunk(
                    reader, package_base + key_strings_offset);
                key_names = ReadStringPool(reader, pool);
            }
            while (inner < chunk.End()) {
                const auto sub = ReadChunk(reader, inner);
                if (sub.type == kResTableTypeType) {
                    const auto type_id = reader.U8(sub.offset + 8);
                    const auto entry_count = reader.U32(sub.offset + 12);
                    const auto entries_start = reader.U32(sub.offset + 16);
                    if (type_id == 0 || type_id > type_names.size()) {
                        Fail(sub.offset,
                             "resources.arsc type id is out of range");
                    }
                    for (std::uint32_t index = 0; index < entry_count;
                         ++index) {
                        const auto entry_offset = reader.U32(
                            sub.Body() + static_cast<std::size_t>(index) * 4);
                        if (entry_offset == kNoEntry) continue;
                        const auto entry_base =
                            sub.offset + entries_start + entry_offset;
                        const auto entry_flags = reader.U16(entry_base + 2);
                        const auto key_index = reader.U32(entry_base + 4);
                        if (key_index >= key_names.size()) {
                            Fail(entry_base,
                                 "resources.arsc entry key is out of range");
                        }
                        ArscEntry entry;
                        entry.resource_id =
                            (result.package_id << 24U) |
                            (static_cast<std::uint32_t>(type_id) << 16U) |
                            index;
                        entry.type_name = type_names[type_id - 1];
                        entry.entry_name = key_names[key_index];
                        if ((entry_flags & kEntryFlagComplex) == 0) {
                            const auto value_base = entry_base + 8;
                            const auto data_type =
                                reader.U8(value_base + 3);
                            const auto data = reader.U32(value_base + 4);
                            if (data_type == kValueTypeString &&
                                data < global_strings.size()) {
                                entry.string_value = global_strings[data];
                            }
                        }
                        // First (default) configuration wins; later configs
                        // for the same id are ignored.
                        if (result.FindById(entry.resource_id) == nullptr) {
                            result.entries.push_back(std::move(entry));
                        }
                    }
                } else if (sub.type != kResTableTypeSpecType &&
                           sub.type != kResStringPoolType) {
                    // Library/overlay chunks are structurally skipped.
                }
                inner = sub.End();
            }
        }
        cursor = chunk.End();
    }
    if (result.package_id == 0) {
        Fail(0, "resources.arsc contains no package chunk");
    }
    return result;
}

}  // namespace ogplay::loader

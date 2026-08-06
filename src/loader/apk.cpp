#include "ogplay/loader/apk.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace ogplay::loader {
namespace {

constexpr std::uint32_t kLocalSignature = 0x04034b50;
constexpr std::uint32_t kCentralSignature = 0x02014b50;
constexpr std::uint32_t kEndSignature = 0x06054b50;
constexpr std::uint32_t kDataDescriptorSignature = 0x08074b50;
constexpr std::size_t kEndSize = 22;
constexpr std::size_t kMaxCommentSize = 65535;
constexpr std::uint16_t kStoredMethod = 0;
constexpr std::uint16_t kDeflateMethod = 8;

void RequireRange(const std::span<const std::byte> bytes, const std::size_t offset,
                  const std::size_t size, const std::string_view what) {
    if (offset > bytes.size() || size > bytes.size() - offset) {
        throw std::runtime_error(std::string(what) + " is outside APK bytes");
    }
}

std::uint16_t Read16(const std::span<const std::byte> bytes, const std::size_t offset) {
    RequireRange(bytes, offset, 2, "ZIP field");
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8U);
}

std::uint32_t Read32(const std::span<const std::byte> bytes, const std::size_t offset) {
    RequireRange(bytes, offset, 4, "ZIP field");
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << static_cast<unsigned>(index * 8);
    }
    return value;
}

std::string ReadName(const std::span<const std::byte> bytes, const std::size_t offset,
                     const std::size_t size) {
    RequireRange(bytes, offset, size, "ZIP entry name");
    std::string name;
    name.reserve(size);
    for (std::size_t index = 0; index < size; ++index) {
        const auto value = std::to_integer<std::uint8_t>(bytes[offset + index]);
        if (value == 0 || value >= 0x80) {
            throw std::runtime_error("APK entry name must be non-empty ASCII without NUL");
        }
        name.push_back(static_cast<char>(value));
    }
    if (name.empty() || name.front() == '/' || name.front() == '\\') {
        throw std::runtime_error("APK entry name is empty or absolute");
    }
    std::size_t begin{};
    while (begin < name.size()) {
        const auto end = name.find_first_of("/\\", begin);
        const auto segment = name.substr(begin, end == std::string::npos ? end : end - begin);
        if (segment == ".." || segment == ".") {
            throw std::runtime_error("APK entry name contains an unsafe path segment");
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return name;
}

std::uint32_t Crc32(const std::span<const std::byte> bytes) noexcept {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

class DeflateBitReader final {
  public:
    explicit DeflateBitReader(const std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::uint32_t Read(const unsigned count) {
        if (count > 15 || bit_offset_ > bytes_.size() * 8U ||
            count > bytes_.size() * 8U - bit_offset_) {
            throw std::runtime_error("APK deflate stream is truncated");
        }
        std::uint32_t value{};
        for (unsigned bit = 0; bit < count; ++bit) {
            const auto byte = std::to_integer<std::uint8_t>(bytes_[bit_offset_ / 8U]);
            value |= static_cast<std::uint32_t>((byte >> (bit_offset_ % 8U)) & 1U) << bit;
            ++bit_offset_;
        }
        return value;
    }

    void AlignToByte() noexcept { bit_offset_ = (bit_offset_ + 7U) & ~std::size_t{7U}; }

    [[nodiscard]] std::size_t ConsumedBytes() const noexcept {
        return (bit_offset_ + 7U) / 8U;
    }

  private:
    std::span<const std::byte> bytes_;
    std::size_t bit_offset_{};
};

class DeflateHuffman final {
  public:
    explicit DeflateHuffman(const std::span<const std::uint8_t> lengths) {
        for (const auto length : lengths) {
            if (length > 15) throw std::runtime_error("APK deflate code length is invalid");
            ++counts_[length];
        }
        if (counts_[0] == lengths.size()) {
            throw std::runtime_error("APK deflate Huffman alphabet is empty");
        }
        std::int32_t remaining{1};
        for (std::size_t length = 1; length < counts_.size(); ++length) {
            remaining = remaining * 2 - counts_[length];
            if (remaining < 0) {
                throw std::runtime_error("APK deflate Huffman alphabet is oversubscribed");
            }
        }

        std::array<std::uint16_t, 16> offsets{};
        for (std::size_t length = 1; length + 1 < offsets.size(); ++length) {
            offsets[length + 1] =
                static_cast<std::uint16_t>(offsets[length] + counts_[length]);
        }
        symbols_.resize(lengths.size() - counts_[0]);
        for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
            const auto length = lengths[symbol];
            if (length != 0) symbols_[offsets[length]++] = static_cast<std::uint16_t>(symbol);
        }
    }

    [[nodiscard]] std::uint16_t Decode(DeflateBitReader& input) const {
        std::uint32_t code{};
        std::uint32_t first{};
        std::size_t index{};
        for (std::size_t length = 1; length < counts_.size(); ++length) {
            code |= input.Read(1);
            const auto count = static_cast<std::uint32_t>(counts_[length]);
            if (code >= first && code - first < count) {
                return symbols_[index + static_cast<std::size_t>(code - first)];
            }
            index += count;
            first = (first + count) << 1U;
            code <<= 1U;
        }
        throw std::runtime_error("APK deflate Huffman code is invalid");
    }

  private:
    std::array<std::uint16_t, 16> counts_{};
    std::vector<std::uint16_t> symbols_;
};

std::pair<DeflateHuffman, DeflateHuffman> FixedDeflateTables() {
    std::array<std::uint8_t, 288> literal_lengths{};
    std::fill(literal_lengths.begin(), literal_lengths.begin() + 144, 8);
    std::fill(literal_lengths.begin() + 144, literal_lengths.begin() + 256, 9);
    std::fill(literal_lengths.begin() + 256, literal_lengths.begin() + 280, 7);
    std::fill(literal_lengths.begin() + 280, literal_lengths.end(), 8);
    std::array<std::uint8_t, 32> distance_lengths{};
    distance_lengths.fill(5);
    return {DeflateHuffman{literal_lengths}, DeflateHuffman{distance_lengths}};
}

std::pair<DeflateHuffman, DeflateHuffman> DynamicDeflateTables(DeflateBitReader& input) {
    const auto literal_count = static_cast<std::size_t>(input.Read(5)) + 257U;
    const auto distance_count = static_cast<std::size_t>(input.Read(5)) + 1U;
    const auto code_count = static_cast<std::size_t>(input.Read(4)) + 4U;
    if (literal_count > 286) {
        throw std::runtime_error("APK deflate literal alphabet is too large");
    }
    constexpr std::array<std::uint8_t, 19> order{
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    std::array<std::uint8_t, 19> code_lengths{};
    for (std::size_t index = 0; index < code_count; ++index) {
        code_lengths[order[index]] = static_cast<std::uint8_t>(input.Read(3));
    }
    const DeflateHuffman code_table{code_lengths};

    std::vector<std::uint8_t> lengths(literal_count + distance_count);
    std::size_t index{};
    while (index < lengths.size()) {
        const auto symbol = code_table.Decode(input);
        if (symbol <= 15) {
            lengths[index++] = static_cast<std::uint8_t>(symbol);
            continue;
        }
        std::size_t repeat{};
        std::uint8_t value{};
        if (symbol == 16) {
            if (index == 0) {
                throw std::runtime_error("APK deflate repeat has no previous code length");
            }
            repeat = static_cast<std::size_t>(input.Read(2)) + 3U;
            value = lengths[index - 1];
        } else if (symbol == 17) {
            repeat = static_cast<std::size_t>(input.Read(3)) + 3U;
        } else if (symbol == 18) {
            repeat = static_cast<std::size_t>(input.Read(7)) + 11U;
        } else {
            throw std::runtime_error("APK deflate code-length symbol is invalid");
        }
        if (repeat > lengths.size() - index) {
            throw std::runtime_error("APK deflate code-length repeat is out of range");
        }
        std::fill(lengths.begin() + static_cast<std::ptrdiff_t>(index),
                  lengths.begin() + static_cast<std::ptrdiff_t>(index + repeat), value);
        index += repeat;
    }
    if (lengths[256] == 0) {
        throw std::runtime_error("APK deflate literal alphabet has no end code");
    }
    return {DeflateHuffman{{lengths.data(), literal_count}},
            DeflateHuffman{{lengths.data() + literal_count, distance_count}}};
}

void InflateCompressedBlock(DeflateBitReader& input, const DeflateHuffman& literals,
                            const DeflateHuffman& distances,
                            const std::span<std::byte> output, std::size_t& output_offset) {
    constexpr std::array<std::uint16_t, 29> length_base{
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    constexpr std::array<std::uint8_t, 29> length_extra{
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    constexpr std::array<std::uint16_t, 30> distance_base{
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
        6145, 8193, 12289, 16385, 24577};
    constexpr std::array<std::uint8_t, 30> distance_extra{
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
        6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

    for (;;) {
        const auto symbol = literals.Decode(input);
        if (symbol < 256) {
            if (output_offset == output.size()) {
                throw std::runtime_error("APK deflate output exceeds declared size");
            }
            output[output_offset++] = static_cast<std::byte>(symbol);
            continue;
        }
        if (symbol == 256) return;
        if (symbol > 285) throw std::runtime_error("APK deflate length symbol is invalid");
        const auto length_index = static_cast<std::size_t>(symbol - 257U);
        const auto length = static_cast<std::size_t>(length_base[length_index]) +
                            input.Read(length_extra[length_index]);
        const auto distance_symbol = distances.Decode(input);
        if (distance_symbol >= distance_base.size()) {
            throw std::runtime_error("APK deflate distance symbol is invalid");
        }
        const auto distance = static_cast<std::size_t>(distance_base[distance_symbol]) +
                              input.Read(distance_extra[distance_symbol]);
        if (distance > output_offset || length > output.size() - output_offset) {
            throw std::runtime_error("APK deflate back-reference is out of range");
        }
        for (std::size_t copied = 0; copied < length; ++copied) {
            output[output_offset] = output[output_offset - distance];
            ++output_offset;
        }
    }
}

std::vector<std::byte> InflateDeflate(const std::span<const std::byte> compressed,
                                     const std::size_t expected_size) {
    std::vector<std::byte> output(expected_size);
    std::size_t output_offset{};
    DeflateBitReader input{compressed};
    bool final{};
    while (!final) {
        final = input.Read(1) != 0;
        const auto type = input.Read(2);
        if (type == 0) {
            input.AlignToByte();
            const auto length = input.Read(8) | (input.Read(8) << 8U);
            const auto inverse = input.Read(8) | (input.Read(8) << 8U);
            if ((length ^ 0xffffU) != inverse ||
                length > output.size() - output_offset) {
                throw std::runtime_error("APK deflate stored block is invalid");
            }
            for (std::uint32_t index = 0; index < length; ++index) {
                output[output_offset++] = static_cast<std::byte>(input.Read(8));
            }
        } else if (type == 1) {
            auto [literals, distances] = FixedDeflateTables();
            InflateCompressedBlock(input, literals, distances, output, output_offset);
        } else if (type == 2) {
            auto [literals, distances] = DynamicDeflateTables(input);
            InflateCompressedBlock(input, literals, distances, output, output_offset);
        } else {
            throw std::runtime_error("APK deflate block type is reserved");
        }
    }
    if (output_offset != output.size()) {
        throw std::runtime_error("APK deflate output size disagrees with entry metadata");
    }
    if (input.ConsumedBytes() != compressed.size()) {
        throw std::runtime_error("APK deflate stream has trailing data");
    }
    return output;
}

std::span<const std::byte> ReadCompressedEntryData(
    const std::span<const std::byte> bytes, const ApkEntry& entry,
    const std::string_view description) {
    const auto offset = static_cast<std::size_t>(entry.local_header_offset);
    RequireRange(bytes, offset, 30, "APK local file header");
    if (Read32(bytes, offset) != kLocalSignature) {
        throw std::runtime_error("APK local file header signature is invalid");
    }
    if (Read16(bytes, offset + 6) != entry.general_purpose_flags ||
        Read16(bytes, offset + 8) != entry.compression_method) {
        throw std::runtime_error("APK local and central entry metadata disagree");
    }
    const auto local_crc = Read32(bytes, offset + 14);
    const auto local_compressed_size = Read32(bytes, offset + 18);
    const auto local_uncompressed_size = Read32(bytes, offset + 22);
    const bool has_descriptor = (entry.general_purpose_flags & 8U) != 0;
    const bool empty_local_metadata = local_crc == 0 && local_compressed_size == 0 &&
                                      local_uncompressed_size == 0;
    const bool matching_local_metadata = local_crc == entry.crc32 &&
                                         local_compressed_size == entry.compressed_size &&
                                         local_uncompressed_size == entry.uncompressed_size;
    if ((!has_descriptor && !matching_local_metadata) ||
        (has_descriptor && !empty_local_metadata && !matching_local_metadata)) {
        throw std::runtime_error("APK local and central entry metadata disagree");
    }
    const auto local_name_size = Read16(bytes, offset + 26);
    const auto local_extra_size = Read16(bytes, offset + 28);
    if (ReadName(bytes, offset + 30, local_name_size) != entry.name) {
        throw std::runtime_error("APK local and central entry names disagree");
    }
    const auto data_offset = offset + 30U + local_name_size + local_extra_size;
    RequireRange(bytes, data_offset, entry.compressed_size, description);
    const auto data_end = data_offset + entry.compressed_size;
    if (has_descriptor) {
        RequireRange(bytes, data_end, 12, "APK data descriptor");
        const bool unsigned_matches = Read32(bytes, data_end) == entry.crc32 &&
                                      Read32(bytes, data_end + 4) == entry.compressed_size &&
                                      Read32(bytes, data_end + 8) == entry.uncompressed_size;
        bool signed_matches{};
        if (Read32(bytes, data_end) == kDataDescriptorSignature &&
            bytes.size() - data_end >= 16U) {
            signed_matches = Read32(bytes, data_end + 4) == entry.crc32 &&
                             Read32(bytes, data_end + 8) == entry.compressed_size &&
                             Read32(bytes, data_end + 12) == entry.uncompressed_size;
        }
        if (!unsigned_matches && !signed_matches) {
            throw std::runtime_error("APK data descriptor disagrees with central metadata");
        }
    }
    return bytes.subspan(data_offset, entry.compressed_size);
}

std::size_t FindEnd(const std::span<const std::byte> bytes) {
    if (bytes.size() < kEndSize) throw std::runtime_error("APK has no ZIP end record");
    const auto first = bytes.size() > kEndSize + kMaxCommentSize
                           ? bytes.size() - kEndSize - kMaxCommentSize
                           : 0;
    for (auto offset = bytes.size() - kEndSize;; --offset) {
        if (Read32(bytes, offset) == kEndSignature &&
            Read16(bytes, offset + 20) == bytes.size() - offset - kEndSize) {
            return offset;
        }
        if (offset == first) break;
    }
    throw std::runtime_error("APK has no valid ZIP end record");
}

}  // namespace

ApkArchive ParseApkArchive(const std::span<const std::byte> bytes) {
    const auto end = FindEnd(bytes);
    if (Read16(bytes, end + 4) != 0 || Read16(bytes, end + 6) != 0) {
        throw std::runtime_error("multi-disk APK archives are unsupported");
    }
    const auto entry_count = Read16(bytes, end + 10);
    if (Read16(bytes, end + 8) != entry_count) {
        throw std::runtime_error("APK central directory entry counts disagree");
    }
    const auto central_size = Read32(bytes, end + 12);
    const auto central_offset = Read32(bytes, end + 16);
    RequireRange(bytes, central_offset, central_size, "APK central directory");
    if (static_cast<std::uint64_t>(central_offset) + central_size != end) {
        throw std::runtime_error("APK central directory does not end at the ZIP end record");
    }

    ApkArchive archive;
    archive.entries.reserve(entry_count);
    std::unordered_set<std::string> names;
    std::size_t cursor = central_offset;
    for (std::size_t index = 0; index < entry_count; ++index) {
        RequireRange(bytes, cursor, 46, "APK central directory entry");
        if (Read32(bytes, cursor) != kCentralSignature) {
            throw std::runtime_error("APK central directory signature is invalid");
        }
        const auto flags = Read16(bytes, cursor + 8);
        if ((flags & 1U) != 0) throw std::runtime_error("encrypted APK entries are unsupported");
        const auto name_size = Read16(bytes, cursor + 28);
        const auto extra_size = Read16(bytes, cursor + 30);
        const auto comment_size = Read16(bytes, cursor + 32);
        const auto record_size = static_cast<std::size_t>(46) + name_size + extra_size + comment_size;
        RequireRange(bytes, cursor, record_size, "APK central directory entry");
        auto name = ReadName(bytes, cursor + 46, name_size);
        if (!names.insert(name).second) throw std::runtime_error("APK contains duplicate entry names");
        archive.entries.push_back({
            .name = std::move(name),
            .general_purpose_flags = flags,
            .compression_method = Read16(bytes, cursor + 10),
            .crc32 = Read32(bytes, cursor + 16),
            .compressed_size = Read32(bytes, cursor + 20),
            .uncompressed_size = Read32(bytes, cursor + 24),
            .local_header_offset = Read32(bytes, cursor + 42),
        });
        cursor += record_size;
    }
    if (cursor != static_cast<std::size_t>(central_offset) + central_size) {
        throw std::runtime_error("APK central directory size disagrees with entries");
    }
    return archive;
}

std::vector<std::byte> ReadStoredApkEntry(const std::span<const std::byte> bytes,
                                         const ApkArchive& archive,
                                         const std::string_view name) {
    const auto found = std::find_if(archive.entries.begin(), archive.entries.end(),
                                    [name](const ApkEntry& entry) { return entry.name == name; });
    if (found == archive.entries.end()) throw std::runtime_error("APK entry was not found");
    if (found->compression_method != kStoredMethod ||
        found->compressed_size != found->uncompressed_size) {
        throw std::runtime_error("APK entry is not stored without compression");
    }
    const auto data = ReadCompressedEntryData(bytes, *found, "APK stored entry data");
    if (Crc32(data) != found->crc32) throw std::runtime_error("APK stored entry CRC32 mismatch");
    return {data.begin(), data.end()};
}

std::vector<std::byte> ReadApkEntry(const std::span<const std::byte> bytes,
                                    const ApkArchive& archive,
                                    const std::string_view name) {
    const auto found = std::find_if(archive.entries.begin(), archive.entries.end(),
                                    [name](const ApkEntry& entry) { return entry.name == name; });
    if (found == archive.entries.end()) throw std::runtime_error("APK entry was not found");
    if (found->compression_method == kStoredMethod) {
        return ReadStoredApkEntry(bytes, archive, name);
    }
    if (found->compression_method != kDeflateMethod) {
        throw std::runtime_error("APK entry uses an unsupported compression method");
    }

    const auto data = ReadCompressedEntryData(bytes, *found, "APK deflated entry data");
    auto result = InflateDeflate(data, found->uncompressed_size);
    if (Crc32(result) != found->crc32) {
        throw std::runtime_error("APK deflated entry CRC32 mismatch");
    }
    return result;
}

}  // namespace ogplay::loader

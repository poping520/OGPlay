#include "ogplay/loader/apk.h"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "ogplay/core/byte_order.h"

namespace ogplay::loader {
namespace {

constexpr std::uint32_t kLocalSignature = 0x04034b50;
constexpr std::uint32_t kCentralSignature = 0x02014b50;
constexpr std::uint32_t kEndSignature = 0x06054b50;
constexpr std::size_t kEndSize = 22;
constexpr std::size_t kMaxCommentSize = 65535;
constexpr std::uint16_t kStoredMethod = 0;
constexpr std::uint16_t kDeflateMethod = 8;

void RequireRange(const std::span<const std::byte> bytes, const std::size_t offset,
                  const std::size_t size, const std::string_view what) {
    if (!core::RangeFits(bytes, offset, size)) {
        throw std::runtime_error(std::string(what) + " is outside APK bytes");
    }
}

std::uint16_t Read16(const std::span<const std::byte> bytes, const std::size_t offset) {
    RequireRange(bytes, offset, 2, "ZIP field");
    return core::ReadLittleEndian<std::uint16_t>(bytes, offset);
}

std::uint32_t Read32(const std::span<const std::byte> bytes, const std::size_t offset) {
    RequireRange(bytes, offset, 4, "ZIP field");
    return core::ReadLittleEndian<std::uint32_t>(bytes, offset);
}

std::string ReadName(const std::span<const std::byte> bytes, const std::size_t offset,
                     const std::size_t size) {
    RequireRange(bytes, offset, size, "ZIP entry name");
    std::string name;
    name.reserve(size);
    for (std::size_t index = 0; index < size; ++index) {
        const auto value = std::to_integer<std::uint8_t>(bytes[offset + index]);
        if (value == 0) {
            throw std::runtime_error("APK entry name contains NUL");
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
      explicit DeflateHuffman(const std::span<const std::uint8_t> lengths, const bool allow_empty = false) {
          for (const auto length : lengths) {
              if (length > 15) throw std::runtime_error("APK deflate code length is invalid");
              ++counts_[length];
          }
          if (!allow_empty && counts_[0] == lengths.size())
              throw std::runtime_error("APK deflate Huffman alphabet is empty");
          std::int32_t remaining{1};
          for (std::size_t length = 1; length < counts_.size(); ++length) {
              remaining = remaining * 2 - counts_[length];
              if (remaining < 0) {
                  throw std::runtime_error("APK deflate Huffman alphabet is oversubscribed");
              }
          }

          std::array<std::uint16_t, 16> offsets{};
          for (std::size_t length = 1; length + 1 < offsets.size(); ++length) {
              offsets[length + 1] = static_cast<std::uint16_t>(offsets[length] + counts_[length]);
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
    std::fill(literal_lengths.begin(), literal_lengths.begin() + 144,
              std::uint8_t{8});
    std::fill(literal_lengths.begin() + 144, literal_lengths.begin() + 256,
              std::uint8_t{9});
    std::fill(literal_lengths.begin() + 256, literal_lengths.begin() + 280,
              std::uint8_t{7});
    std::fill(literal_lengths.begin() + 280, literal_lengths.end(),
              std::uint8_t{8});
    std::array<std::uint8_t, 32> distance_lengths{};
    distance_lengths.fill(std::uint8_t{5});
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
            DeflateHuffman{{lengths.data() + literal_count, distance_count}, true}};
}

// A cursor owns one bounded LZ window, the current block and any unfinished copy.
class DeflateCursor final {
public:
    DeflateCursor(std::span<const std::byte> bytes, std::size_t expected) : input_(bytes), expected_(expected) {}

    [[nodiscard]] std::optional<std::byte> Next() {
        while (!done_) {
            if (copy_remaining_) {
                --copy_remaining_;
                return Emit(history_[(offset_ - copy_distance_) % history_.size()]);
            }
            if (stored_remaining_) {
                --stored_remaining_;
                return Emit(static_cast<std::byte>(input_.Read(8)));
            }
            if (tables_) {
                const auto symbol = tables_->first.Decode(input_);
                if (symbol < 256) return Emit(static_cast<std::byte>(symbol));
                if (symbol != 256) {
                    StartCopy(symbol);
                    continue;
                }
                tables_.reset();
            }
            if (final_) {
                if (offset_ != expected_)
                    throw std::runtime_error("APK deflate output size disagrees with entry metadata");
                done_ = true;
                break;
            }
            StartBlock();
        }
        return std::nullopt;
    }

    [[nodiscard]] std::size_t Size() const noexcept { return offset_; }

  private:
      std::byte Emit(std::byte value) {
          if (offset_ == expected_) throw std::runtime_error("APK deflate output exceeds declared size");
          history_[offset_ % history_.size()] = value;
          ++offset_;
          return value;
      }

      void StartBlock() {
          final_ = input_.Read(1) != 0;
          switch (input_.Read(2)) {
          case 0: {
              input_.AlignToByte();
              const auto length_lo = input_.Read(8);
              const auto length = length_lo | (input_.Read(8) << 8U);
              const auto inverse_lo = input_.Read(8);
              const auto inverse = inverse_lo | (input_.Read(8) << 8U);
              if ((length ^ 0xffffU) != inverse || length > expected_ - offset_)
                  throw std::runtime_error("APK deflate stored block is invalid");
              stored_remaining_ = length;
              break;
          }
          case 1: tables_.emplace(FixedDeflateTables()); break;
          case 2: tables_.emplace(DynamicDeflateTables(input_)); break;
          default: throw std::runtime_error("APK deflate block type is reserved");
          }
      }

      void StartCopy(std::uint16_t symbol) {
          static constexpr std::array<std::uint16_t, 29> length_base{3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                                                     15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                                                     67, 83, 99, 115, 131, 163, 195, 227, 258};
          static constexpr std::array<std::uint8_t, 29> length_extra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
          static constexpr std::array<std::uint16_t, 30> distance_base{
              1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
              193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
          static constexpr std::array<std::uint8_t, 30> distance_extra{
              0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

          if (symbol > 285) throw std::runtime_error("APK deflate length symbol is invalid");
          const auto length_index = static_cast<std::size_t>(symbol - 257U);
          copy_remaining_ =
              static_cast<std::size_t>(length_base[length_index]) + input_.Read(length_extra[length_index]);
          const auto distance_symbol = tables_->second.Decode(input_);
          if (distance_symbol >= distance_base.size())
              throw std::runtime_error("APK deflate distance symbol is invalid");
          copy_distance_ =
              static_cast<std::size_t>(distance_base[distance_symbol]) + input_.Read(distance_extra[distance_symbol]);
          if (copy_distance_ == 0 || copy_distance_ > std::min(offset_, history_.size()) ||
              copy_remaining_ > expected_ - offset_)
              throw std::runtime_error("APK deflate back-reference is out of range");
      }

      DeflateBitReader input_;
      std::size_t expected_, offset_{}, stored_remaining_{}, copy_remaining_{}, copy_distance_{};
      std::array<std::byte, 32U * 1024U> history_{};
      std::optional<std::pair<DeflateHuffman, DeflateHuffman>> tables_;
      bool final_{}, done_{};
};

void InflateDeflateWindow(const std::span<const std::byte> compressed, const std::size_t expected_size,
                          const std::size_t window_offset, const std::span<std::byte> window,
                          const std::stop_token stop = {}) {
    DeflateCursor cursor{compressed, expected_size};
    for (std::size_t offset = 0;; ++offset) {
        if ((offset & 0xffffU) == 0U && stop.stop_requested()) throw std::runtime_error("APK entry read cancelled");
        auto byte = cursor.Next();
        if (!byte) break;
        if (offset >= window_offset && offset - window_offset < window.size()) window[offset - window_offset] = *byte;
    }
}

std::vector<std::byte> InflateDeflate(const std::span<const std::byte> compressed,
                                     const std::size_t expected_size) {
    std::vector<std::byte> output(expected_size);
    static_cast<void>(InflateDeflateWindow(compressed, expected_size, 0, output));
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
    // API19 uses central metadata; local flags still reject encrypted input.
    if ((Read16(bytes, offset + 6) & 1U) != 0) throw std::runtime_error("encrypted APK entries are unsupported");
    const auto local_name_size = Read16(bytes, offset + 26);
    const auto local_extra_size = Read16(bytes, offset + 28);
    const auto data_offset = offset + 30U + local_name_size + local_extra_size;
    RequireRange(bytes, data_offset, entry.compressed_size, description);
    return bytes.subspan(data_offset, entry.compressed_size);
}

std::size_t FindEnd(const std::span<const std::byte> bytes) {
    if (bytes.size() < kEndSize) throw std::runtime_error("APK has no ZIP end record");
    const auto first = bytes.size() > kEndSize + kMaxCommentSize
                           ? bytes.size() - kEndSize - kMaxCommentSize
                           : 0;
    for (auto offset = bytes.size() - kEndSize;; --offset) {
        if (Read32(bytes, offset) == kEndSignature) {
            // API19 ZipArchive.cpp:211-214 selects the last signature first;
            // :254-257 rejects an out-of-range comment without resuming the scan.
            if (Read16(bytes, offset + 20) > bytes.size() - offset - kEndSize) {
                throw std::runtime_error("APK has no valid ZIP end record");
            }
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
    if (static_cast<std::uint64_t>(central_offset) + central_size > end) {
        throw std::runtime_error("APK central directory does not end at the ZIP end record");
    }

    ApkArchive archive;
    archive.entries.reserve(entry_count);
    std::unordered_set<std::string> names;
    std::size_t cursor = central_offset;
    const auto central_end = static_cast<std::size_t>(central_offset) + central_size;
    for (std::size_t index = 0; index < entry_count; ++index) {
        RequireRange(bytes.first(central_end), cursor, 46, "APK central directory entry");
        if (Read32(bytes, cursor) != kCentralSignature) {
            throw std::runtime_error("APK central directory signature is invalid");
        }
        const auto flags = Read16(bytes, cursor + 8);
        if ((flags & 1U) != 0) throw std::runtime_error("encrypted APK entries are unsupported");
        const auto name_size = Read16(bytes, cursor + 28);
        const auto extra_size = Read16(bytes, cursor + 30);
        const auto comment_size = Read16(bytes, cursor + 32);
        const auto record_size = static_cast<std::size_t>(46) + name_size + extra_size + comment_size;
        RequireRange(bytes.first(central_end), cursor, record_size, "APK central directory entry");
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
    return {data.begin(), data.end()};
}

std::uint64_t StoredApkEntryDataOffset(
    const std::span<const std::byte> bytes, const ApkArchive& archive,
    const std::string_view name) {
    const auto found = std::find_if(
        archive.entries.begin(), archive.entries.end(),
        [name](const ApkEntry& entry) { return entry.name == name; });
    if (found == archive.entries.end()) {
        throw std::runtime_error("APK entry was not found");
    }
    if (found->compression_method != kStoredMethod ||
        found->compressed_size != found->uncompressed_size) {
        throw std::runtime_error("APK entry is not stored without compression");
    }
    const auto data =
        ReadCompressedEntryData(bytes, *found, "APK stored entry data");
    return static_cast<std::uint64_t>(data.data() - bytes.data());
}

std::span<const std::byte> ValidatedStoredApkEntryData(
    const std::span<const std::byte> bytes, const ApkArchive& archive,
    const std::string_view name, const std::stop_token stop) {
    const auto found = std::find_if(archive.entries.begin(), archive.entries.end(),
        [name](const ApkEntry& entry) { return entry.name == name; });
    if (found == archive.entries.end()) throw std::runtime_error("APK entry was not found");
    return ValidatedStoredApkEntryData(bytes, *found, stop);
}

std::span<const std::byte> ValidatedStoredApkEntryData(const std::span<const std::byte> bytes, const ApkEntry& entry,
                                                       const std::stop_token stop) {
    if (entry.compression_method != kStoredMethod || entry.compressed_size != entry.uncompressed_size)
        throw std::runtime_error("APK entry is not stored without compression");
    const auto data = ReadCompressedEntryData(bytes, entry, "APK stored entry data");
    if (stop.stop_requested()) throw std::runtime_error("APK entry read cancelled");
    return data;
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
    return result;
}

std::size_t ReadApkEntryRange(
    const std::span<const std::byte> bytes, const ApkArchive& archive,
    const std::string_view name, const std::uint64_t offset,
    const std::span<std::byte> destination, const std::stop_token stop,
    ApkEntryRangeStatistics* const statistics) {
    const auto found = std::find_if(
        archive.entries.begin(), archive.entries.end(),
        [name](const ApkEntry& entry) { return entry.name == name; });
    if (found == archive.entries.end()) throw std::runtime_error("APK entry was not found");
    if (offset >= found->uncompressed_size || destination.empty()) return 0;
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
        destination.size(), found->uncompressed_size - offset));
    if (found->compression_method == kStoredMethod) {
        if (found->compressed_size != found->uncompressed_size)
            throw std::runtime_error("APK stored entry sizes disagree");
        const auto data = ReadCompressedEntryData(bytes, *found, "APK stored entry data");
        if (stop.stop_requested()) throw std::runtime_error("APK entry read cancelled");
        std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset), count,
                    destination.begin());
        if (statistics != nullptr) {
            statistics->copied_bytes += count;
        }
        return count;
    }
    if (found->compression_method != kDeflateMethod) {
        throw std::runtime_error("APK entry uses an unsupported compression method");
    }
    const auto data = ReadCompressedEntryData(bytes, *found, "APK deflated entry data");
    InflateDeflateWindow(data, found->uncompressed_size, static_cast<std::size_t>(offset), destination.first(count),
                         stop);
    if (statistics != nullptr) {
        statistics->validation_scan_bytes += found->uncompressed_size;
        statistics->copied_bytes += count;
    }
    return count;
}

struct ApkDeflateStream::Impl {
    std::span<const std::byte> compressed;
    std::size_t size;
    DeflateCursor cursor;
    bool failed{};

    Impl(std::span<const std::byte> bytes, std::size_t expected)
        : compressed(bytes), size(expected), cursor(bytes, expected) {}
};

ApkDeflateStream::ApkDeflateStream(std::span<const std::byte> bytes, const ApkEntry& entry) {
    if (entry.compression_method != kDeflateMethod) throw std::runtime_error("ZIP entry is not deflated");
    impl_ = std::make_unique<Impl>(ReadCompressedEntryData(bytes, entry, "APK deflated entry data"),
                                   entry.uncompressed_size);
}

ApkDeflateStream::~ApkDeflateStream() = default;

std::size_t ApkDeflateStream::Read(std::size_t offset, std::span<std::byte> destination) {
    auto& stream = *impl_;
    if (stream.failed) throw std::runtime_error("APK deflate stream is invalid");
    if (offset > stream.size) throw std::runtime_error("APK deflate read offset outside entry");
    if (destination.empty()) return 0;
    try {
        if (offset < stream.cursor.Size()) stream.cursor = DeflateCursor(stream.compressed, stream.size);
        while (stream.cursor.Size() < offset)
            static_cast<void>(stream.cursor.Next());
        const auto count = std::min(destination.size(), stream.size - offset);
        for (auto& byte : destination.first(count))
            byte = stream.cursor.Next().value();
        if (stream.cursor.Size() == stream.size) static_cast<void>(stream.cursor.Next());
        return count;
    } catch (...) {
        stream.failed = true;
        throw;
    }
}

}  // namespace ogplay::loader

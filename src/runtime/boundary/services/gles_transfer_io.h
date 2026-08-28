#pragma once

// Shared little-endian 32-bit guest transfer primitives for the GLES1/GLES2
// boundary modules and the graphics dispatch service. Byte-level conversion
// exists exactly once here; each handler keeps its own size validation, error
// text and Commit() timing because those differ per handler semantics.

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/core/byte_order.h"
#include "ogplay/gles/guest_transfer.h"
#include "ogplay/memory/address_space.h"

namespace ogplay::runtime::gles_io {

// Index types used by client-array draw preflight (GL 1.1 enum values).
inline constexpr std::uint32_t kIndexTypeUnsignedByte = 0x1401U;
inline constexpr std::uint32_t kIndexTypeUnsignedShort = 0x1403U;

// Little-endian bytes to 32-bit words; a trailing partial word is truncated.
[[nodiscard]] inline std::vector<std::uint32_t> LoadWordsLE(
    const std::span<const std::byte> bytes) {
    std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
    for (std::size_t index = 0; index < words.size(); ++index) {
        words[index] = core::ReadLittleEndian<std::uint32_t>(
            bytes, index * sizeof(std::uint32_t));
    }
    return words;
}

// Read count little-endian 32-bit words starting at a guest address.
[[nodiscard]] inline std::vector<std::uint32_t> LoadGuestWordsLE(
    const memory::AddressSpace& address_space, const std::uint32_t address,
    const std::size_t count, const std::uint64_t thread_id) {
    std::vector<std::byte> bytes(count * sizeof(std::uint32_t));
    address_space.Read(memory::GuestAddress{address}, bytes, thread_id);
    return LoadWordsLE(bytes);
}

// Words to little-endian bytes; the caller guarantees the span is large
// enough. No validation, no commit - handlers keep their own policies.
inline void StoreWordsLE(const std::span<std::byte> bytes,
                         const std::span<const std::uint32_t> words) noexcept {
    for (std::size_t index = 0; index < words.size(); ++index) {
        core::WriteLittleEndian<std::uint32_t>(bytes,
                                               index * sizeof(std::uint32_t),
                                               words[index]);
    }
}

// Bit-pattern conversion between 32-bit words and float/int32 values.
template <typename Value>
[[nodiscard]] inline std::vector<std::uint32_t> WordsFromValues(
    const std::span<const Value> values) {
    std::vector<std::uint32_t> words;
    words.reserve(values.size());
    for (const auto value : values) {
        words.push_back(std::bit_cast<std::uint32_t>(value));
    }
    return words;
}

template <typename Value>
[[nodiscard]] inline std::vector<Value> ValuesFromWords(
    const std::span<const std::uint32_t> words) {
    std::vector<Value> values;
    values.reserve(words.size());
    for (const auto word : words) {
        values.push_back(std::bit_cast<Value>(word));
    }
    return values;
}

// Whole-buffer read with a word-alignment preflight; the error text is
// provided by the caller so every handler keeps its original message.
[[nodiscard]] inline std::vector<std::uint32_t> ReadWordsAligned(
    const gles::GuestBuffer& input, const std::string_view alignment_error) {
    const auto bytes = input.Bytes();
    if (bytes.size() % sizeof(std::uint32_t) != 0U) {
        throw std::logic_error(std::string(alignment_error));
    }
    return LoadWordsLE(bytes);
}

// Exact-size committed word write for query/output handlers.
inline void WriteWordsExact(gles::GuestBuffer& output,
                            const std::span<const std::uint32_t> words,
                            const std::string_view size_error) {
    auto bytes = output.WritableBytes();
    if (bytes.size() != words.size() * sizeof(std::uint32_t)) {
        throw std::logic_error(std::string(size_error));
    }
    StoreWordsLE(bytes, words);
    output.Commit();
}

// Bit-pattern value write with the same exact-size policy.
template <typename Value>
inline void WriteValuesExact(gles::GuestBuffer& output,
                             const std::span<const Value> values,
                             const std::string_view size_error) {
    WriteWordsExact(output, WordsFromValues(values), size_error);
}

// Write a NUL-terminated, truncation-limited text into the buffer and return
// the number of text bytes written (excluding the terminator).
[[nodiscard]] inline std::uint32_t WriteText(gles::GuestBuffer& output,
                                             const std::string_view text) {
    auto bytes = output.WritableBytes();
    const auto length =
        bytes.empty() ? 0U : std::min(text.size(), bytes.size() - 1U);
    std::ranges::fill(bytes, std::byte{});
    for (std::size_t index = 0; index < length; ++index) {
        bytes[index] =
            static_cast<std::byte>(static_cast<unsigned char>(text[index]));
    }
    output.Commit();
    return static_cast<std::uint32_t>(length);
}

// Maximum client-array index for byte/short index data; the unsupported-type
// error text is provided by the caller.
[[nodiscard]] inline std::uint32_t MaximumGuestIndex(
    const std::span<const std::byte> bytes, const std::uint32_t type,
    const std::string_view unsupported_type_error) {
    std::uint32_t maximum{};
    if (type == kIndexTypeUnsignedByte) {
        for (const auto value : bytes) {
            maximum = std::max(maximum, static_cast<std::uint32_t>(
                                            std::to_integer<std::uint8_t>(value)));
        }
        return maximum;
    }
    if (type != kIndexTypeUnsignedShort || bytes.size() % 2U != 0U) {
        throw std::invalid_argument(std::string(unsupported_type_error));
    }
    for (std::size_t offset = 0; offset < bytes.size(); offset += 2U) {
        maximum = std::max(
            maximum,
            static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[offset])) |
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + 1U]))
                 << 8U));
    }
    return maximum;
}

}  // namespace ogplay::runtime::gles_io

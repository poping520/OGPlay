#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <iomanip>
#include <sstream>
#include <vector>

#include "ogplay/core/byte_order.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"

namespace ogplay::runtime {

inline void ValidateJniGuestStringLimits(const JniGuestStringLimits limits) {
    if (limits.maximum_modified_utf8_bytes == 0 || limits.maximum_modified_utf8_bytes > INT32_MAX ||
        limits.maximum_utf16_code_units == 0 || limits.maximum_utf16_code_units > INT32_MAX ||
        limits.maximum_copy_bytes == 0 || limits.maximum_copy_bytes > INT32_MAX) {
        throw std::invalid_argument("JNI guest string resource limits are invalid");
    }
}

[[nodiscard]] inline std::uint8_t ReadGuest8(
    memory::AddressSpace& address_space, const memory::GuestAddress address,
    const std::uint64_t thread_id) {
    std::byte byte{};
    address_space.Read(address, std::span{&byte, 1U}, thread_id);
    return std::to_integer<std::uint8_t>(byte);
}

[[nodiscard]] inline std::uint16_t ReadGuest16(
    memory::AddressSpace& address_space, const memory::GuestAddress address,
    const std::uint64_t thread_id) {
    std::array<std::byte, 2> bytes{};
    address_space.Read(address, bytes, thread_id);
    return core::ReadLittleEndian<std::uint16_t>(std::span{bytes}, 0U);
}

[[nodiscard]] inline std::uint32_t ReadGuest32(
    memory::AddressSpace& address_space, const memory::GuestAddress address,
    const std::uint64_t thread_id) {
    std::array<std::byte, 4> bytes{};
    address_space.Read(address, bytes, thread_id);
    return core::ReadLittleEndian<std::uint32_t>(std::span{bytes}, 0U);
}

[[nodiscard]] inline std::uint64_t ReadGuest64(
    memory::AddressSpace& address_space, const memory::GuestAddress address,
    const std::uint64_t thread_id) {
    std::array<std::byte, 8> bytes{};
    address_space.Read(address, bytes, thread_id);
    return core::ReadLittleEndian<std::uint64_t>(std::span{bytes}, 0U);
}

[[nodiscard]] inline std::string ReadGuestCString(
    memory::AddressSpace& address_space, const memory::GuestAddress address,
    const std::uint64_t thread_id, const std::string_view field) {
    constexpr std::size_t kMaximumBytes = 1024U;
    if (address.IsNull()) {
        throw JniGuestBindingError(
            "JNI guest " + std::string(field) + " pointer is null");
    }
    std::size_t length{};
    try {
        length = address_space.CStringLength(address, kMaximumBytes, thread_id);
    } catch (const std::length_error&) {
        throw JniGuestBindingError(
            "JNI guest " + std::string(field) + " scan limit reached: scanned_bytes=1024");
    }
    std::string result(length, '\0');
    address_space.Read(address, std::as_writable_bytes(std::span{result}), thread_id);
    return result;
}

// String bodies have a separate resource policy from class/member names.
[[nodiscard]] inline std::vector<std::uint8_t> ReadGuestModifiedUtf8(
    memory::AddressSpace& space, const memory::GuestAddress address,
    const std::uint64_t thread_id, const std::size_t maximum_payload_bytes) {
    const auto scan_bytes = maximum_payload_bytes + 1U; // include terminator
    std::size_t length{};
    try {
        length = space.CStringLength(address, scan_bytes, thread_id);
    } catch (const std::length_error&) {
        std::ostringstream message;
        message << "modified UTF-8 scan limit reached: pointer=0x" << std::hex
                << std::setw(8) << std::setfill('0') << address.Value() << std::dec
                << " scanned_bytes=" << scan_bytes << " payload_budget=" << maximum_payload_bytes;
        throw JniGuestBindingError(message.str());
    }
    std::vector<std::uint8_t> encoded(length);
    space.Read(address, std::as_writable_bytes(std::span{encoded}), thread_id);
    return encoded;
}

}  // namespace ogplay::runtime

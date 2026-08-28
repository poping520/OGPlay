#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "ogplay/core/byte_order.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"

namespace ogplay::runtime {

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
            "JNI guest " + std::string(field) + " is not null-terminated");
    }
    std::string result(length, '\0');
    address_space.Read(address, std::as_writable_bytes(std::span{result}), thread_id);
    return result;
}

}  // namespace ogplay::runtime

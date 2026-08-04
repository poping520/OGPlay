#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/guest_transfer.h"
#include "ogplay/memory/address.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::gles {

enum class GlesLengthDisposition { transfer, deferred };

struct GlesLengthResolution final {
    GlesLengthDisposition disposition{GlesLengthDisposition::transfer};
    std::uint64_t element_count{};
};

struct GlesLengthRequest final {
    std::string_view function_name;
    std::string_view parameter_name;
    std::string_view expression;
    std::span<const GlesGuestValue> arguments;
};

class GlesLengthResolver {
public:
    virtual ~GlesLengthResolver() = default;
    [[nodiscard]] virtual std::optional<GlesLengthResolution> Resolve(
        const GlesLengthRequest& request) const = 0;
};

class GlesCallPreparationError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct PreparedGlesPointer final {
    std::size_t parameter_index{};
    memory::GuestAddress guest_address;
    std::uint64_t byte_size{};
    GuestTransferDirection direction{GuestTransferDirection::input};
    bool deferred{};
    std::optional<GuestBuffer> transfer;
};

struct PreparedGlesCall final {
    GlesThunkId id{};
    std::vector<PreparedGlesPointer> pointers;
};

[[nodiscard]] PreparedGlesCall PrepareGles2Call(
    memory::AddressSpace& memory, GlesThunkId id,
    std::span<const GlesGuestValue> arguments, std::uint64_t thread_id = 0,
    const GlesLengthResolver* resolver = nullptr,
    std::uint64_t size_limit = kDefaultGuestTransferLimit);

}  // namespace ogplay::gles

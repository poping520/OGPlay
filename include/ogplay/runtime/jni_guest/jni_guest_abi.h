#pragma once

#include "ogplay/runtime/common/guest_address_layout.h"

#include <cstdint>
#include <optional>

#include "ogplay/memory/address.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

struct JniGuestThunk final {
    bool java_vm{};
    std::uint16_t slot{};
};

[[nodiscard]] std::optional<JniGuestThunk> DescribeJniGuestThunk(
    memory::GuestAddress target) noexcept;

class GuestJniAbi final {
public:
    explicit GuestJniAbi(memory::AddressSpace& address_space);
    ~GuestJniAbi();

    GuestJniAbi(const GuestJniAbi&) = delete;
    GuestJniAbi& operator=(const GuestJniAbi&) = delete;
    GuestJniAbi(GuestJniAbi&&) = delete;
    GuestJniAbi& operator=(GuestJniAbi&&) = delete;

    [[nodiscard]] memory::GuestAddress Environment() const noexcept {
        return kJniGuestEnvironment;
    }
    [[nodiscard]] memory::GuestAddress JavaVm() const noexcept {
        return kJniGuestJavaVm;
    }

private:
    memory::AddressSpace* address_space_{};
    std::uint64_t page_size_{};
};

}  // namespace ogplay::runtime

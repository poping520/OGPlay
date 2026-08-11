#include "ogplay/runtime/jni_guest/jni_guest_abi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_java_vm.h"

namespace ogplay::runtime {
namespace {

constexpr std::uint32_t kThunkStride = 4U;
constexpr std::uint8_t kJniSupervisorCall = 3U;

[[nodiscard]] memory::GuestAddress EnvironmentThunk(
    const std::size_t slot) {
    return memory::GuestAddress{
        kJniThunkBegin + static_cast<std::uint32_t>(slot * kThunkStride) + 1U};
}

[[nodiscard]] memory::GuestAddress InvokeThunk(const std::size_t slot) {
    return memory::GuestAddress{
        kJniInvokeThunkBegin +
        static_cast<std::uint32_t>(slot * kThunkStride) + 1U};
}

void Write32(std::span<std::byte> bytes, const std::size_t offset,
             const std::uint32_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 4U) {
        throw std::length_error("JNI guest ABI data exceeds its guest page");
    }
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

void WriteThunk(std::span<std::byte> bytes, const std::size_t slot) {
    const auto offset = slot * kThunkStride;
    if (offset > bytes.size() || bytes.size() - offset < kThunkStride) {
        throw std::length_error("JNI guest thunk catalog exceeds its guest page");
    }
    bytes[offset] = static_cast<std::byte>(kJniSupervisorCall);
    bytes[offset + 1U] = std::byte{0xdf};  // Thumb svc #3
    bytes[offset + 2U] = std::byte{0x70};
    bytes[offset + 3U] = std::byte{0x47};  // bx lr
}

[[nodiscard]] std::optional<std::uint16_t> DescribeThunkSlot(
    const memory::GuestAddress target, const std::uint32_t begin,
    const std::size_t slot_count, const std::size_t reserved_count) noexcept {
    const auto value = target.Value();
    if (value <= begin) return std::nullopt;
    const auto offset = value - begin;
    if ((offset % kThunkStride) != 1U) return std::nullopt;
    const auto slot = static_cast<std::size_t>(offset / kThunkStride);
    if (slot < reserved_count || slot >= slot_count ||
        slot > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(slot);
}

}  // namespace

std::optional<JniGuestThunk> DescribeJniGuestThunk(
    const memory::GuestAddress target) noexcept {
    if (const auto slot = DescribeThunkSlot(
            target, kJniThunkBegin, kJniNativeInterfaceSlotCount,
            kJniReservedSlotCount);
        slot.has_value()) {
        return JniGuestThunk{false, *slot};
    }
    if (const auto slot = DescribeThunkSlot(
            target, kJniInvokeThunkBegin, kJniInvokeInterfaceSlotCount, 3U);
        slot.has_value()) {
        return JniGuestThunk{true, *slot};
    }
    return std::nullopt;
}

GuestJniAbi::GuestJniAbi(memory::AddressSpace& address_space)
    : address_space_(&address_space), page_size_(address_space.PageSize()) {
    const memory::GuestRange environment_thunks{
        memory::GuestAddress{kJniThunkBegin}, page_size_};
    const memory::GuestRange invoke_thunks{
        memory::GuestAddress{kJniInvokeThunkBegin}, page_size_};
    const memory::GuestRange data{kJniGuestAbiDataBegin, page_size_};
    bool environment_mapped{};
    bool invoke_mapped{};
    bool data_mapped{};
    try {
        address_space.Map(environment_thunks, memory::PageProtection::read |
                                                 memory::PageProtection::write);
        environment_mapped = true;
        address_space.Map(invoke_thunks, memory::PageProtection::read |
                                            memory::PageProtection::write);
        invoke_mapped = true;
        address_space.Map(data, memory::PageProtection::read |
                                    memory::PageProtection::write);
        data_mapped = true;

        std::vector<std::byte> environment_code(
            static_cast<std::size_t>(page_size_), std::byte{});
        for (std::size_t slot = kJniReservedSlotCount;
             slot < kJniNativeInterfaceSlotCount; ++slot) {
            WriteThunk(environment_code, slot);
        }
        address_space.Write(memory::GuestAddress{kJniThunkBegin},
                            environment_code);

        std::vector<std::byte> invoke_code(
            static_cast<std::size_t>(page_size_), std::byte{});
        for (std::size_t slot = 3U; slot < kJniInvokeInterfaceSlotCount;
             ++slot) {
            WriteThunk(invoke_code, slot);
        }
        address_space.Write(memory::GuestAddress{kJniInvokeThunkBegin},
                            invoke_code);

        std::vector<std::byte> abi_data(static_cast<std::size_t>(page_size_),
                                        std::byte{});
        for (std::size_t slot = kJniReservedSlotCount;
             slot < kJniNativeInterfaceSlotCount; ++slot) {
            Write32(abi_data, slot * sizeof(std::uint32_t),
                    EnvironmentThunk(slot).Value());
        }
        const auto invoke_offset =
            kJniGuestInvokeTable.Value() - kJniGuestAbiDataBegin.Value();
        for (std::size_t slot = 3U; slot < kJniInvokeInterfaceSlotCount;
             ++slot) {
            Write32(abi_data,
                    invoke_offset + slot * sizeof(std::uint32_t),
                    InvokeThunk(slot).Value());
        }
        Write32(abi_data,
                kJniGuestEnvironment.Value() -
                    kJniGuestAbiDataBegin.Value(),
                kJniGuestEnvironmentTable.Value());
        Write32(abi_data,
                kJniGuestJavaVm.Value() - kJniGuestAbiDataBegin.Value(),
                kJniGuestInvokeTable.Value());
        address_space.Write(kJniGuestAbiDataBegin, abi_data);

        address_space.Protect(environment_thunks,
                              memory::PageProtection::read |
                                  memory::PageProtection::execute);
        address_space.Protect(invoke_thunks, memory::PageProtection::read |
                                                memory::PageProtection::execute);
        address_space.Protect(data, memory::PageProtection::read);
    } catch (...) {
        if (data_mapped) address_space.Unmap(data);
        if (invoke_mapped) address_space.Unmap(invoke_thunks);
        if (environment_mapped) address_space.Unmap(environment_thunks);
        address_space_ = nullptr;
        page_size_ = 0;
        throw;
    }
}

GuestJniAbi::~GuestJniAbi() {
    if (address_space_ == nullptr) return;
    address_space_->Unmap(
        {kJniGuestAbiDataBegin, page_size_});
    address_space_->Unmap(
        {memory::GuestAddress{kJniInvokeThunkBegin}, page_size_});
    address_space_->Unmap(
        {memory::GuestAddress{kJniThunkBegin}, page_size_});
}

}  // namespace ogplay::runtime

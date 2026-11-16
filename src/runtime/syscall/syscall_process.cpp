#include "ogplay/runtime/syscall/syscall.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace ogplay::runtime {
namespace {

[[nodiscard]] std::string ReadOptionalName(memory::AddressSpace& address_space, const memory::GuestAddress address,
                                           const std::uint64_t thread_id) {
    if (address.Value() == 0) return {};
    std::string result;
    result.reserve(79);
    for (std::size_t index = 0; index < 80; ++index) {
        std::byte value{};
        address_space.Read(address.Add(index), std::span{&value, 1}, thread_id);
        const auto character = std::to_integer<unsigned char>(value);
        if (character == 0) return result;
        result.push_back(static_cast<char>(character));
    }
    throw std::invalid_argument("PR_SET_VMA name is not terminated");
}

}  // namespace

void BindAndroidProcessSyscalls(A32SyscallDispatcher& dispatcher, memory::AddressSpace& address_space,
                                GuestVmaAnnotationSink vma_annotation_sink) {
    constexpr std::int32_t kEsrch = 3;
    constexpr std::int32_t kEfault = 14;
    constexpr std::int32_t kEinval = 22;
    constexpr std::uint32_t kPrSetVma = 0x53564d41U;
    constexpr std::uint32_t kPrSetVmaAnonName = 0;
    if (!vma_annotation_sink) {
        throw SyscallError("VMA annotation sink is empty");
    }
    dispatcher.Implement(122, [&address_space](const A32SyscallFrame& frame) {
        constexpr std::size_t field_size = 65;
        constexpr std::array<std::string_view, 6> fields{kGuestSysname, kGuestNodename, kGuestRelease,
                                                         kGuestVersion, kGuestMachine,  ""};
        std::array<std::byte, field_size * fields.size()> bytes{};
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (!fields[i].empty()) std::memcpy(bytes.data() + i * field_size, fields[i].data(), fields[i].size());
        }
        if (frame.arguments[0] == 0) return -kEfault;
        try {
            address_space.Write(memory::GuestAddress{frame.arguments[0]}, bytes, frame.thread_id);
            return 0;
        } catch (const memory::MemoryFault&) {
            return -kEfault;
        } catch (const std::overflow_error&) {
            return -kEfault;
        }
    });
    dispatcher.Implement(172, [&address_space, sink = std::move(vma_annotation_sink)](const A32SyscallFrame& frame) {
        if (frame.thread_id == 0) return -kEsrch;
        if (frame.arguments[0] != kPrSetVma || frame.arguments[1] != kPrSetVmaAnonName || frame.arguments[3] == 0) {
            return -kEinval;
        }
        try {
            const memory::GuestRange range{memory::GuestAddress{frame.arguments[2]}, frame.arguments[3]};
            address_space.ValidateMapped(range, frame.thread_id);
            sink({range, ReadOptionalName(address_space, memory::GuestAddress{frame.arguments[4]}, frame.thread_id)});
            return 0;
        } catch (const memory::MemoryFault&) {
            return -kEfault;
        } catch (const std::exception&) {
            return -kEinval;
        }
    });
}

}  // namespace ogplay::runtime

#include "ogplay/runtime/syscall/arm_kernel_helpers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ogplay::runtime {
namespace {

void EncodeWord(std::vector<std::byte>& page, const std::uint32_t address,
                const std::uint32_t instruction) {
    const auto offset = static_cast<std::size_t>(address & 0xfffU);
    for (std::size_t index = 0; index < 4; ++index) {
        page[offset + index] = static_cast<std::byte>(
            (instruction >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
}

}  // namespace

void MapArmKernelHelpers(memory::AddressSpace& address_space) {
    const auto page_size = address_space.PageSize();
    if (page_size != 4096U) {
        throw std::runtime_error(
            "ARM kernel helper layout requires 4096-byte host pages");
    }
    const auto read_write = memory::PageProtection::read |
                            memory::PageProtection::write;
    address_space.Map({kArmKernelHelperPage, page_size}, read_write);
    std::vector<std::byte> page(static_cast<std::size_t>(page_size));
    for (std::size_t offset = 0; offset < page.size(); offset += 4) {
        EncodeWord(page, static_cast<std::uint32_t>(offset),
                   0xef000001U);  // unsupported helper: explicit SVC trap
    }

    // ARMv7 kuser ABI: barriers surround compare/exchange; C iff success.
    EncodeWord(page, kArmKernelMemoryBarrier.Value(), 0xf57ff05fU);  // dmb sy
    EncodeWord(page, kArmKernelMemoryBarrier.Add(4).Value(), 0xe12fff1eU);  // bx lr
    const std::array cmpxchg{
        0xf57ff05fU,  // dmb sy
        0xe1923f9fU,  // retry: ldrex r3, [r2]
        0xe0533000U,  // subs r3, r3, r0
        0x01823f91U,  // strexeq r3, r1, [r2]
        0x03330001U,  // teqeq r3, #1
        0x0afffffaU,  // beq retry (lost reservation)
        0xe2730000U,  // rsbs r0, r3, #0
        0xeaffffefU,  // b __kuser_memory_barrier
    };
    const std::array cmpxchg64{
        0xe92d40f2U,  // push {r1,r4,r5,r6,r7,lr}; preserve ABI registers
        0xe1c040d0U,  // ldrd r4, r5, [r0]
        0xe1c160d0U,  // ldrd r6, r7, [r1]
        0xf57ff05fU,  // dmb sy
        0xe1b20f9fU,  // retry: ldrexd r0, r1, [r2]
        0xe0303004U,  // eors r3, r0, r4
        0x00313005U,  // eorseq r3, r1, r5
        0x01a23f96U,  // strexdeq r3, r6, r7, [r2]
        0x03330001U,  // teqeq r3, #1
        0x0afffff9U,  // beq retry
        0xf57ff05fU,  // dmb sy
        0xe2730000U,  // rsbs r0, r3, #0
        0xe8bd40f2U,  // pop {r1,r4,r5,r6,r7,lr}
        0xe12fff1eU,  // bx lr
    };
    const auto encode = [&page](const memory::GuestAddress start, const auto& code) {
        for (std::size_t index = 0; index < code.size(); ++index) {
            EncodeWord(page, start.Add(index * sizeof(std::uint32_t)).Value(), code[index]);
        }
    };
    encode(kArmKernelCmpxchg, cmpxchg);
    encode(kArmKernelCmpxchg64, cmpxchg64);
    EncodeWord(page, kArmKernelGetTls.Value(),
               0xee1d0f70U);  // mrc p15,0,r0,c13,c0,3
    EncodeWord(page, kArmKernelGetTls.Add(4).Value(),
               0xe12fff1eU);  // bx lr
    EncodeWord(page, 0xffff0ffcU, 5U);  // Linux kuser helper ABI version
    address_space.Write(kArmKernelHelperPage, page);
    address_space.Protect({kArmKernelHelperPage, page_size},
                          memory::PageProtection::read |
                              memory::PageProtection::execute);
}

}  // namespace ogplay::runtime

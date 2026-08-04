#include "ogplay/runtime/arm_kernel_helpers.h"

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

    EncodeWord(page, kArmKernelMemoryBarrier.Value(),
               0xe12fff1eU);  // bx lr; AddressSpace locks order accesses
    const std::array cmpxchg{
        0xe1923f9fU,  // ldrex r3, [r2]
        0xe1530000U,  // cmp r3, r0
        0x01823f91U,  // strexeq r3, r1, [r2]
        0x13a03001U,  // movne r3, #1
        0xe3530000U,  // cmp r3, #0
        0x03a00000U,  // moveq r0, #0
        0x13e00000U,  // mvnne r0, #0
        0xe12fff1eU,  // bx lr
    };
    for (std::size_t index = 0; index < cmpxchg.size(); ++index) {
        EncodeWord(page,
                   kArmKernelCmpxchg.Add(index * sizeof(std::uint32_t)).Value(),
                   cmpxchg[index]);
    }
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

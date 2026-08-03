#pragma once

#include <array>
#include <cstdint>

namespace ogplay::samples::m1_guest {

inline constexpr std::uint32_t kCodeAddress = 0x00010000;
inline constexpr std::uint32_t kMailboxAddress = 0x00020000;
inline constexpr std::uint32_t kInputOffset = 0;
inline constexpr std::uint32_t kOutputOffset = 4;
inline constexpr std::uint32_t kSequenceOffset = 8;
inline constexpr std::uint32_t kA32SupervisorCall = 0x004d31;
inline constexpr std::uint32_t kThumbSupervisorCall = 0x31;

// r4 points at the mailbox. Computes output = input * 3 + 7 and sequence += 1.
inline constexpr std::array<std::uint32_t, 9> kA32Program{
    0xe5940000,  // ldr r0, [r4]
    0xe0801000,  // add r1, r0, r0
    0xe0811000,  // add r1, r1, r0
    0xe2811007,  // add r1, r1, #7
    0xe5841004,  // str r1, [r4, #4]
    0xe5942008,  // ldr r2, [r4, #8]
    0xe2822001,  // add r2, r2, #1
    0xe5842008,  // str r2, [r4, #8]
    0xef004d31,  // svc #0x4d31
};

// r4 points at the mailbox. Computes output = input + 7 and sequence += 1.
inline constexpr std::array<std::uint16_t, 7> kThumbProgram{
    0x6820,  // ldr r0, [r4]
    0x3007,  // adds r0, #7
    0x6060,  // str r0, [r4, #4]
    0x68a1,  // ldr r1, [r4, #8]
    0x3101,  // adds r1, #1
    0x60a1,  // str r1, [r4, #8]
    0xdf31,  // svc #0x31
};

[[nodiscard]] constexpr std::uint32_t ExpectedA32Output(const std::uint32_t input) {
    return input * 3U + 7U;
}

[[nodiscard]] constexpr std::uint32_t ExpectedThumbOutput(const std::uint32_t input) {
    return input + 7U;
}

}  // namespace ogplay::samples::m1_guest

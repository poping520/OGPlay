#pragma once

#include <cstdint>

namespace ogplay::runtime {

inline constexpr std::int32_t kGuestProcessId = 1000;
inline constexpr std::int32_t kGuestParentProcessId = 1;

inline constexpr char kGuestSysname[] = "Linux";
inline constexpr char kGuestNodename[] = "localhost";
inline constexpr char kGuestRelease[] = "3.4.0";
inline constexpr char kGuestVersion[] = "API 19";
inline constexpr char kGuestMachine[] = "armv7l";

}  // namespace ogplay::runtime

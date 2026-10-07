#pragma once
#include <cstdint>
#include "ogplay/memory/address.h"

namespace ogplay::runtime {
struct BitmapJavaReference final { std::uint32_t token{}; };
struct AndroidBitmapInfo final {
    std::uint32_t width{}, height{}, stride{};
    std::int32_t format{};
    std::uint32_t flags{};
};
// Owner-thread setup/retirement; no hook replacement during active guest calls.
struct AndroidBitmapHooks final {
    void* owner{};
    std::int32_t (*info)(void*, std::uint64_t, memory::GuestAddress,
                         BitmapJavaReference, AndroidBitmapInfo&){};
    std::int32_t (*lock)(void*, std::uint64_t, memory::GuestAddress,
                         BitmapJavaReference, memory::GuestAddress&){};
    std::int32_t (*unlock)(void*, std::uint64_t, memory::GuestAddress,
                           BitmapJavaReference){};
};
}

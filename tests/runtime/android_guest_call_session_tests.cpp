#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/integration/android_guest_call_session.h"

TEST_CASE("Android guest call session validates its complete launch request") {
    ogplay::runtime::VirtualFileSystem filesystem;
    const std::array<std::byte, 4> bytes{};
    const ogplay::loader::Elf32ModuleInput module{
        "root.so", bytes, ogplay::memory::GuestAddress{0x10000000U}};

    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::runtime::AndroidGuestCallSession::Start(
            {19, "", std::span{&module, 1}, {}, 64, 36,
             1000, 1, &filesystem, {}})),
        "Android guest call session request is incomplete",
        ogplay::runtime::AndroidGuestCallSessionError);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::runtime::AndroidGuestCallSession::Start(
            {19, "root.so", std::span{&module, 1}, {}, 64, 36,
             1000, 1, nullptr, {}})),
        "Android guest call session request is incomplete",
        ogplay::runtime::AndroidGuestCallSessionError);
}

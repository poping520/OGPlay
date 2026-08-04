#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "ogplay/runtime/guest_lifecycle.h"

namespace {

[[nodiscard]] ogplay::runtime::GuestLifecycleModule Module(
    const std::size_t index, const std::uint32_t bias,
    const std::uint32_t init, const std::uint32_t fini) {
    ogplay::loader::Elf32LifecycleInfo lifecycle;
    lifecycle.init = ogplay::memory::GuestAddress{init};
    lifecycle.fini = ogplay::memory::GuestAddress{fini};
    lifecycle.init_array = {ogplay::memory::GuestAddress{0},
                            ogplay::memory::GuestAddress{0xffffffffU},
                            ogplay::memory::GuestAddress{0x201U}};
    lifecycle.fini_array = {ogplay::memory::GuestAddress{0x301U},
                            ogplay::memory::GuestAddress{0x401U}};
    return {index, ogplay::memory::GuestAddress{bias}, lifecycle};
}

}  // namespace

TEST_CASE("guest lifecycle follows linker module and Bionic function order") {
    const std::vector modules{Module(2, 0x10000U, 0x101U, 0x501U),
                              Module(3, 0x20000U, 0x101U, 0x501U)};
    const std::vector<std::size_t> init_order{3, 2};
    const auto init = ogplay::runtime::BuildGuestInitializationPlan(
        modules, init_order);
    REQUIRE(init.size() == 4);
    CHECK(init[0] == ogplay::runtime::GuestLifecycleCall{
                         3, ogplay::memory::GuestAddress{0x20101U},
                         ogplay::runtime::GuestLifecycleKind::dynamic_init});
    CHECK(init[1].address == ogplay::memory::GuestAddress{0x20201U});
    CHECK(init[2].module_index == 2);
    CHECK(init[3].address == ogplay::memory::GuestAddress{0x10201U});

    std::vector<ogplay::runtime::GuestLifecycleCall> invoked;
    ogplay::runtime::ExecuteGuestLifecycle(
        init, [&invoked](const auto& call) { invoked.push_back(call); });
    CHECK(invoked == init);

    const std::vector<std::size_t> fini_order{2, 3};
    const auto fini = ogplay::runtime::BuildGuestFinalizationPlan(
        modules, fini_order);
    REQUIRE(fini.size() == 6);
    CHECK(fini[0].address == ogplay::memory::GuestAddress{0x10401U});
    CHECK(fini[1].address == ogplay::memory::GuestAddress{0x10301U});
    CHECK(fini[2].kind ==
          ogplay::runtime::GuestLifecycleKind::dynamic_fini);
    CHECK(fini[3].module_index == 3);
    CHECK(fini[5].address == ogplay::memory::GuestAddress{0x20501U});
}

TEST_CASE("guest lifecycle validates the complete plan before invocation") {
    const std::vector modules{Module(2, 0xfffff000U, 0x2000U, 0x501U)};
    const std::vector<std::size_t> order{2};
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::BuildGuestInitializationPlan(
            modules, order)),
        ogplay::runtime::GuestLifecycleError);
    const std::vector<std::size_t> unknown{3};
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::BuildGuestFinalizationPlan(
            modules, unknown)),
        ogplay::runtime::GuestLifecycleError);
    const std::vector<std::size_t> duplicate{2, 2};
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::BuildGuestFinalizationPlan(
            modules, duplicate)),
        ogplay::runtime::GuestLifecycleError);
    CHECK_THROWS_AS(ogplay::runtime::ExecuteGuestLifecycle({}, {}),
                    ogplay::runtime::GuestLifecycleError);
}

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/loader/dynamic_linker.h"

namespace {

[[nodiscard]] ogplay::loader::Elf32Symbol Symbol(
    std::string name, const std::uint32_t value,
    const std::uint16_t section) {
    return {std::move(name), ogplay::memory::GuestAddress{value}, 4, 1,
            2, 0, section};
}

[[nodiscard]] ogplay::loader::Elf32LinkModule Module(
    std::string name, const std::uint32_t bias,
    std::vector<std::string> needed,
    std::vector<ogplay::loader::Elf32Symbol> symbols) {
    ogplay::loader::Elf32LinkModule module;
    module.name = std::move(name);
    module.load_bias = ogplay::memory::GuestAddress{bias};
    module.dynamic.soname = module.name;
    module.dynamic.needed = std::move(needed);
    module.symbols.symbols.push_back(
        {"", ogplay::memory::GuestAddress{}, 0, 0, 0, 0, 0});
    for (auto& symbol : symbols) {
        module.symbols.symbols.push_back(std::move(symbol));
    }
    return module;
}

[[nodiscard]] ogplay::loader::Elf32DynamicLinker BaseLinker() {
    const std::vector modules{
        Module("app.so", 0x10000, {"libbase.so"}, {}),
        Module("libbase.so", 0x20000, {},
               {Symbol("base", 0x100, 1)}),
    };
    return ogplay::loader::Elf32DynamicLinker(
        ogplay::loader::BuildElf32LinkNamespace("app.so", modules));
}

}  // namespace

TEST_CASE("ELF dynamic linker opens resolves references and closes a root") {
    auto linker = BaseLinker();
    const std::vector additions{
        Module("plugin.so", 0x30000, {"libbase.so", "libdep.so"},
               {Symbol("plugin", 0x100, 1)}),
        Module("libdep.so", 0x40000, {},
               {Symbol("dependency", 0x200, 1)}),
    };
    const auto opened = linker.Open("plugin.so", additions);
    CHECK(opened.handle != 0);
    CHECK(opened.reference_count == 1);
    CHECK(opened.initialization_order ==
          std::vector<std::size_t>{3, 2});
    CHECK(linker.Symbol(opened.handle, "plugin").address ==
          ogplay::memory::GuestAddress{0x30100});
    CHECK(linker.Symbol(opened.handle, "base").address ==
          ogplay::memory::GuestAddress{0x20100});
    CHECK_THROWS_AS(static_cast<void>(linker.Symbol(opened.handle, "")),
                    ogplay::loader::LinkError);
    CHECK_THROWS_AS(static_cast<void>(linker.Open("plugin.so", additions)),
                    ogplay::loader::LinkError);

    const auto reopened = linker.Open("plugin.so");
    CHECK(reopened.handle == opened.handle);
    CHECK(reopened.reference_count == 2);
    CHECK(reopened.initialization_order.empty());
    CHECK(linker.Close(opened.handle).reference_count == 1);
    const auto closed = linker.Close(opened.handle);
    CHECK(closed.reference_count == 0);
    CHECK(closed.finalization_order ==
          std::vector<std::size_t>{2, 3});
    CHECK_THROWS_AS(static_cast<void>(linker.Symbol(opened.handle, "plugin")),
                    ogplay::loader::LinkError);
}

TEST_CASE("ELF dynamic linker retains shared dependencies until the last close") {
    auto linker = BaseLinker();
    const std::vector first_additions{
        Module("first.so", 0x30000, {"shared.so"},
               {Symbol("first", 0x100, 1)}),
        Module("shared.so", 0x40000, {},
               {Symbol("shared", 0x100, 1)}),
    };
    const auto first = linker.Open("first.so", first_additions);
    CHECK(first.initialization_order ==
          std::vector<std::size_t>{3, 2});

    const std::vector second_additions{
        Module("second.so", 0x50000, {"shared.so"},
               {Symbol("second", 0x100, 1)}),
    };
    const auto second = linker.Open("second.so", second_additions);
    CHECK(second.initialization_order == std::vector<std::size_t>{4});
    CHECK(linker.Close(first.handle).finalization_order ==
          std::vector<std::size_t>{2});
    CHECK(linker.Symbol(second.handle, "shared").address ==
          ogplay::memory::GuestAddress{0x40100});
    CHECK(linker.Close(second.handle).finalization_order ==
          std::vector<std::size_t>{4, 3});

    const auto reloaded = linker.Open("first.so");
    CHECK(reloaded.handle != first.handle);
    CHECK(reloaded.initialization_order ==
          std::vector<std::size_t>{3, 2});
    CHECK_THROWS_AS(static_cast<void>(linker.Close(first.handle)),
                    ogplay::loader::LinkError);
}

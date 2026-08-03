#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ogplay/loader/link_namespace.h"

namespace {

[[nodiscard]] ogplay::loader::Elf32Symbol Symbol(
    std::string name, const std::uint32_t value, const std::uint8_t binding,
    const std::uint8_t visibility, const std::uint16_t section) {
    return {std::move(name), ogplay::memory::GuestAddress{value}, 4, binding,
            2, visibility, section};
}

[[nodiscard]] ogplay::loader::Elf32LinkModule Module(
    std::string name, const std::uint32_t bias,
    std::vector<std::string> needed,
    std::vector<ogplay::loader::Elf32Symbol> symbols) {
    ogplay::loader::Elf32LinkModule module;
    module.name = std::move(name);
    module.load_bias = ogplay::memory::GuestAddress{bias};
    module.dynamic.needed = std::move(needed);
    module.dynamic.soname = module.name;
    module.symbols.symbols.push_back(Symbol("", 0, 0, 0, 0));
    for (auto& symbol : symbols) {
        module.symbols.symbols.push_back(std::move(symbol));
    }
    return module;
}

}  // namespace

TEST_CASE("ELF link namespace builds dependency and breadth first lookup orders") {
    std::vector modules{
        Module("app.so", 0x10000, {"liba.so", "libb.so"},
               {Symbol("foo", 0, 1, 0, 0),
                Symbol("optional", 0, 2, 0, 0),
                Symbol("private_value", 0x180, 1, 2, 1),
                Symbol("protected_value", 0x1a0, 1, 3, 1)}),
        Module("liba.so", 0x20000, {"libc.so"},
               {Symbol("foo", 0x100, 1, 0, 1),
                Symbol("protected_value", 0x200, 1, 0, 1)}),
        Module("libb.so", 0x30000, {},
               {Symbol("foo", 0x300, 1, 0, 1)}),
        Module("libc.so", 0x40000, {},
               {Symbol("bar", 0x400, 1, 0, 1)}),
    };
    const auto link_namespace =
        ogplay::loader::BuildElf32LinkNamespace("app.so", modules);
    CHECK(link_namespace.load_order ==
          std::vector<std::size_t>{3, 1, 2, 0});
    CHECK(link_namespace.lookup_scope ==
          std::vector<std::size_t>{0, 1, 2, 3});
    const auto foo = ogplay::loader::LookupElf32Symbol(link_namespace, "foo");
    CHECK(foo.module_index == 1);
    CHECK(foo.address == ogplay::memory::GuestAddress{0x20100});

    const auto resolved =
        ogplay::loader::ResolveElf32Symbols(link_namespace, 0);
    REQUIRE(resolved.values.size() == 5);
    REQUIRE(resolved.values[1].has_value());
    CHECK(*resolved.values[1] == ogplay::memory::GuestAddress{0x20100});
    REQUIRE(resolved.values[2].has_value());
    CHECK(resolved.values[2]->IsNull());
    CHECK(*resolved.values[3] == ogplay::memory::GuestAddress{0x10180});
    CHECK(*resolved.values[4] == ogplay::memory::GuestAddress{0x101a0});
}

TEST_CASE("ELF link namespace rejects missing ambiguous and unresolved inputs") {
    SUBCASE("missing dependency") {
        const std::vector modules{Module("app.so", 0x10000, {"missing.so"}, {})};
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::BuildElf32LinkNamespace("app.so", modules)),
                        ogplay::loader::LinkError);
    }
    SUBCASE("ambiguous soname") {
        auto first = Module("first.so", 0x10000, {}, {});
        auto second = Module("second.so", 0x20000, {}, {});
        second.dynamic.soname = "first.so";
        const std::vector modules{first, second};
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::BuildElf32LinkNamespace("first.so", modules)),
                        ogplay::loader::LinkError);
    }
    SUBCASE("unresolved strong symbol") {
        const std::vector modules{Module(
            "app.so", 0x10000, {}, {Symbol("missing", 0, 1, 0, 0)})};
        const auto link_namespace =
            ogplay::loader::BuildElf32LinkNamespace("app.so", modules);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ResolveElf32Symbols(link_namespace, 0)),
                        ogplay::loader::LinkError);
    }
}

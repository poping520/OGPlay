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

[[nodiscard]] ogplay::loader::Elf32SymbolVersion Version(
    const std::uint16_t index,
    const ogplay::loader::Elf32SymbolVersionKind kind,
    std::string name = {}, std::string dependency = {},
    const bool hidden = false) {
    return {index, hidden, kind, std::move(name), std::move(dependency)};
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

TEST_CASE("ELF link namespace extends transactionally with a per-root scope") {
    const std::vector base_modules{
        Module("app.so", 0x10000, {"liba.so"}, {}),
        Module("liba.so", 0x20000, {},
               {Symbol("base", 0x100, 1, 0, 1)}),
    };
    const auto base =
        ogplay::loader::BuildElf32LinkNamespace("app.so", base_modules);
    const std::vector new_modules{
        Module("plugin.so", 0x30000, {"liba.so", "libnew.so"},
               {Symbol("base", 0, 1, 0, 0),
                Symbol("added", 0, 1, 0, 0)}),
        Module("libnew.so", 0x40000, {},
               {Symbol("added", 0x200, 1, 0, 1)}),
    };
    const auto extension = ogplay::loader::ExtendElf32LinkNamespace(
        base, "plugin.so", new_modules);

    CHECK(base.modules.size() == 2);
    CHECK(extension.link_namespace.modules.size() == 4);
    CHECK(extension.link_namespace.lookup_scope ==
          std::vector<std::size_t>{0, 1});
    CHECK(extension.scope.lookup_scope ==
          std::vector<std::size_t>{2, 1, 3});
    CHECK(extension.scope.load_order ==
          std::vector<std::size_t>{1, 3, 2});
    CHECK(extension.newly_loaded == std::vector<std::size_t>{3, 2});

    const auto resolved = ogplay::loader::ResolveElf32Symbols(
        extension.link_namespace, extension.scope, 2);
    CHECK(*resolved.values[1] == ogplay::memory::GuestAddress{0x20100});
    CHECK(*resolved.values[2] == ogplay::memory::GuestAddress{0x40200});
}

TEST_CASE("ELF link namespace matches required and default symbol versions") {
    auto app = Module("app.so", 0x10000, {"liba.so"},
                      {Symbol("foo", 0, 1, 0, 0)});
    app.versions = ogplay::loader::Elf32SymbolVersionTable{{
        Version(0, ogplay::loader::Elf32SymbolVersionKind::local),
        Version(2, ogplay::loader::Elf32SymbolVersionKind::requirement,
                "LIB_1.0", "liba.so"),
    }};
    auto library = Module(
        "liba.so", 0x20000, {},
        {Symbol("foo", 0x100, 1, 0, 1), Symbol("foo", 0x200, 1, 0, 1)});
    library.versions = ogplay::loader::Elf32SymbolVersionTable{{
        Version(0, ogplay::loader::Elf32SymbolVersionKind::local),
        Version(2, ogplay::loader::Elf32SymbolVersionKind::definition,
                "LIB_1.0", {}, true),
        Version(3, ogplay::loader::Elf32SymbolVersionKind::definition,
                "LIB_2.0"),
    }};
    const std::vector modules{app, library};
    const auto link_namespace =
        ogplay::loader::BuildElf32LinkNamespace("app.so", modules);
    const auto resolved =
        ogplay::loader::ResolveElf32Symbols(link_namespace, 0);
    CHECK(*resolved.values[1] == ogplay::memory::GuestAddress{0x20100});

    auto local_app = app;
    local_app.versions->symbols[1] =
        Version(0, ogplay::loader::Elf32SymbolVersionKind::local);
    const std::vector local_modules{local_app, library};
    const auto local_namespace =
        ogplay::loader::BuildElf32LinkNamespace("app.so", local_modules);
    const auto local_resolved =
        ogplay::loader::ResolveElf32Symbols(local_namespace, 0);
    CHECK(*local_resolved.values[1] ==
          ogplay::memory::GuestAddress{0x20200});

    const auto default_symbol =
        ogplay::loader::LookupElf32Symbol(link_namespace, "foo");
    CHECK(default_symbol.symbol_index == 2);
    CHECK(default_symbol.address == ogplay::memory::GuestAddress{0x20200});

    SUBCASE("legacy Android provider resolves a versioned NDK import by name") {
        library.versions.reset();
        const std::vector legacy_modules{app, library};
        const auto legacy_namespace =
            ogplay::loader::BuildElf32LinkNamespace("app.so", legacy_modules);
        const auto legacy =
            ogplay::loader::ResolveElf32Symbols(legacy_namespace, 0);
        CHECK(*legacy.values[1] ==
              ogplay::memory::GuestAddress{0x20100});
    }
}

TEST_CASE("ELF namespace extension rejects collisions and unreachable inputs") {
    const std::vector modules{Module("app.so", 0x10000, {}, {})};
    const auto link_namespace =
        ogplay::loader::BuildElf32LinkNamespace("app.so", modules);
    SUBCASE("alias collides with a loaded module") {
        auto duplicate = Module("other.so", 0x20000, {}, {});
        duplicate.dynamic.soname = "app.so";
        const std::vector additions{duplicate};
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ExtendElf32LinkNamespace(
                                link_namespace, "other.so", additions)),
                        ogplay::loader::LinkError);
    }
    SUBCASE("new module is unreachable from the dynamic root") {
        const std::vector additions{
            Module("plugin.so", 0x20000, {}, {}),
            Module("orphan.so", 0x30000, {}, {})};
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ExtendElf32LinkNamespace(
                                link_namespace, "plugin.so", additions)),
                        ogplay::loader::LinkError);
    }
    SUBCASE("version table size must equal dynsym") {
        auto invalid = Module("invalid.so", 0x20000, {}, {});
        invalid.versions = ogplay::loader::Elf32SymbolVersionTable{};
        const std::vector additions{invalid};
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ExtendElf32LinkNamespace(
                                link_namespace, "invalid.so", additions)),
                        ogplay::loader::LinkError);
    }
}

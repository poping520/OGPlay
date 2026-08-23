#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/bionic/guest_symbol_override.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "runtime/boundary/modules/module_catalog.h"
#include "runtime/boundary/modules/opensles/opensles_abi.h"

namespace {

[[nodiscard]] ogplay::loader::Elf32Symbol Symbol(
    std::string name, const std::uint32_t value, const std::uint8_t binding,
    const std::uint16_t section) {
    return {std::move(name), ogplay::memory::GuestAddress{value}, 4, binding,
            2, 0, section};
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
    module.symbols.symbols.push_back(Symbol("", 0, 0, 0));
    for (auto& symbol : symbols) {
        module.symbols.symbols.push_back(std::move(symbol));
    }
    return module;
}

}  // namespace

TEST_CASE("Bionic profiles select only API 19 22 and 23") {
    const auto& api19 = ogplay::runtime::SelectBionicProfile(19);
    const auto& api22 = ogplay::runtime::SelectBionicProfile(22);
    const auto& api23 = ogplay::runtime::SelectBionicProfile(23);
    CHECK(api19.api == ogplay::runtime::AndroidApi::api19);
    CHECK(api19.android_release == "4.4");
    CHECK(api19.data_directory == "bionic/19");
    CHECK(api22.api == ogplay::runtime::AndroidApi::api22);
    CHECK(api22.data_directory == "bionic/22");
    CHECK(api23.api == ogplay::runtime::AndroidApi::api23);
    CHECK(api23.data_directory == "bionic/23");
    CHECK(api19.guest_libraries.size() == 5);
    CHECK(ogplay::runtime::AndroidBoundaryCatalog(api19.api).Modules().size() == 6);
    CHECK_THROWS_AS(static_cast<void>(ogplay::runtime::SelectBionicProfile(21)),
                    ogplay::runtime::BionicProfileError);
}

TEST_CASE("Bionic routing separates guest intercept and HLE boundary symbols") {
    const auto& profile = ogplay::runtime::SelectBionicProfile(19);
    CHECK(ogplay::runtime::RouteBionicSymbol(profile, "libc.so", "snprintf") ==
          ogplay::runtime::BionicSymbolRoute::guest_execution);
    CHECK(ogplay::runtime::RouteBionicSymbol(profile, "libc.so", "memcpy") ==
          ogplay::runtime::BionicSymbolRoute::host_intercept);
    CHECK(ogplay::runtime::RouteBionicSymbol(profile, "libc.so",
                                             "pthread_create") ==
          ogplay::runtime::BionicSymbolRoute::guest_execution);
    CHECK(ogplay::runtime::RouteBionicSymbol(profile, "libEGL.so", "eglSwapBuffers") ==
          ogplay::runtime::BionicSymbolRoute::host_boundary);
    CHECK(ogplay::runtime::RouteBionicSymbol(profile, "liblog.so",
                                             "__android_log_write") ==
          ogplay::runtime::BionicSymbolRoute::host_boundary);
    CHECK(ogplay::runtime::RouteBionicSymbol(profile, "libOpenSLES.so",
                                             "slCreateEngine") ==
          ogplay::runtime::BionicSymbolRoute::host_boundary);
    const auto* open_sles = ogplay::runtime::AndroidBoundaryCatalog(profile.api)
                                .FindModule("libOpenSLES.so");
    REQUIRE(open_sles != nullptr);
    CHECK(open_sles->exports.size() == ogplay::runtime::OpenSlesIids().size());
    CHECK(std::all_of(open_sles->exports.begin(), open_sles->exports.end(),
                      [](const auto& export_) {
                          return export_.kind ==
                                 ogplay::runtime::BoundaryExportKind::public_data;
                      }));
    CHECK(ogplay::runtime::AndroidBoundaryCatalog(profile.api)
              .FindModule("libc.so") == nullptr);
    CHECK(ogplay::runtime::GuestSymbolOverrides().size() == 5U);
    CHECK_THROWS_AS(static_cast<void>(ogplay::runtime::RouteBionicSymbol(
                        profile, "libc.so", "")),
                    ogplay::runtime::BionicProfileError);
}

TEST_CASE("Boundary catalog filters inactive modules and exports across APIs") {
    using namespace ogplay::runtime;
    static constexpr std::array exports{
        BoundaryExportDefinition{"always", 7U, 0U, {}},
        BoundaryExportDefinition{"since22", 11U, 1U,
                                 {AndroidApi::api22, AndroidApi::api23}},
    };
    static constexpr std::array modules{
        BoundaryModuleDefinition{"libactive.so", {}, exports},
        BoundaryModuleDefinition{"libsince23.so",
                                 {AndroidApi::api23, AndroidApi::api23},
                                 exports},
    };
    const BoundaryCatalog api19(AndroidApi::api19, modules);
    REQUIRE(api19.Modules().size() == 1U);
    CHECK(api19.Modules()[0].exports.size() == 1U);
    CHECK(api19.Modules()[0].exports[0].local_id == 7U);
    CHECK(api19.SlotCount() == 1U);

    const BoundaryCatalog api22(AndroidApi::api22, modules);
    REQUIRE(api22.Modules().size() == 1U);
    CHECK(api22.Modules()[0].exports.size() == 2U);
    CHECK(api22.Modules()[0].exports[1].local_id == 11U);

    const BoundaryCatalog api23(AndroidApi::api23, modules);
    CHECK(api23.Modules().size() == 2U);
    CHECK(api23.SlotCount() == 4U);
}

TEST_CASE("export-less Virtual SO remains active without publishing handlers") {
    using namespace ogplay::runtime;
    static constexpr std::array<BoundaryExportDefinition, 0> no_exports{};
    static constexpr std::array definitions{
        BoundaryModuleDefinition{"libempty.so", {}, no_exports}};
    const BoundaryCatalog catalog(AndroidApi::api19, definitions);
    REQUIRE(catalog.Modules().size() == 1U);
    CHECK(catalog.FindModule("libempty.so") != nullptr);
    CHECK(catalog.Modules()[0].exports.empty());
    CHECK(catalog.SlotCount() == 0U);
}

TEST_CASE("Boundary catalog data exports preserve addresses without callable slots") {
    using namespace ogplay::runtime;
    static constexpr std::array exports{
        BoundaryExportDefinition{"global", 0U, 0U, {},
                                 BoundaryExportKind::public_data,
                                 ogplay::memory::GuestAddress{0x71502000U}, 16U},
        BoundaryExportDefinition{"call", 9U, 2U}};
    static constexpr std::array modules{
        BoundaryModuleDefinition{"libmixed.so", {}, exports}};
    const BoundaryCatalog catalog(AndroidApi::api19, modules);
    REQUIRE(catalog.Modules().size() == 1U);
    REQUIRE(catalog.Modules()[0].exports.size() == 2U);
    CHECK(catalog.SlotCount() == 1U);
    CHECK(catalog.Modules()[0].exports[0].address ==
          ogplay::memory::GuestAddress{0x71502000U});
    CHECK(catalog.Modules()[0].exports[0].size == 16U);
    CHECK(catalog.Modules()[0].exports[1].address ==
          ogplay::memory::GuestAddress{ogplay::runtime::kBionicHleThunkBegin + 1U});
}

TEST_CASE("libOpenSLES Virtual SO publishes AOSP IID data globals") {
    const auto& profile = ogplay::runtime::SelectBionicProfile(19);
    const auto symbols = ogplay::runtime::detail::BuildAndroidBoundarySymbols();
    const ogplay::runtime::BionicHleSymbolProvider hle(symbols);
    const std::vector modules{
        Module("app.so", 0x10000000U, {"libOpenSLES.so"}, {})};
    const auto link_namespace = ogplay::runtime::BuildBionicLinkNamespace(
        profile, "app.so", modules, hle);
    REQUIRE(link_namespace.modules.size() == 2U);
    CHECK(link_namespace.modules[1].dynamic.soname == "libOpenSLES.so");
    REQUIRE(link_namespace.modules[1].symbols.symbols.size() ==
            ogplay::runtime::OpenSlesIids().size() + 1U);
    CHECK(link_namespace.modules[1].symbols.symbols[0].name.empty());
    const auto object = std::find_if(
        link_namespace.modules[1].symbols.symbols.begin(),
        link_namespace.modules[1].symbols.symbols.end(), [](const auto& symbol) {
            return symbol.name == "SL_IID_OBJECT";
        });
    REQUIRE(object != link_namespace.modules[1].symbols.symbols.end());
    const auto object_iid = std::find_if(
        ogplay::runtime::OpenSlesIids().begin(),
        ogplay::runtime::OpenSlesIids().end(), [](const auto& iid) {
            return iid.name == "SL_IID_OBJECT";
        });
    REQUIRE(object_iid != ogplay::runtime::OpenSlesIids().end());
    CHECK(object->value == object_iid->variable_address);
    CHECK(object->size == 4U);
    CHECK(object->type == 1U);

    const std::vector importing_modules{Module(
        "importer.so", 0x11000000U, {"libOpenSLES.so"},
        {Symbol("slCreateEngine", 0, 1, 0)})};
    const auto importing_namespace =
        ogplay::runtime::BuildBionicLinkNamespace(
            profile, "importer.so", importing_modules, hle);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::ResolveElf32Symbols(
            importing_namespace, 0)),
        ogplay::loader::LinkError);
}

TEST_CASE("Bionic memory intercepts preserve libc behavior") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress page{0x10000U};
    memory.Map({page, memory.PageSize() * 2U},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    const std::array source{std::byte{'a'}, std::byte{'b'}, std::byte{'c'},
                            std::byte{}};
    memory.Write(page, source);
    CHECK(ogplay::runtime::ExecuteBionicMemoryIntercept(
              memory, {"memcpy", {page.Add(32).Value(), page.Value(), 4, 0},
                       41}) == page.Add(32).Value());
    CHECK(ogplay::runtime::ExecuteBionicMemoryIntercept(
              memory, {"strlen", {page.Add(32).Value(), 0, 0, 0}, 41}) == 3);
    CHECK(ogplay::runtime::ExecuteBionicMemoryIntercept(
              memory, {"memcmp", {page.Value(), page.Add(32).Value(), 4, 0},
                       41}) == 0);
    CHECK(ogplay::runtime::ExecuteBionicMemoryIntercept(
              memory, {"memset", {page.Add(33).Value(), 'x', 2, 0}, 41}) ==
          page.Add(33).Value());
    CHECK(ogplay::runtime::ExecuteBionicMemoryIntercept(
              memory, {"memmove", {page.Add(1).Value(), page.Value(), 3, 0},
                       41}) == page.Add(1).Value());
    std::array<std::byte, 4> moved{};
    memory.Read(page, moved);
    CHECK(moved == std::array{std::byte{'a'}, std::byte{'a'},
                              std::byte{'b'}, std::byte{'c'}});
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::ExecuteBionicMemoryIntercept(
            memory, {"strcmp", {page.Value(), page.Value(), 0, 0}, 41})),
        ogplay::runtime::BionicProfileError);
}

TEST_CASE("Bionic memcpy intercept meets the M2 throughput floor") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress source{0x10000U};
    constexpr std::uint32_t kBytes = 1024U * 1024U;
    const ogplay::memory::GuestAddress destination{0x200000U};
    memory.Map({source, kBytes}, ogplay::memory::PageProtection::read |
                                     ogplay::memory::PageProtection::write);
    memory.Map({destination, kBytes},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    const auto start = std::chrono::steady_clock::now();
    constexpr std::uint32_t kIterations = 8;
    for (std::uint32_t index = 0; index < kIterations; ++index) {
        CHECK(ogplay::runtime::ExecuteBionicMemoryIntercept(
                  memory, {"memcpy", {destination.Value(), source.Value(),
                                       kBytes, 0}, 51}) ==
              destination.Value());
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto seconds = std::chrono::duration<double>(elapsed).count();
    const auto mebibytes_per_second =
        static_cast<double>(kIterations) / seconds;
    INFO("Bionic memcpy intercept MiB/s: " << mebibytes_per_second);
    CHECK(mebibytes_per_second >= 4.0);
}

TEST_CASE("Bionic link namespace combines guest libraries and observable HLE thunks") {
    const auto& profile = ogplay::runtime::SelectBionicProfile(19);
    const std::vector hle_bindings{
        ogplay::runtime::BionicHleSymbol{
            "libc.so", "memcpy", ogplay::memory::GuestAddress{0x70000101}},
        ogplay::runtime::BionicHleSymbol{
            "liblog.so", "__android_log_write",
            ogplay::memory::GuestAddress{0x70000200}},
    };
    const ogplay::runtime::BionicHleSymbolProvider hle(hle_bindings);
    const std::vector modules{
        Module("app.so", 0x10000000, {"libc.so", "liblog.so"},
               {Symbol("memcpy", 0, 1, 0), Symbol("snprintf", 0, 1, 0),
                Symbol("__android_log_write", 0, 1, 0)}),
        Module("libc.so", 0x18000000, {},
               {Symbol("memcpy", 0x100, 1, 1),
                Symbol("snprintf", 0x200, 1, 1)}),
    };

    const auto link_namespace = ogplay::runtime::BuildBionicLinkNamespace(
        profile, "app.so", modules, hle);
    CHECK(link_namespace.load_order ==
          std::vector<std::size_t>{1, 2, 0});
    CHECK(link_namespace.lookup_scope ==
          std::vector<std::size_t>{0, 1, 2});

    const auto resolved =
        ogplay::loader::ResolveElf32Symbols(link_namespace, 0);
    REQUIRE(resolved.values[1].has_value());
    CHECK(*resolved.values[1] == ogplay::memory::GuestAddress{0x70000101});
    REQUIRE(resolved.values[2].has_value());
    CHECK(*resolved.values[2] == ogplay::memory::GuestAddress{0x18000200});
    REQUIRE(resolved.values[3].has_value());
    CHECK(*resolved.values[3] == ogplay::memory::GuestAddress{0x70000200});

    const auto symbolized = hle.Resolve(0x70000200);
    REQUIRE(symbolized.has_value());
    CHECK(symbolized->module == "liblog.so");
    CHECK(symbolized->symbol == "__android_log_write");
    CHECK(symbolized->source_hint == "hle");
}

TEST_CASE("Bionic namespace rejects invalid thunks and unresolved boundaries") {
    const auto& profile = ogplay::runtime::SelectBionicProfile(23);
    SUBCASE("thunk address outside reserved range") {
        const std::vector symbols{ogplay::runtime::BionicHleSymbol{
            "liblog.so", "__android_log_write",
            ogplay::memory::GuestAddress{0x60000000}}};
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::runtime::BionicHleSymbolProvider(symbols)),
            ogplay::runtime::BionicProfileError);
    }
    SUBCASE("guest supplies a host boundary library") {
        const ogplay::runtime::BionicHleSymbolProvider hle(
            std::span<const ogplay::runtime::BionicHleSymbol>{});
        const std::vector modules{
            Module("app.so", 0x10000000, {"liblog.so"}, {}),
            Module("liblog.so", 0x18000000, {}, {})};
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::runtime::BuildBionicLinkNamespace(
                                profile, "app.so", modules, hle)),
                        ogplay::runtime::BionicProfileError);
    }
    SUBCASE("unregistered boundary symbol remains unresolved") {
        const ogplay::runtime::BionicHleSymbolProvider hle(
            std::span<const ogplay::runtime::BionicHleSymbol>{});
        const std::vector modules{Module(
            "app.so", 0x10000000, {"liblog.so"},
            {Symbol("__android_log_write", 0, 1, 0)})};
        const auto link_namespace = ogplay::runtime::BuildBionicLinkNamespace(
            profile, "app.so", modules, hle);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ResolveElf32Symbols(link_namespace, 0)),
                        ogplay::loader::LinkError);
    }
}

TEST_CASE("Bionic virtual SO publishes complete exports for late imports") {
    const auto& profile = ogplay::runtime::SelectBionicProfile(19);
    const std::vector hle_bindings{
        ogplay::runtime::BionicHleSymbol{
            "liblog.so", "__android_log_write",
            ogplay::memory::GuestAddress{0x70000201U}},
        ogplay::runtime::BionicHleSymbol{
            "liblog.so", "__android_log_print",
            ogplay::memory::GuestAddress{0x70000205U}},
    };
    const ogplay::runtime::BionicHleSymbolProvider hle(hle_bindings);
    const std::vector initial{Module(
        "app.so", 0x10000000U, {"liblog.so"},
        {Symbol("__android_log_write", 0, 1, 0)})};
    const auto base = ogplay::runtime::BuildBionicLinkNamespace(
        profile, "app.so", initial, hle);
    REQUIRE(base.modules.size() == 2);
    CHECK(base.modules[1].symbols.symbols.size() == 3);

    const std::vector plugin{Module(
        "plugin.so", 0x11000000U, {"liblog.so"},
        {Symbol("__android_log_print", 0, 1, 0)})};
    const auto extension = ogplay::runtime::ExtendBionicLinkNamespace(
        profile, base, "plugin.so", plugin, hle);
    CHECK(extension.link_namespace.modules.size() == 3);
    const auto resolved = ogplay::loader::ResolveElf32Symbols(
        extension.link_namespace, extension.scope, 2);
    REQUIRE(resolved.values[1].has_value());
    CHECK(*resolved.values[1] == ogplay::memory::GuestAddress{0x70000205U});
}

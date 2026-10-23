#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ogplay/runtime/integration/android_link_preflight.h"
#include "runtime/boundary/core/boundary_symbols.h"

namespace {

void Put16(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
}

void Put32(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
}

std::vector<std::byte> MinimalModule() {
    std::vector<std::byte> bytes(0x240, std::byte{});
    bytes[0] = std::byte{0x7f}; bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'}; bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1}; bytes[5] = std::byte{1}; bytes[6] = std::byte{1};
    Put16(bytes, 16, 3); Put16(bytes, 18, 40); Put32(bytes, 20, 1);
    Put32(bytes, 28, 52); Put32(bytes, 36, 0x05000400U);
    Put16(bytes, 40, 52); Put16(bytes, 42, 32); Put16(bytes, 44, 2);
    Put32(bytes, 52, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 60, 0x10000U); Put32(bytes, 68, 0x240);
    Put32(bytes, 72, 0x240); Put32(bytes, 76, 5); Put32(bytes, 80, 0x1000);
    Put32(bytes, 84, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 88, 0x100); Put32(bytes, 92, 0x10100U);
    Put32(bytes, 100, 56); Put32(bytes, 104, 56); Put32(bytes, 108, 6);
    Put32(bytes, 112, 4);
    Put32(bytes, 0x100, ogplay::loader::kElfDynamicStringTable);
    Put32(bytes, 0x104, 0x10160U);
    Put32(bytes, 0x108, ogplay::loader::kElfDynamicStringTableSize);
    Put32(bytes, 0x10c, 12);
    Put32(bytes, 0x110, ogplay::loader::kElfDynamicSoname);
    Put32(bytes, 0x114, 1);
    Put32(bytes, 0x118, ogplay::loader::kElfDynamicHash);
    Put32(bytes, 0x11c, 0x10180U);
    Put32(bytes, 0x120, ogplay::loader::kElfDynamicSymbolTable);
    Put32(bytes, 0x124, 0x101a0U);
    Put32(bytes, 0x128, ogplay::loader::kElfDynamicSymbolEntrySize);
    Put32(bytes, 0x12c, 16);
    const char strings[] = "\0sample.so\0";
    for (std::size_t index = 0; index < sizeof(strings); ++index) {
        bytes[0x160 + index] = static_cast<std::byte>(strings[index]);
    }
    Put32(bytes, 0x180, 1); Put32(bytes, 0x184, 1);
    Put32(bytes, 0x188, 0); Put32(bytes, 0x18c, 0);
    return bytes;
}

}  // namespace

TEST_CASE("Android link preflight maps and relocates without executing guest") {
    const auto bytes = MinimalModule();
    const ogplay::loader::Elf32ModuleInput module{
        "sample.so", bytes, ogplay::memory::GuestAddress{0x10000000U}};
    const auto report = ogplay::runtime::PreflightAndroidGuestLink(
        {19, "sample.so", std::span{&module, 1}});
    CHECK(report.guest_modules == 1);
    CHECK(report.boundary_modules == 0);
    CHECK(report.relocations == 0);
    CHECK_THROWS_AS(static_cast<void>(ogplay::runtime::PreflightAndroidGuestLink(
                        {19, "", {}})),
                    ogplay::runtime::AndroidLinkPreflightError);
}

namespace {
std::vector<std::byte> ImportModule(std::string name, std::vector<std::string> needed,
    std::vector<std::pair<std::string, std::uint8_t>> imports,
    std::vector<std::string> exports = {}) {
    auto bytes = MinimalModule();
    bytes.resize(0x1000);
    Put32(bytes, 68, 0x1000); Put32(bytes, 72, 0x1000);
    std::string strings(1, '\0');
    const auto text = [&](const std::string& value) {
        const auto offset = static_cast<std::uint32_t>(strings.size());
        strings += value; strings += '\0'; return offset;
    };
    std::size_t dynamic = 0x100;
    const auto tag = [&](std::int32_t kind, std::uint32_t value) {
        Put32(bytes, dynamic, static_cast<std::uint32_t>(kind));
        Put32(bytes, dynamic + 4, value); dynamic += 8;
    };
    tag(ogplay::loader::kElfDynamicStringTable, 0x10400);
    const auto string_size = dynamic + 4;
    tag(ogplay::loader::kElfDynamicStringTableSize, 0);
    tag(ogplay::loader::kElfDynamicSoname, text(name));
    tag(ogplay::loader::kElfDynamicHash, 0x10900);
    tag(ogplay::loader::kElfDynamicSymbolTable, 0x10600);
    tag(ogplay::loader::kElfDynamicSymbolEntrySize, 16);
    for (const auto& dependency : needed) tag(ogplay::loader::kElfDynamicNeeded, text(dependency));
    tag(ogplay::loader::kElfDynamicRel, 0x10b00);
    tag(ogplay::loader::kElfDynamicRelSize, static_cast<std::uint32_t>(imports.size() * 8));
    tag(0, 0);
    Put32(bytes, 100, static_cast<std::uint32_t>(dynamic - 0x100));
    Put32(bytes, 104, static_cast<std::uint32_t>(dynamic - 0x100));
    const auto count = imports.size() + exports.size() + 1;
    Put32(bytes, 0x900, 1); Put32(bytes, 0x904, static_cast<std::uint32_t>(count));
    std::size_t index = 1;
    for (const auto& [symbol, binding] : imports) {
        Put32(bytes, 0xb00 + (index - 1) * 8, 0x10e00 + static_cast<std::uint32_t>((index - 1) * 4));
        Put32(bytes, 0xb04 + (index - 1) * 8,
              (static_cast<std::uint32_t>(index) << 8U) | ogplay::loader::kArmRelocationJumpSlot);
        const auto base = 0x600 + index++ * 16;
        Put32(bytes, base, text(symbol)); bytes[base + 12] = static_cast<std::byte>(binding << 4U);
    }
    for (const auto& symbol : exports) {
        const auto base = 0x600 + index++ * 16;
        Put32(bytes, base, text(symbol)); Put32(bytes, base + 4, 0x10f00);
        bytes[base + 12] = std::byte{0x12}; Put16(bytes, base + 14, 1);
    }
    Put32(bytes, string_size, static_cast<std::uint32_t>(strings.size()));
    for (std::size_t i = 0; i < strings.size(); ++i) bytes[0x400 + i] = static_cast<std::byte>(strings[i]);
    return bytes;
}
ogplay::loader::ApkNativeLibrary AuditLibrary(std::string name, std::vector<std::byte> bytes) {
    return {"lib/armeabi-v7a/" + name, name, ogplay::loader::AndroidArmAbi::armeabi_v7a,
        "", std::move(bytes), name, std::nullopt};
}
}

TEST_CASE("APS-11 preflight lists all strong imports and separately reports unresolved weak symbols") {
    using namespace ogplay;
    const auto module = ImportModule("sample.so", {}, {{"missing_a", 1}, {"missing_b", 1}, {"optional", 2}});
    loader::ApkNativeLibraryInventory inventory({AuditLibrary("sample.so", module)});
    const auto report = runtime::AuditAndroidNativeImports(
        loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a), runtime::SelectBionicProfile(19), {});
    CHECK(report.strong_missing == 2); CHECK(report.weak_missing == 1);
    REQUIRE(report.gaps.size() == 3);
    CHECK(report.gaps[0].library == "sample.so"); CHECK(report.gaps[0].symbol == "missing_a");
    CHECK_FALSE(report.gaps[0].weak); CHECK(report.gaps[1].symbol == "missing_b"); CHECK(report.gaps[2].weak);
}

TEST_CASE("APS-11 preflight uses NEEDED scopes SONAME aliases system sources and boundary catalog") {
    using namespace ogplay;
    const auto root = ImportModule("sample.so", {"alias.so", "libandroid.so", "libm.so"},
        {{"provided", 1}, {"ASensorManager_getInstance", 1}, {"cos", 1}, {"optional", 2}});
    const auto dependency = ImportModule("alias.so", {}, {}, {"provided"});
    const auto system = ImportModule("libm.so", {}, {}, {"cos"});
    loader::ApkNativeLibraryInventory inventory({AuditLibrary("sample.so", root), AuditLibrary("other.so", dependency)});
    const runtime::BionicModuleSource source{"libm.so", system};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    const auto report = runtime::AuditAndroidNativeImports(selected, runtime::SelectBionicProfile(19), std::span{&source, 1});
    CHECK(report.strong_missing == 0); CHECK(report.weak_missing == 1);
    // An unrelated exported name must not leak into a root's NEEDED lookup scope.
    loader::ApkNativeLibraryInventory isolated({
        AuditLibrary("sample.so", ImportModule("sample.so", {}, {{"provided", 1}})),
        AuditLibrary("other.so", dependency)});
    const auto missing = runtime::AuditAndroidNativeImports(
        loader::SelectApkNativeLibraries(isolated, loader::AndroidArmAbi::armeabi_v7a), runtime::SelectBionicProfile(19), {});
    CHECK(missing.strong_missing == 1);
    CHECK(missing.gaps[0].symbol == "provided");
    // Same symbol rules as actual loading; a missing dependency remains a hard error.
    CHECK_THROWS(static_cast<void>(runtime::AuditAndroidNativeImports(selected, runtime::SelectBionicProfile(19), {})));
}

TEST_CASE("DVM271 API19 GLES imports resolve from a guest ELF and removing either export fails") {
    using namespace ogplay;
    const auto bytes =
        ImportModule("surface.so", {"libGLESv1_CM.so"}, {{"glDrawTexfOES", 1}, {"glBlendEquationOES", 1}});
    const loader::Elf32ModuleInput input{"surface.so", bytes, memory::GuestAddress{0x10000000U}};
    const auto report = runtime::PreflightAndroidGuestLink({19, "surface.so", std::span{&input, 1}});
    CHECK(report.guest_modules == 1U);
    CHECK(report.boundary_modules == 1U);
    const auto image = loader::ParseElf32Arm(bytes);
    loader::Elf32LinkModule module;
    module.name = "surface.so";
    module.load_bias = input.load_bias;
    module.dynamic = loader::ReadElf32DynamicInfo(bytes, image);
    module.symbols = loader::ReadElf32SymbolTable(bytes, image);
    for (const auto removed : {"glDrawTexfOES", "glBlendEquationOES"}) {
        CAPTURE(removed);
        auto symbols = runtime::detail::BuildAndroidBoundarySymbols(runtime::AndroidApi::api19);
        std::erase_if(symbols, [removed](const auto& symbol) {
            return symbol.library == "libGLESv1_CM.so" && symbol.symbol == removed;
        });
        const runtime::BionicHleSymbolProvider provider(symbols);
        const auto ns = runtime::BuildBionicLinkNamespace(runtime::SelectBionicProfile(19), "surface.so",
                                                          std::span{&module, 1}, provider);
        CHECK_THROWS_WITH_AS(static_cast<void>(loader::ResolveElf32Symbols(ns, 0U)),
                             (std::string("strong ELF symbol is unresolved: ") + removed).c_str(), loader::LinkError);
    }
}

TEST_CASE("WU11-6 preflight retains unused import diagnostics without rejecting the ELF") {
    using namespace ogplay;
    auto bytes = ImportModule("sample.so", {}, {{"unused_strong", 1}, {"unused_weak", 2}});
    for (std::size_t pos = 0x100; pos < 0x200; pos += 8) {
        if (bytes[pos] == static_cast<std::byte>(loader::kElfDynamicRelSize)) Put32(bytes, pos + 4, 0);
    }
    loader::ApkNativeLibraryInventory inventory({AuditLibrary("sample.so", bytes)});
    const auto report = runtime::AuditAndroidNativeImports(
        loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a),
        runtime::SelectBionicProfile(19), {});
    CHECK(report.strong_missing == 0);
    CHECK(report.weak_missing == 0);
    CHECK(report.unused_strong_missing == 1);
    CHECK(report.unused_weak_missing == 1);
    REQUIRE(report.gaps.size() == 2);
    CHECK_FALSE(report.gaps[0].relocation_referenced);
    const loader::Elf32ModuleInput input{"sample.so", bytes, memory::GuestAddress{0x10000000}};
    CHECK_NOTHROW(static_cast<void>(runtime::PreflightAndroidGuestLink({19, "sample.so", std::span{&input, 1}})));
    // An actual strong reference must still fail.
    Put32(bytes, 0x138 + 4, 8);  // DT_RELSZ, after the six metadata tags and DT_REL.
    CHECK_THROWS_AS(static_cast<void>(runtime::PreflightAndroidGuestLink({19, "sample.so", std::span{&input, 1}})),
                    runtime::AndroidLinkPreflightError);
}

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni_guest/jni_guest_library_lifecycle.h"
#include "ogplay/runtime/jni/jni.h"

namespace {

ogplay::loader::Elf32Symbol Symbol(const char* name,
                                   const std::uint32_t value,
                                   const std::uint8_t type = 2U,
                                   const std::uint16_t section = 7U) {
    return {name, ogplay::memory::GuestAddress{value}, 16U, 1U, type, 0U,
            section};
}

ogplay::loader::Elf32LinkModule Module(
    std::string name, const std::uint32_t bias,
    std::optional<std::string> soname,
    std::vector<ogplay::loader::Elf32Symbol> symbols) {
    ogplay::loader::Elf32LinkModule module;
    module.name = std::move(name);
    module.load_bias = ogplay::memory::GuestAddress{bias};
    module.dynamic.soname = std::move(soname);
    module.symbols.symbols = std::move(symbols);
    return module;
}

}  // namespace

TEST_CASE("guest JNI library lifecycle selects only the root JNI_OnLoad") {
    using namespace ogplay;
    loader::Elf32LinkNamespace link_namespace;
    link_namespace.modules = {
        Module("libdependency.so", 0x20000000U, std::nullopt,
               {Symbol("", 0U, 0U, 0U),
                Symbol("JNI_OnLoad", 0x101U)}),
        Module("libroot-file.so", 0x10000000U, "libroot.so",
               {Symbol("", 0U, 0U, 0U),
                Symbol("JNI_OnLoad", 0x220U)}),
    };
    const auto planned = runtime::BuildJniGuestLibraryOnLoad(
        link_namespace, "libroot.so",
        memory::GuestAddress{0x71200424U});
    REQUIRE(planned.has_value());
    CHECK(planned->module_index == 1U);
    CHECK(planned->module_name == "libroot-file.so");
    CHECK(planned->call.target == memory::GuestAddress{0x10000220U});
    CHECK(planned->call.registers ==
          std::array<std::uint32_t, 4>{0x71200424U, 0U, 0U, 0U});
    CHECK(planned->call.stack_words.empty());
}

TEST_CASE("guest JNI library lifecycle preserves absence and rejects bad ABI") {
    using namespace ogplay;
    loader::Elf32LinkNamespace link_namespace;
    link_namespace.modules = {
        Module("libroot.so", 0x10000000U, std::nullopt,
               {Symbol("", 0U, 0U, 0U)}),
    };
    CHECK_FALSE(runtime::BuildJniGuestLibraryOnLoad(
                    link_namespace, "libroot.so",
                    memory::GuestAddress{0x71200424U})
                    .has_value());
    CHECK_NOTHROW(runtime::ValidateJniGuestLibraryOnLoadResult(0x00010001U));
    CHECK_NOTHROW(runtime::ValidateJniGuestLibraryOnLoadResult(0x00010002U));
    CHECK_NOTHROW(runtime::ValidateJniGuestLibraryOnLoadResult(0x00010004U));
    CHECK_NOTHROW(runtime::ValidateJniGuestLibraryOnLoadResult(
        static_cast<std::uint32_t>(runtime::kJniVersion1_6)));
    CHECK_THROWS_WITH_AS(
        runtime::ValidateJniGuestLibraryOnLoadResult(0xffffffffU),
        "guest JNI_OnLoad returned an unsupported JNI version",
        std::runtime_error);

    CHECK_THROWS_WITH_AS(
        static_cast<void>(runtime::BuildJniGuestLibraryOnLoad(
            link_namespace, "libmissing.so",
            memory::GuestAddress{0x71200424U})),
        "guest JNI root module is absent from link namespace",
        std::runtime_error);

    link_namespace.modules.push_back(
        Module("libroot-alias.so", 0x20000000U, "libroot.so",
               {Symbol("", 0U, 0U, 0U)}));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(runtime::BuildJniGuestLibraryOnLoad(
            link_namespace, "libroot.so",
            memory::GuestAddress{0x71200424U})),
        "guest JNI root module identity is ambiguous", std::runtime_error);

    link_namespace.modules = {
        Module("libroot.so", 0x10000000U, std::nullopt,
               {Symbol("", 0U, 0U, 0U),
                Symbol("JNI_OnLoad", 0x220U, 1U)}),
    };
    CHECK_THROWS_WITH_AS(
        static_cast<void>(runtime::BuildJniGuestLibraryOnLoad(
            link_namespace, "libroot.so",
            memory::GuestAddress{0x71200424U})),
        "root JNI_OnLoad export is not a function", std::runtime_error);
}

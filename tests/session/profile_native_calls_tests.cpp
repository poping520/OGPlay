#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ogplay/session/profile_native_calls.h"

namespace {

ogplay::loader::Elf32Symbol Export(const std::string& name,
                                   const std::uint32_t value) {
    return {name, ogplay::memory::GuestAddress{value}, 4, 1, 2, 0, 1};
}

ogplay::session::ProfileNativeCall Call(const std::string& method,
                                        const std::string& signature) {
    return {ogplay::session::ProfileNativeCallPhase::startup,
            "org/example/Renderer", method, signature,
            ogplay::session::ProfileNativeDispatch::static_method, {}};
}

}  // namespace

TEST_CASE("Profile native calls resolve short names before long names in order") {
    const std::vector calls{Call("nativeInit", "()V"),
                            Call("nativeResize", "(II)V")};
    ogplay::loader::Elf32SymbolTable symbols;
    symbols.symbols.emplace_back();
    symbols.symbols.push_back(Export(
        "Java_org_example_Renderer_nativeInit", 0x101U));
    symbols.symbols.push_back(Export(
        "Java_org_example_Renderer_nativeResize__II", 0x201U));

    const auto targets = ogplay::session::ResolveProfileNativeCalls(
        calls, symbols, ogplay::memory::GuestAddress{0x10000000U});
    REQUIRE(targets.size() == 2);
    CHECK(targets[0].call_index == 0);
    CHECK(targets[0].export_name ==
          "Java_org_example_Renderer_nativeInit");
    CHECK(targets[0].address == ogplay::memory::GuestAddress{0x10000101U});
    CHECK(targets[1].export_name ==
          "Java_org_example_Renderer_nativeResize__II");
    CHECK(targets[1].address == ogplay::memory::GuestAddress{0x10000201U});
}

TEST_CASE("Profile native calls reject missing duplicate and overflowing exports") {
    const std::vector calls{Call("nativeInit", "()V")};
    ogplay::loader::Elf32SymbolTable missing;
    missing.symbols.emplace_back();
    CHECK_THROWS_WITH(static_cast<void>(ogplay::session::ResolveProfileNativeCalls(
                          calls, missing, ogplay::memory::GuestAddress{})),
                      "profiled JNI call has no root-library export: "
                      "Java_org_example_Renderer_nativeInit or "
                      "Java_org_example_Renderer_nativeInit__");

    auto duplicate = missing;
    duplicate.symbols.push_back(Export(
        "Java_org_example_Renderer_nativeInit", 0x101U));
    duplicate.symbols.push_back(Export(
        "Java_org_example_Renderer_nativeInit", 0x201U));
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::ResolveProfileNativeCalls(
                        calls, duplicate, ogplay::memory::GuestAddress{})),
                    ogplay::session::ProfileNativeCallError);

    auto overflow = missing;
    overflow.symbols.push_back(Export(
        "Java_org_example_Renderer_nativeInit", 0x100U));
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::ResolveProfileNativeCalls(
                        calls, overflow,
                        ogplay::memory::GuestAddress{0xFFFFFF80U})),
                    ogplay::session::ProfileNativeCallError);
}

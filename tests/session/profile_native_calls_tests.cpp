#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <optional>
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

TEST_CASE("Profile native invocations marshal JNI registers and aligned stack words") {
    using namespace ogplay::session;
    ProfileNativeCall call{
        ProfileNativeCallPhase::startup,
        "org/example/Renderer",
        "nativeInit",
        "(IIIII)V",
        ProfileNativeDispatch::instance,
        {{ProfileNativeArgumentSource::constant, 0U},
         {ProfileNativeArgumentSource::constant, 1U},
         {ProfileNativeArgumentSource::surface_width, 0U},
         {ProfileNativeArgumentSource::surface_height, 0U},
         {ProfileNativeArgumentSource::constant, 9U}}};
    const std::array calls{call};
    const std::array targets{ProfileNativeCallTarget{
        0U, "Java_org_example_Renderer_nativeInit",
        ogplay::memory::GuestAddress{0x10000101U}}};
    const std::array references{ProfileNativeClassReference{
        "org/example/Renderer", ogplay::runtime::JniReference{0x101U},
        ogplay::runtime::JniReference{0x202U}}};
    const auto invocations = BuildProfileNativeInvocations(
        calls, targets, ProfileNativeCallPhase::startup, references,
        {ogplay::memory::GuestAddress{0x71200420U}, {800U, 480U},
         std::nullopt});
    REQUIRE(invocations.size() == 1);
    CHECK(invocations[0].call_index == 0U);
    CHECK(invocations[0].export_name ==
          "Java_org_example_Renderer_nativeInit");
    CHECK(invocations[0].address ==
          ogplay::memory::GuestAddress{0x10000101U});
    CHECK(invocations[0].registers ==
          std::array<std::uint32_t, 4>{
              0x71200420U, 0x101U, 0U, 1U});
    CHECK(invocations[0].stack_words ==
          std::vector<std::uint32_t>{800U, 480U, 9U, 0U});
}

TEST_CASE("Profile native invocations select phase static class and input facts") {
    using namespace ogplay::session;
    const std::array calls{ProfileNativeCall{
        ProfileNativeCallPhase::pointer_down,
        "org/example/Input",
        "nativeTouch",
        "(III)V",
        ProfileNativeDispatch::static_method,
        {{ProfileNativeArgumentSource::input_x, 0U},
         {ProfileNativeArgumentSource::input_y, 0U},
         {ProfileNativeArgumentSource::input_pointer, 0U}}}};
    const std::array targets{ProfileNativeCallTarget{
        0U, "Java_org_example_Input_nativeTouch",
        ogplay::memory::GuestAddress{0x20000100U}}};
    const std::array references{ProfileNativeClassReference{
        "org/example/Input", ogplay::runtime::JniReference{0x303U},
        ogplay::runtime::JniReference{0x404U}}};
    const ProfileNativeInvocationContext context{
        ogplay::memory::GuestAddress{0x71200420U}, {640U, 360U},
        ProfileNativeInputArguments{12U, 34U, 2U, 0U}};
    CHECK(BuildProfileNativeInvocations(
              calls, targets, ProfileNativeCallPhase::frame, references,
              context)
              .empty());
    const auto invocations = BuildProfileNativeInvocations(
        calls, targets, ProfileNativeCallPhase::pointer_down, references,
        context);
    REQUIRE(invocations.size() == 1);
    CHECK(invocations[0].registers ==
          std::array<std::uint32_t, 4>{
              0x71200420U, 0x404U, 12U, 34U});
    CHECK(invocations[0].stack_words ==
          std::vector<std::uint32_t>{2U, 0U});
}

TEST_CASE("Profile native invocation planning rejects partial runtime state") {
    using namespace ogplay::session;
    const std::array calls{ProfileNativeCall{
        ProfileNativeCallPhase::key_down,
        "org/example/Input",
        "nativeKey",
        "(I)V",
        ProfileNativeDispatch::instance,
        {{ProfileNativeArgumentSource::input_key, 0U}}}};
    const std::array targets{ProfileNativeCallTarget{
        0U, "Java_org_example_Input_nativeKey",
        ogplay::memory::GuestAddress{0x20000101U}}};
    const std::array references{ProfileNativeClassReference{
        "org/example/Input", ogplay::runtime::JniReference{0x303U},
        ogplay::runtime::JniReference{0x404U}}};
    const ProfileNativeInvocationContext missing_input{
        ogplay::memory::GuestAddress{0x71200420U}, {640U, 360U},
        std::nullopt};
    CHECK_THROWS_AS(
        static_cast<void>(BuildProfileNativeInvocations(
            calls, targets, ProfileNativeCallPhase::key_down, references,
            missing_input)),
        ProfileNativeCallError);

    auto bad_targets = targets;
    bad_targets[0].call_index = 1U;
    CHECK_THROWS_AS(
        static_cast<void>(BuildProfileNativeInvocations(
            calls, bad_targets, ProfileNativeCallPhase::key_down, references,
            {ogplay::memory::GuestAddress{0x71200420U}, {640U, 360U},
             ProfileNativeInputArguments{}})),
        ProfileNativeCallError);

    const std::array missing_reference{ProfileNativeClassReference{
        "org/example/Other", ogplay::runtime::JniReference{0x303U},
        ogplay::runtime::JniReference{0x404U}}};
    CHECK_THROWS_AS(
        static_cast<void>(BuildProfileNativeInvocations(
            calls, targets, ProfileNativeCallPhase::key_down,
            missing_reference,
            {ogplay::memory::GuestAddress{0x71200420U}, {640U, 360U},
             ProfileNativeInputArguments{}})),
        ProfileNativeCallError);

    auto null_receiver = references;
    null_receiver[0].instance = ogplay::runtime::JniReference{};
    CHECK_THROWS_AS(
        static_cast<void>(BuildProfileNativeInvocations(
            calls, targets, ProfileNativeCallPhase::key_down, null_receiver,
            {ogplay::memory::GuestAddress{0x71200420U}, {640U, 360U},
             ProfileNativeInputArguments{}})),
        ProfileNativeCallError);

    CHECK_THROWS_AS(
        static_cast<void>(BuildProfileNativeInvocations(
            calls, targets, ProfileNativeCallPhase::key_down, references,
            {ogplay::memory::GuestAddress{}, {640U, 360U},
             ProfileNativeInputArguments{}})),
        ProfileNativeCallError);
    CHECK_THROWS_AS(
        static_cast<void>(BuildProfileNativeInvocations(
            calls, targets, ProfileNativeCallPhase::key_down, references,
            {ogplay::memory::GuestAddress{0x71200420U}, {0U, 360U},
             ProfileNativeInputArguments{}})),
        ProfileNativeCallError);

    auto unsupported_signature = calls;
    unsupported_signature[0].signature = "(J)V";
    CHECK_THROWS_AS(
        static_cast<void>(BuildProfileNativeInvocations(
            unsupported_signature, targets, ProfileNativeCallPhase::key_down,
            references,
            {ogplay::memory::GuestAddress{0x71200420U}, {640U, 360U},
             ProfileNativeInputArguments{}})),
        ProfileNativeCallError);
}

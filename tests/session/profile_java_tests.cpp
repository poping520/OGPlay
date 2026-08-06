#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/session/profile_java.h"

namespace {

[[nodiscard]] ogplay::session::TitleProfile JavaProfile() {
    ogplay::session::TitleProfile profile;
    profile.java_classes = {
        {"fixture/ProfileBridge",
         {{"increment", "(I)I", "math.increment"},
          {"reset", "()V", "state.reset"}}},
    };
    return profile;
}

[[nodiscard]] std::vector<ogplay::session::ProfileJavaImplementation>
Implementations() {
    return {
        {"math.increment",
         [](const ogplay::runtime::JniInvocation& invocation) {
             return ogplay::runtime::JniValue{
                 static_cast<ogplay::runtime::JniInt>(
                     std::get<ogplay::runtime::JniInt>(
                         invocation.arguments.front()) +
                     1)};
         }},
        {"state.reset", [](const ogplay::runtime::JniInvocation&) {
             return ogplay::runtime::JniValue{std::monostate{}};
         }},
        {"unused.generic", [](const ogplay::runtime::JniInvocation&) {
             return ogplay::runtime::JniValue{std::monostate{}};
         }},
    };
}

}  // namespace

TEST_CASE("Profile Java declarations register exact JNI classes and handlers") {
    auto assembly =
        ogplay::session::AssembleProfileJava(JavaProfile(), Implementations());
    REQUIRE(assembly.classes != nullptr);
    REQUIRE(assembly.invocations != nullptr);
    REQUIRE(assembly.bindings.size() == 2);
    CHECK(assembly.bindings[0].class_name == "fixture/ProfileBridge");
    CHECK(assembly.bindings[0].implementation == "math.increment");

    const std::vector<ogplay::runtime::JniValue> arguments{
        static_cast<ogplay::runtime::JniInt>(41)};
    const auto result = assembly.invocations->InvokeVirtual(
        1, ogplay::runtime::JniReference{7},
        assembly.bindings[0].class_identity,
        assembly.bindings[0].method_id, arguments,
        ogplay::runtime::JniArgumentSource::value_array);
    CHECK(std::get<ogplay::runtime::JniInt>(result) == 42);
    CHECK(std::holds_alternative<std::monostate>(
        assembly.invocations->InvokeVirtual(
            1, ogplay::runtime::JniReference{7},
            assembly.bindings[1].class_identity,
            assembly.bindings[1].method_id, {},
            ogplay::runtime::JniArgumentSource::value_array)));
    CHECK_NOTHROW(assembly.invocations->RegisterHandler(
        "unused.generic", [](const ogplay::runtime::JniInvocation&) {
            return ogplay::runtime::JniValue{std::monostate{}};
        }));
}

TEST_CASE("Profile Java assembly rejects missing and duplicate implementations") {
    auto implementations = Implementations();
    implementations.erase(implementations.begin());
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::session::AssembleProfileJava(
            JavaProfile(), implementations)),
        "Profile Java method has no registered implementation: math.increment",
        ogplay::session::ProfileJavaError);

    implementations = Implementations();
    implementations.push_back(implementations.front());
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::AssembleProfileJava(
            JavaProfile(), implementations)),
        ogplay::session::ProfileJavaError);

    implementations = Implementations();
    implementations.front().handler = {};
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::AssembleProfileJava(
            JavaProfile(), implementations)),
        ogplay::session::ProfileJavaError);
}

TEST_CASE("Profile Java assembly rejects invalid declarations transactionally") {
    auto profile = JavaProfile();
    profile.java_classes.push_back(profile.java_classes.front());
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::AssembleProfileJava(
            profile, Implementations())),
        ogplay::session::ProfileJavaError);

    profile = JavaProfile();
    profile.java_classes.front().methods.front().signature = "(V)V";
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::AssembleProfileJava(
            profile, Implementations())),
        ogplay::session::ProfileJavaError);

    profile = {};
    auto empty =
        ogplay::session::AssembleProfileJava(profile, Implementations());
    CHECK(empty.bindings.empty());
    REQUIRE(empty.classes != nullptr);
    REQUIRE(empty.invocations != nullptr);
}

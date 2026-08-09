#include <array>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni/jni_invocation.h"

namespace {

struct InvocationFixture final {
    InvocationFixture() {
        base = classes.RegisterClass(
            {"test/Base", {},
             {{"value", "(I)I", "base.value", false},
              {"reset", "()V", "base.reset", false},
              {"version", "()I", "base.version", true}}, {}});
        derived = classes.RegisterClass(
            {"test/Derived", "test/Base",
             {{"value", "(I)I", "derived.value", false}}, {}});
        value = *classes.GetMethodId(base, "value", "(I)I", false);
        reset = *classes.GetMethodId(base, "reset", "()V", false);
        version = *classes.GetMethodId(base, "version", "()I", true);
        engine.RegisterHandler("base.value", [](const auto& invocation) {
            return ogplay::runtime::JniValue{
                static_cast<ogplay::runtime::JniInt>(
                    std::get<ogplay::runtime::JniInt>(
                        invocation.arguments[0]) + 1)};
        });
        engine.RegisterHandler("derived.value", [](const auto& invocation) {
            return ogplay::runtime::JniValue{
                static_cast<ogplay::runtime::JniInt>(
                    std::get<ogplay::runtime::JniInt>(
                        invocation.arguments[0]) + 2)};
        });
        engine.RegisterHandler("base.reset", [](const auto&) {
            return ogplay::runtime::JniValue{std::monostate{}};
        });
        engine.RegisterHandler("base.version", [](const auto&) {
            return ogplay::runtime::JniValue{
                static_cast<ogplay::runtime::JniInt>(19)};
        });
    }

    ogplay::runtime::JniClassRegistry classes;
    ogplay::runtime::JniInvocationEngine engine{classes};
    ogplay::runtime::JniObjectIdentity base;
    ogplay::runtime::JniObjectIdentity derived;
    ogplay::runtime::JniMethodId value;
    ogplay::runtime::JniMethodId reset;
    ogplay::runtime::JniMethodId version;
    ogplay::runtime::JniReference receiver{7};
};

}  // namespace

TEST_CASE("JNI virtual nonvirtual and static calls select exact handlers") {
    InvocationFixture fixture;
    const std::vector<ogplay::runtime::JniValue> arguments{
        static_cast<ogplay::runtime::JniInt>(40)};
    CHECK(std::get<ogplay::runtime::JniInt>(fixture.engine.InvokeVirtual(
              1, fixture.receiver, fixture.derived, fixture.value, arguments,
              ogplay::runtime::JniArgumentSource::value_array)) == 42);
    CHECK(std::get<ogplay::runtime::JniInt>(fixture.engine.InvokeNonvirtual(
              1, fixture.receiver, fixture.derived, fixture.base,
              fixture.value, arguments,
              ogplay::runtime::JniArgumentSource::va_list)) == 41);
    CHECK(std::get<ogplay::runtime::JniInt>(fixture.engine.InvokeStatic(
              1, fixture.derived, fixture.version, {},
              ogplay::runtime::JniArgumentSource::variadic)) == 19);
    CHECK(std::holds_alternative<std::monostate>(
        fixture.engine.InvokeVirtual(
            1, fixture.receiver, fixture.base, fixture.reset, {},
            ogplay::runtime::JniArgumentSource::value_array)));
}

TEST_CASE("JNI invocation validates argument and method kinds") {
    InvocationFixture fixture;
    const std::vector<ogplay::runtime::JniValue> wrong{
        static_cast<ogplay::runtime::JniLong>(40)};
    CHECK_THROWS_AS(
        static_cast<void>(fixture.engine.InvokeVirtual(
            1, fixture.receiver, fixture.base, fixture.value, wrong,
            ogplay::runtime::JniArgumentSource::value_array)),
        ogplay::runtime::JniInvocationError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.engine.InvokeVirtual(
            1, ogplay::runtime::JniReference{}, fixture.base, fixture.value, {},
            ogplay::runtime::JniArgumentSource::value_array)),
        ogplay::runtime::JniInvocationError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.engine.InvokeStatic(
            1, fixture.base, fixture.value, {},
            ogplay::runtime::JniArgumentSource::value_array)),
        ogplay::runtime::JniInvocationError);
}

TEST_CASE("JNI invocation rejects missing and wrong return handlers") {
    ogplay::runtime::JniClassRegistry classes;
    const auto type = classes.RegisterClass(
        {"test/Type", {},
         {{"missing", "()I", "missing.impl", true},
          {"wrong", "()I", "wrong.impl", true}}, {}});
    const auto missing = *classes.GetMethodId(type, "missing", "()I", true);
    const auto wrong = *classes.GetMethodId(type, "wrong", "()I", true);
    ogplay::runtime::JniInvocationEngine engine(classes);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(engine.InvokeStatic(
            2, type, missing, {},
            ogplay::runtime::JniArgumentSource::value_array)),
        "JNI method implementation has no registered handler: missing.impl",
        ogplay::runtime::JniInvocationError);
    engine.RegisterHandler("wrong.impl", [](const auto&) {
        return ogplay::runtime::JniValue{std::monostate{}};
    });
    CHECK_THROWS_AS(
        static_cast<void>(engine.InvokeStatic(
            2, type, wrong, {},
            ogplay::runtime::JniArgumentSource::value_array)),
        ogplay::runtime::JniInvocationError);
}

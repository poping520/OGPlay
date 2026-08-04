#include <doctest/doctest.h>

#include "ogplay/runtime/jni_class_registry.h"

TEST_CASE("JNI class registry resolves hierarchy and assignability") {
    ogplay::runtime::JniClassRegistry classes;
    const auto object = classes.RegisterClass({"java/lang/Object", {}, {}, {}});
    const auto activity = classes.RegisterClass(
        {"android/app/Activity", "java/lang/Object", {}, {}});
    const auto derived = classes.RegisterClass(
        {"test/Derived", "android/app/Activity", {}, {}});
    CHECK(classes.FindClass("java/lang/Object") == object);
    CHECK(classes.GetSuperclass(object) == std::nullopt);
    CHECK(classes.GetSuperclass(activity) == object);
    CHECK(classes.IsAssignableFrom(object, derived));
    CHECK(classes.IsAssignableFrom(activity, derived));
    CHECK_FALSE(classes.IsAssignableFrom(derived, activity));
    CHECK_FALSE(classes.FindClass("missing/Class").has_value());
}

TEST_CASE("JNI member lookup distinguishes static overloads and inheritance") {
    ogplay::runtime::JniClassRegistry classes;
    const auto base = classes.RegisterClass(
        {"test/Base",
         {},
         {{"run", "(I)I", "base.run.int", false},
          {"run", "(J)J", "base.run.long", false},
          {"create", "()Ltest/Base;", "base.create", true},
          {"<init>", "()V", "base.ctor", false}},
         {{"value", "I", "base.value", false},
          {"count", "I", "base.count", true}}});
    const auto derived =
        classes.RegisterClass({"test/Derived", "test/Base", {}, {}});
    const auto integer = classes.GetMethodId(derived, "run", "(I)I", false);
    const auto wide = classes.GetMethodId(derived, "run", "(J)J", false);
    REQUIRE(integer.has_value());
    REQUIRE(wide.has_value());
    CHECK(*integer != *wide);
    CHECK(classes.ResolveMethod(*integer).declaration.implementation ==
          "base.run.int");
    CHECK(classes.GetMethodId(base, "create", "()Ltest/Base;", true)
              .has_value());
    CHECK_FALSE(classes.GetMethodId(base, "create", "()Ltest/Base;", false)
                    .has_value());
    CHECK_FALSE(classes.GetMethodId(derived, "<init>", "()V", false)
                    .has_value());
    CHECK(classes.GetFieldId(derived, "value", "I", false).has_value());
    CHECK(classes.GetFieldId(derived, "count", "I", true).has_value());
}

TEST_CASE("JNI class registration validates transactionally") {
    ogplay::runtime::JniClassRegistry classes;
    CHECK_THROWS_AS(
        static_cast<void>(classes.RegisterClass(
            {"broken.Name", {}, {}, {}})),
        ogplay::runtime::JniClassRegistryError);
    CHECK_THROWS_AS(
        static_cast<void>(classes.RegisterClass(
            {"test/Child", "test/Missing", {}, {}})),
        ogplay::runtime::JniClassRegistryError);
    CHECK_FALSE(classes.FindClass("test/Child").has_value());
    CHECK_THROWS_AS(
        static_cast<void>(classes.RegisterClass(
            {"test/Duplicate", {},
             {{"run", "()V", "one", false},
              {"run", "()V", "two", true}}, {}})),
        ogplay::runtime::JniClassRegistryError);
    CHECK_FALSE(classes.FindClass("test/Duplicate").has_value());
    CHECK_THROWS_AS(
        static_cast<void>(classes.RegisterClass(
            {"test/BadSignature", {},
             {{"run", "(V)V", "bad", false}}, {}})),
        ogplay::runtime::JniClassRegistryError);
    CHECK_FALSE(classes.FindClass("test/BadSignature").has_value());
}

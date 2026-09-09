#include <doctest/doctest.h>

#include "ogplay/runtime/jni/jni_class_registry.h"

TEST_CASE("JNI class registry resolves hierarchy and assignability") {
    ogplay::runtime::JniClassRegistry classes;
    const auto object = classes.RegisterClass({"java/lang/Object", {}, {}, {}});
    const auto activity = classes.RegisterClass(
        {"android/app/Activity", "java/lang/Object", {}, {}});
  const auto derived =
      classes.RegisterClass({"test/Derived", "android/app/Activity", {}, {}});
    CHECK(classes.FindClass("java/lang/Object") == object);
    CHECK(classes.ClassName(object) == "java/lang/Object");
    CHECK(classes.ClassName(derived) == "test/Derived");
    CHECK(classes.GetSuperclass(object) == std::nullopt);
    CHECK(classes.GetSuperclass(activity) == object);
    CHECK(classes.IsAssignableFrom(object, derived));
    CHECK(classes.IsAssignableFrom(activity, derived));
    CHECK_FALSE(classes.IsAssignableFrom(derived, activity));
    CHECK_FALSE(classes.FindClass("missing/Class").has_value());
}

TEST_CASE("JNI member lookup distinguishes static overloads and inheritance") {
    ogplay::runtime::JniClassRegistry classes;
  const auto base =
      classes.RegisterClass({"test/Base",
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
  CHECK(classes.GetMethodId(base, "create", "()Ltest/Base;", true).has_value());
  CHECK_FALSE(
      classes.GetMethodId(base, "create", "()Ltest/Base;", false).has_value());
  CHECK_FALSE(classes.GetMethodId(derived, "<init>", "()V", false).has_value());
    CHECK(classes.GetFieldId(derived, "value", "I", false).has_value());
    CHECK(classes.GetFieldId(derived, "count", "I", true).has_value());
}

TEST_CASE("JNI class registry follows interface graphs") {
    using ogplay::runtime::JniClassRegistry;
    JniClassRegistry classes;
    const auto object =
        classes.RegisterClass({"java/lang/Object", {}, {}, {}});
    const auto root = classes.RegisterClass(
        {"test/RootContract",
         {},
         {{"run", "()I", "root.run", false}},
         {{"VERSION", "I", "root.version", true}},
         {},
         true});
    const auto left = classes.RegisterClass(
        {"test/LeftContract", {}, {}, {}, {"test/RootContract"}, true});
    const auto right = classes.RegisterClass(
        {"test/RightContract", {}, {}, {}, {"test/RootContract"}, true});
    const auto diamond = classes.RegisterClass(
        {"test/DiamondContract",
         {},
         {},
         {},
         {"test/LeftContract", "test/RightContract"},
         true});
    const auto implementation = classes.RegisterClass(
        {"test/Implementation",
         "java/lang/Object",
         {{"run", "()I", "implementation.run", false}},
         {},
         {"test/DiamondContract"}});
    const auto derived = classes.RegisterClass(
        {"test/Derived", "test/Implementation", {}, {}});

    CHECK(classes.GetInterfaces(object).empty());
    CHECK(classes.GetInterfaces(diamond) ==
          std::vector{left, right});
    CHECK(classes.GetInterfaces(implementation) == std::vector{diamond});
    CHECK(classes.IsAssignableFrom(root, diamond));
    CHECK(classes.IsAssignableFrom(left, implementation));
    CHECK(classes.IsAssignableFrom(right, derived));
    CHECK(classes.IsAssignableFrom(root, derived));
    CHECK_FALSE(classes.IsAssignableFrom(diamond, root));
    CHECK(classes.GetMethodId(diamond, "run", "()I", false) ==
          classes.GetMethodId(root, "run", "()I", false));
    CHECK(classes.GetMethodId(derived, "run", "()I", false) ==
          classes.GetMethodId(implementation, "run", "()I", false));
    CHECK(classes.GetFieldId(derived, "VERSION", "I", true) ==
          classes.GetFieldId(root, "VERSION", "I", true));
    CHECK_FALSE(classes.GetFieldId(derived, "VERSION", "I", false));
}

TEST_CASE("JNI class registration validates transactionally") {
    ogplay::runtime::JniClassRegistry classes;
    CHECK_THROWS_AS(
      static_cast<void>(classes.RegisterClass({"broken.Name", {}, {}, {}})),
        ogplay::runtime::JniClassRegistryError);
  CHECK_THROWS_AS(static_cast<void>(classes.RegisterClass(
            {"test/Child", "test/Missing", {}, {}})),
        ogplay::runtime::JniClassRegistryError);
    CHECK_FALSE(classes.FindClass("test/Child").has_value());
    CHECK_THROWS_AS(
        static_cast<void>(classes.RegisterClass(
          {"test/Duplicate",
           {},
           {{"run", "()V", "one", false}, {"run", "()V", "two", true}},
           {}})),
        ogplay::runtime::JniClassRegistryError);
    CHECK_FALSE(classes.FindClass("test/Duplicate").has_value());
    CHECK_THROWS_AS(
        static_cast<void>(classes.RegisterClass(
          {"test/BadSignature", {}, {{"run", "(V)V", "bad", false}}, {}})),
        ogplay::runtime::JniClassRegistryError);
    CHECK_FALSE(classes.FindClass("test/BadSignature").has_value());
    const auto contract = classes.RegisterClass(
        {"test/Contract", {}, {}, {}, {}, true});
    static_cast<void>(contract);
    CHECK_THROWS_AS(static_cast<void>(classes.RegisterClass(
                        {"test/MissingInterface",
                         {},
                         {},
                         {},
                         {"test/Absent"}})),
                    ogplay::runtime::JniClassRegistryError);
    CHECK_FALSE(classes.FindClass("test/MissingInterface").has_value());
    CHECK_THROWS_AS(static_cast<void>(classes.RegisterClass(
                        {"test/DuplicateInterface",
                         {},
                         {},
                         {},
                         {"test/Contract", "test/Contract"}})),
                    ogplay::runtime::JniClassRegistryError);
    CHECK_FALSE(classes.FindClass("test/DuplicateInterface").has_value());
}

TEST_CASE(
    "JNI class registry extends one platform class without replacing it") {
  ogplay::runtime::JniClassRegistry classes;
  const auto object = classes.RegisterClass({"java/lang/Object", {}, {}, {}});
  const auto bundle =
      classes.RegisterClass({"android/os/Bundle", "java/lang/Object", {}, {}});
  const auto constructor = classes.RegisterMethod(
      bundle, {"<init>", "()V", "dexvm.bundle.init", false});
  CHECK(classes.FindClass("java/lang/Object") == object);
  CHECK(classes.FindClass("android/os/Bundle") == bundle);
  CHECK(classes.GetMethodId(bundle, "<init>", "()V", false) == constructor);
  CHECK(classes.ResolveMethod(constructor).declaration.implementation ==
        "dexvm.bundle.init");
  CHECK_THROWS_AS(static_cast<void>(classes.RegisterMethod(
                      bundle, {"<init>", "()V", "duplicate", false})),
                  ogplay::runtime::JniClassRegistryError);
  CHECK_THROWS_AS(static_cast<void>(classes.RegisterMethod(
                      bundle, {"broken", "(V)V", "bad", false})),
                  ogplay::runtime::JniClassRegistryError);
  CHECK(classes.GetMethodId(bundle, "broken", "()V", false) == std::nullopt);
}

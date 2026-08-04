#include <doctest/doctest.h>

#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace {

struct FieldFixture final {
    FieldFixture() {
        object_class = classes.RegisterClass(
            {"java/lang/Object", {}, {}, {}});
        base_class = classes.RegisterClass(
            {"example/Base", "java/lang/Object", {},
             {{"count", "I", "base.count", false},
              {"token", "Ljava/lang/Object;", "base.token", false},
              {"shared", "J", "base.shared", true}}});
        child_class = classes.RegisterClass(
            {"example/Child", "example/Base", {}, {}});
        other_class = classes.RegisterClass(
            {"example/Other", "java/lang/Object", {}, {}});
        count = *classes.GetFieldId(child_class, "count", "I", false);
        token = *classes.GetFieldId(base_class, "token",
                                    "Ljava/lang/Object;", false);
        shared = *classes.GetFieldId(child_class, "shared", "J", true);
    }

    ogplay::runtime::JniClassRegistry classes;
    ogplay::runtime::JniFieldStore fields{classes};
    ogplay::runtime::JniObjectIdentity object_class;
    ogplay::runtime::JniObjectIdentity base_class;
    ogplay::runtime::JniObjectIdentity child_class;
    ogplay::runtime::JniObjectIdentity other_class;
    ogplay::runtime::JniFieldId count;
    ogplay::runtime::JniFieldId token;
    ogplay::runtime::JniFieldId shared;
};

}  // namespace

TEST_CASE("JNI instance fields use typed defaults and inherited declarations") {
    FieldFixture fixture;
    const auto first = ogplay::runtime::AllocateJniHostObjectIdentity();
    const auto second = ogplay::runtime::AllocateJniHostObjectIdentity();
    CHECK(std::get<ogplay::runtime::JniInt>(fixture.fields.GetInstance(
              first, fixture.child_class, fixture.count)) == 0);
    CHECK(std::get<ogplay::runtime::JniReference>(fixture.fields.GetInstance(
              first, fixture.child_class, fixture.token)).IsNull());
    fixture.fields.SetInstance(first, fixture.child_class, fixture.count,
                               ogplay::runtime::JniInt{42});
    CHECK(std::get<ogplay::runtime::JniInt>(fixture.fields.GetInstance(
              first, fixture.child_class, fixture.count)) == 42);
    CHECK(std::get<ogplay::runtime::JniInt>(fixture.fields.GetInstance(
              second, fixture.child_class, fixture.count)) == 0);
    fixture.fields.DeleteInstanceFields(first);
    CHECK(std::get<ogplay::runtime::JniInt>(fixture.fields.GetInstance(
              first, fixture.child_class, fixture.count)) == 0);
}

TEST_CASE("JNI static fields are shared through subclass dispatch") {
    FieldFixture fixture;
    CHECK(std::get<ogplay::runtime::JniLong>(fixture.fields.GetStatic(
              fixture.base_class, fixture.shared)) == 0);
    fixture.fields.SetStatic(fixture.child_class, fixture.shared,
                             ogplay::runtime::JniLong{99});
    CHECK(std::get<ogplay::runtime::JniLong>(fixture.fields.GetStatic(
              fixture.base_class, fixture.shared)) == 99);
}

TEST_CASE("JNI fields reject wrong kinds classes values and identities") {
    FieldFixture fixture;
    const auto instance = ogplay::runtime::AllocateJniHostObjectIdentity();
    CHECK_THROWS_AS(
        fixture.fields.SetInstance(instance, fixture.child_class,
                                   fixture.count, ogplay::runtime::JniLong{1}),
        ogplay::runtime::JniFieldStoreError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.fields.GetInstance(
            instance, fixture.other_class, fixture.count)),
        ogplay::runtime::JniFieldStoreError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.fields.GetInstance(
            instance, fixture.child_class, fixture.shared)),
        ogplay::runtime::JniFieldStoreError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.fields.GetStatic(fixture.base_class,
                                                   fixture.count)),
        ogplay::runtime::JniFieldStoreError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.fields.GetInstance(
            {}, fixture.child_class, fixture.count)),
        ogplay::runtime::JniFieldStoreError);
}

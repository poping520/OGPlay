#include <array>
#include <variant>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_environment.h"

TEST_CASE("JNI primitive arrays cover all eight zero-initialized kinds") {
    ogplay::runtime::JniPrimitiveArrayStore arrays;
    constexpr std::array kinds{
        ogplay::runtime::JniPrimitiveKind::boolean,
        ogplay::runtime::JniPrimitiveKind::byte,
        ogplay::runtime::JniPrimitiveKind::character,
        ogplay::runtime::JniPrimitiveKind::short_integer,
        ogplay::runtime::JniPrimitiveKind::integer,
        ogplay::runtime::JniPrimitiveKind::long_integer,
        ogplay::runtime::JniPrimitiveKind::float_value,
        ogplay::runtime::JniPrimitiveKind::double_value,
    };
    for (const auto kind : kinds) {
        const auto array = arrays.New(kind, 3);
        CHECK(arrays.Kind(array) == kind);
        CHECK(arrays.Length(array) == 3);
        CHECK(std::visit([](const auto& values) {
                  return values.size() == 3 && values[0] == 0;
              }, arrays.Region(array, 0, 3)));
        arrays.Delete(array);
    }
    CHECK_THROWS_AS(
        static_cast<void>(
            arrays.New(ogplay::runtime::JniPrimitiveKind::integer, -1)),
        ogplay::runtime::JniArrayError);
}

TEST_CASE("JNI primitive array regions are typed and bounds checked") {
    ogplay::runtime::JniPrimitiveArrayStore arrays;
    const auto array =
        arrays.New(ogplay::runtime::JniPrimitiveKind::integer, 5);
    const ogplay::runtime::JniPrimitiveArrayData replacement =
        std::vector<ogplay::runtime::JniInt>{7, 8, 9};
    const std::vector<ogplay::runtime::JniInt> expected{0, 7, 8, 9, 0};
    arrays.SetRegion(array, 1, replacement);
    CHECK(std::get<std::vector<ogplay::runtime::JniInt>>(
              arrays.Region(array, 0, 5)) == expected);
    const ogplay::runtime::JniPrimitiveArrayData wrong_type =
        std::vector<ogplay::runtime::JniByte>{1};
    CHECK_THROWS_AS(arrays.SetRegion(array, 0, wrong_type),
                    ogplay::runtime::JniArrayError);
    CHECK_THROWS_AS(static_cast<void>(arrays.Region(array, 4, 2)),
                    ogplay::runtime::JniArrayError);
    arrays.Delete(array);
}

TEST_CASE("JNI array elements implement commit abort and exact release") {
    ogplay::runtime::JniPrimitiveArrayStore arrays;
    ogplay::runtime::JniEnvironment environment;
    environment.AttachThread(5);
    const auto array =
        arrays.New(ogplay::runtime::JniPrimitiveKind::byte, 2);
    const auto local = environment.PublishLocalObject(5, array);
    CHECK_FALSE(local.IsNull());

    auto access = arrays.Acquire(
        array, ogplay::runtime::JniArrayAccessKind::elements);
    std::get<std::vector<ogplay::runtime::JniByte>>(access.data)[0] = 42;
    arrays.Release(array, access.token, access.kind, access.data,
                   ogplay::runtime::JniArrayReleaseMode::commit);
    CHECK_THROWS_AS(arrays.Delete(array), ogplay::runtime::JniArrayError);
    std::get<std::vector<ogplay::runtime::JniByte>>(access.data)[1] = 24;
    arrays.Release(array, access.token, access.kind, access.data,
                   ogplay::runtime::JniArrayReleaseMode::abort);
    const auto stored = std::get<std::vector<ogplay::runtime::JniByte>>(
        arrays.Region(array, 0, 2));
    const std::vector<ogplay::runtime::JniByte> expected{42, 0};
    CHECK(stored == expected);
    CHECK_THROWS_AS(
        arrays.Release(array, access.token, access.kind, access.data,
                       ogplay::runtime::JniArrayReleaseMode::abort),
        ogplay::runtime::JniArrayError);

    environment.DeleteLocalRef(5, local);
    environment.DetachThread(5);
    arrays.Delete(array);
}

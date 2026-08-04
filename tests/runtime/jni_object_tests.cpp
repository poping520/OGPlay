#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni_environment.h"
#include "ogplay/runtime/jni_object.h"

TEST_CASE("JNI string objects preserve UTF-16 and modified UTF-8 regions") {
    ogplay::runtime::JniStringStore strings;
    const std::vector<ogplay::runtime::JniChar> value{'A', 0, 0x4E2D,
                                                       0xD83D, 0xDE00};
    const auto string = strings.Create(value);
    const std::vector<ogplay::runtime::JniChar> expected_region{0, 0x4E2D};
    const std::vector<std::uint8_t> expected_utf_region{0x41, 0xC0, 0x80};
    CHECK(strings.Length(string) == 5);
    CHECK(strings.ModifiedUtf8Length(string) == 12);
    CHECK(strings.Region(string, 1, 2) == expected_region);
    CHECK(strings.ModifiedUtf8Region(string, 0, 2) == expected_utf_region);

    const auto encoded = strings.Acquire(
        string, ogplay::runtime::JniStringAccessKind::modified_utf8);
    const auto decoded = strings.CreateModifiedUtf8(encoded.modified_utf8);
    CHECK(strings.Region(decoded, 0, strings.Length(decoded)) == value);
    strings.Release(string, encoded.token, encoded.kind);
    strings.Delete(decoded);
    strings.Delete(string);
}

TEST_CASE("JNI string chars leases require exact object kind and release") {
    ogplay::runtime::JniStringStore strings;
    const std::vector<ogplay::runtime::JniChar> text{'o', 'g'};
    const auto first = strings.Create(text);
    const auto second = strings.Create(text);
    const auto chars =
        strings.Acquire(first, ogplay::runtime::JniStringAccessKind::chars);
    CHECK(chars.is_copy);
    CHECK(chars.chars == text);
    CHECK_THROWS_AS(strings.Delete(first), ogplay::runtime::JniStringError);
    CHECK_THROWS_AS(
        strings.Release(second, chars.token,
                        ogplay::runtime::JniStringAccessKind::chars),
        ogplay::runtime::JniStringError);
    CHECK_THROWS_AS(
        strings.Release(first, chars.token,
                        ogplay::runtime::JniStringAccessKind::critical),
        ogplay::runtime::JniStringError);
    strings.Release(first, chars.token, chars.kind);
    CHECK_THROWS_AS(strings.Release(first, chars.token, chars.kind),
                    ogplay::runtime::JniStringError);
    strings.Delete(first);
    strings.Delete(second);
}

TEST_CASE("JNI string identities publish into the common environment") {
    ogplay::runtime::JniStringStore strings;
    ogplay::runtime::JniEnvironment environment;
    environment.AttachThread(3);
    const std::vector<ogplay::runtime::JniChar> text{'J', 'N', 'I'};
    const auto identity = strings.Create(text);
    const auto local = environment.PublishLocalObject(3, identity);
    CHECK_FALSE(local.IsNull());
    CHECK_THROWS_AS(static_cast<void>(strings.Region(identity, -1, 1)),
                    ogplay::runtime::JniStringError);
    CHECK_THROWS_AS(static_cast<void>(strings.Region(identity, 2, 2)),
                    ogplay::runtime::JniStringError);
    environment.DeleteLocalRef(3, local);
    environment.DetachThread(3);
    strings.Delete(identity);
}

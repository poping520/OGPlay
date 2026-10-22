#include "ogplay/runtime/integration/java_payload_provider.h"
#include "ogplay/runtime/jni/host_copy_payload_provider.h"
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_object.h"

TEST_CASE("JNI string objects preserve UTF-16 and modified UTF-8 regions") {
    ogplay::runtime::HostCopyJavaPayloadProvider strings_payload;
    ogplay::runtime::JniStringStore strings{strings_payload};
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

TEST_CASE("DVM-260 String direct pins and UTF8 copies have independent lifetimes") {
    using namespace ogplay::runtime;
    ogplay::memory::AddressSpace memory;
    ogplay::core::CapabilityLedger ledger;
    GuestJavaPayloadProvider provider{memory, ledger};
    JniStringStore strings{provider};
    std::vector<JniChar> text{'A', 0, 0xd83d, 0xde00};
    const auto first = strings.Create(text);
    text[0] = 'X';
    auto chars = strings.Acquire(first, JniStringAccessKind::chars);
    auto critical = strings.Acquire(first, JniStringAccessKind::critical);
    CHECK_FALSE(chars.is_copy);
    CHECK(chars.address == critical.address);
    CHECK(strings.PinCount(first) == 2);
    CHECK(memory.Read16(chars.address) == 'A');
    CHECK(memory.Read16(chars.address.Add(2)) == 0);
    CHECK(memory.Read16(chars.address.Add(4)) == 0xd83d);
    CHECK(memory.Read16(chars.address.Add(6)) == 0xde00);
    const auto utf8 = strings.Acquire(first, JniStringAccessKind::modified_utf8);
    CHECK(utf8.is_copy);
    CHECK_THROWS_AS(strings.Delete(first), JniStringError);
    CHECK(strings.Release(first, 0, JniStringAccessKind::critical));
    CHECK(strings.Release(first, 0, JniStringAccessKind::chars));
    CHECK_FALSE(strings.Release(first, utf8.token, JniStringAccessKind::chars));
    CHECK_THROWS_AS(strings.Delete(first), JniStringError);
    CHECK(strings.Release(first, utf8.token, utf8.kind));
    CHECK_THROWS_AS(strings.Release(first, utf8.token, utf8.kind), JniStringError);
    strings.Delete(first);
    const auto empty = strings.Create({});
    const auto zero = strings.Acquire(empty, JniStringAccessKind::chars);
    CHECK_FALSE(zero.address.IsNull());
    strings.Release(empty, 0, JniStringAccessKind::critical);
    strings.Delete(empty);
    const std::vector<JniChar> large(1024 * 1024 + 1, 0x1234);
    const auto long_string = strings.Create(large);
    auto long_pin = strings.Acquire(long_string, JniStringAccessKind::critical);
    CHECK(memory.Read16(long_pin.address.Add((large.size() - 1) * 2)) == 0x1234);
    strings.Release(long_string, 0, JniStringAccessKind::chars);
    strings.Delete(long_string);
    CHECK(provider.Statistics().allocated_bytes == 0);
}

TEST_CASE("JNI string identities publish into the common environment") {
    ogplay::runtime::HostCopyJavaPayloadProvider strings_payload;
    ogplay::runtime::JniStringStore strings{strings_payload};
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

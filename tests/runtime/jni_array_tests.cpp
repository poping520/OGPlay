#include "ogplay/runtime/integration/java_payload_provider.h"
#include <thread>
#include "ogplay/runtime/jni/host_copy_payload_provider.h"
#include <array>
#include <variant>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_environment.h"

TEST_CASE("JNI primitive arrays cover all eight zero-initialized kinds") {
    ogplay::runtime::HostCopyJavaPayloadProvider arrays_payload;
    ogplay::runtime::JniPrimitiveArrayStore arrays{arrays_payload};
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
    ogplay::runtime::HostCopyJavaPayloadProvider arrays_payload;
    ogplay::runtime::JniPrimitiveArrayStore arrays{arrays_payload};
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

TEST_CASE("DVM-260 direct array pins preserve writes and cross-thread ownership") {
    using namespace ogplay::runtime;
    ogplay::memory::AddressSpace memory;
    ogplay::core::CapabilityLedger ledger;
    GuestJavaPayloadProvider provider{memory, ledger};
    JniPrimitiveArrayStore arrays{provider};
    const auto array = arrays.New(JniPrimitiveKind::byte, 2);
    const auto access = arrays.Acquire(array, JniArrayAccessKind::elements);
    CHECK_FALSE(access.is_copy);
    CHECK(arrays.Acquire(array, JniArrayAccessKind::critical).address == access.address);
    CHECK(arrays.PinCount(array) == 2);
    std::thread writer([&] {
        memory.Write8(access.address, 42);
        CHECK(arrays.Release(array, JniArrayReleaseMode::abort));
    });
    writer.join();
    CHECK(std::get<std::vector<JniByte>>(arrays.Region(array, 0, 2))[0] == 42);
    CHECK(arrays.PinCount(array) == 1);
    CHECK(arrays.Release(array, JniArrayReleaseMode::commit));
    CHECK(arrays.PinCount(array) == 1);
    CHECK_THROWS_AS(arrays.Delete(array), JniArrayError);
    memory.Write8(access.address.Add(1), 24);
    CHECK(arrays.Release(array, JniArrayReleaseMode::abort));
    CHECK(std::get<std::vector<JniByte>>(arrays.Region(array, 0, 2))[1] == 24);
    CHECK_FALSE(arrays.Release(array, JniArrayReleaseMode::abort));
    CHECK(arrays.PinCount(array) == 0);
    arrays.Delete(array);
}

TEST_CASE("DVM-260 zero length pins threshold and teardown remain bounded") {
    using namespace ogplay::runtime;
    ogplay::memory::AddressSpace memory;
    ogplay::core::CapabilityLedger ledger;
    GuestJavaPayloadProvider provider{memory, ledger};
    std::uint64_t objects{}, bytes{};
    {
        JniPrimitiveArrayStore arrays{provider};
        arrays.SetPinTeardownObserver([&](auto n, auto b) {
            objects = n;
            bytes = b;
        });
        const auto zero = arrays.New(JniPrimitiveKind::byte, 0);
        const auto address = arrays.Acquire(zero, JniArrayAccessKind::elements).address;
        CHECK_FALSE(address.IsNull());
        const auto other = arrays.New(JniPrimitiveKind::byte, 0);
        CHECK(arrays.Acquire(other, JniArrayAccessKind::critical).address != address);
        for (int i = 2; i <= 12; ++i) {
            const auto pin = arrays.Acquire(zero, JniArrayAccessKind::critical);
            CHECK(pin.address == address);
            CHECK(pin.warn_threshold == (i == 11));
        }
        CHECK(arrays.PinCount(zero) == 12);
    }
    CHECK(objects == 2);
    CHECK(bytes == 0);
    CHECK(provider.Statistics().allocated_bytes == 0);
}

TEST_CASE("DVM-260 host copy provider explicitly rejects direct borrow") {
    ogplay::runtime::HostCopyJavaPayloadProvider provider;
    ogplay::runtime::JniPrimitiveArrayStore arrays{provider};
    const auto array = arrays.New(ogplay::runtime::JniPrimitiveKind::byte, 0);
    CHECK_THROWS_AS(static_cast<void>(arrays.Acquire(array, ogplay::runtime::JniArrayAccessKind::elements)),
                    std::logic_error);
    CHECK(arrays.PinCount(array) == 0);
    arrays.Delete(array);
}

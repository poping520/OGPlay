#include <stdexcept>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/headless_jni_contract.h"

TEST_CASE("headless JNI contract closes both call directions") {
    std::size_t calls{};
    const auto report = ogplay::runtime::RunHeadlessJniContract(
        [&calls](const ogplay::memory::GuestAddress target,
                 const std::span<const ogplay::runtime::JniValue> arguments) {
            ++calls;
            CHECK(target == ogplay::memory::GuestAddress{0x72001000});
            REQUIRE(arguments.size() == 1);
            CHECK(std::get<ogplay::runtime::JniInt>(arguments[0]) == 40);
            return ogplay::runtime::JniValue{ogplay::runtime::JniInt{42}};
        });
    CHECK(calls == 1);
    CHECK(report.java_to_native);
    CHECK(report.native_to_java);
    CHECK(report.data_round_trip);
    CHECK(report.reference_closed);
    CHECK(report.exception_closed);
    CHECK(report.asset_round_trip);
    CHECK(report.preferences_round_trip);
    CHECK(report.locale_round_trip);
    CHECK(report.package_round_trip);
    CHECK(report.native_result == 42);
    CHECK(report.lifecycle_event_count == 7);
}

TEST_CASE("headless JNI contract produces a deterministic cumulative trace") {
    const auto execute = [](ogplay::memory::GuestAddress,
                            std::span<const ogplay::runtime::JniValue>) {
        return ogplay::runtime::JniValue{ogplay::runtime::JniInt{42}};
    };
    const auto first = ogplay::runtime::RunHeadlessJniContract(execute);
    const auto second = ogplay::runtime::RunHeadlessJniContract(execute);
    const std::vector<std::string> expected{
        "vm.attach",             "hle.construct",
        "hle.create",            "framework.asset",
        "framework.preferences", "framework.locale",
        "framework.package",     "java.nativeStep",
        "native.lifecycle.enter", "jni.data",
        "jni.reference",         "jni.exception",
        "native.lifecycle.exit", "vm.detach"};
    CHECK(first.trace == expected);
    CHECK(second.trace == expected);
}

TEST_CASE("headless JNI contract rejects absent and mistyped native execution") {
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::RunHeadlessJniContract({})),
        ogplay::runtime::HeadlessJniContractError);
    const auto wrong = [](ogplay::memory::GuestAddress,
                          std::span<const ogplay::runtime::JniValue>) {
        return ogplay::runtime::JniValue{ogplay::runtime::JniLong{42}};
    };
    try {
        static_cast<void>(ogplay::runtime::RunHeadlessJniContract(wrong));
        FAIL("mistyped guest native result was accepted");
    } catch (const ogplay::runtime::HeadlessJniContractError& error) {
        CHECK(error.Reason() == ogplay::runtime::HeadlessJniContractErrorReason::
                                    native_return_mismatch);
    }
    const auto throwing = [](ogplay::memory::GuestAddress,
                             std::span<const ogplay::runtime::JniValue>)
        -> ogplay::runtime::JniValue {
        throw std::runtime_error("guest native stopped");
    };
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::runtime::RunHeadlessJniContract(throwing)),
        "guest native stopped", std::runtime_error);
}

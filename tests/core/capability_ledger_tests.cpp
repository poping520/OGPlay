#include <doctest/doctest.h>

#include <filesystem>

#include "ogplay/core/capability_ledger.h"

TEST_CASE("capability ledger loads the repository ledger") {
    const auto path = std::filesystem::path(OGPLAY_SOURCE_DIR) / "capabilities.toml";
    auto ledger = ogplay::core::CapabilityLedger::Load(path);

    const auto logger = ledger.Find("core.structured_logger");
    REQUIRE(logger.has_value());
    CHECK(logger->status == ogplay::core::CapabilityStatus::complete);
    CHECK_FALSE(logger->test.empty());

    const auto ledger_capability = ledger.Find("core.capability_ledger");
    REQUIRE(ledger_capability.has_value());
    CHECK(ledger_capability->status == ogplay::core::CapabilityStatus::complete);
}

TEST_CASE("unimplemented hits retain first and last link registers") {
    ogplay::core::CapabilityLedger ledger;
    ledger.RecordUnimplemented("jni.GetIntField", 0x1000);
    ledger.RecordUnimplemented("jni.GetIntField", 0x2000);

    const auto hits = ledger.Unimplemented();
    REQUIRE(hits.size() == 1);
    CHECK(hits.front().count == 2);
    CHECK(hits.front().first_lr == 0x1000);
    CHECK(hits.front().last_lr == 0x2000);
}

TEST_CASE("duplicate capability ids are rejected") {
    ogplay::core::CapabilityLedger ledger;
    ledger.Register({.id = "hle.test", .status = ogplay::core::CapabilityStatus::unimplemented,
                     .test = "", .note = ""});
    CHECK_THROWS_AS(ledger.Register({.id = "hle.test",
                                    .status = ogplay::core::CapabilityStatus::unimplemented,
                                    .test = "", .note = ""}), std::runtime_error);
}

TEST_CASE("null calls are grouped by link register and symbol") {
    ogplay::core::CapabilityLedger ledger;
    ledger.RecordNullCall(0x1234, "optional.render");
    ledger.RecordNullCall(0x1234, "optional.render");
    ledger.RecordNullCall(0x1234, "optional.audio");

    const auto hits = ledger.NullCalls();
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].count + hits[1].count == 3);
}

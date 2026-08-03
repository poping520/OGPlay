#include <doctest/doctest.h>

#include <filesystem>

#include "ogplay/core/capability_ledger.h"

TEST_CASE("capability ledger loads the repository ledger") {
    const auto path = std::filesystem::path(OGPLAY_SOURCE_DIR) / "capabilities.toml";
    auto ledger = ogplay::core::CapabilityLedger::Load(path);

    const auto logger = ledger.Find("core.structured_logger");
    REQUIRE(logger.has_value());
    CHECK(logger->status == ogplay::core::CapabilityStatus::partial);
    CHECK_FALSE(logger->test.empty());
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
    ledger.Register({.id = "hle.test"});
    CHECK_THROWS_AS(ledger.Register({.id = "hle.test"}), std::runtime_error);
}


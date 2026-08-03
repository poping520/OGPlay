#include <doctest/doctest.h>

#include "ogplay/core/logger.h"

TEST_CASE("logger retains the newest structured records") {
    ogplay::core::Logger logger(2);
    logger.Write(ogplay::core::LogLevel::debug, "ogplay.cpu.jit", "first record");
    logger.Write(ogplay::core::LogLevel::warn, "ogplay.hle.jni", "second record",
                 {.frame = 12}, {{"slot", std::uint64_t{17}}});
    logger.Write(ogplay::core::LogLevel::error, "ogplay.hle.jni", "third record");

    const auto records = logger.Snapshot();
    REQUIRE(records.size() == 2);
    CHECK(records.front().message == "second record");
    CHECK(records.front().frame == 12);
    CHECK(logger.DroppedCount() == 1);
}

TEST_CASE("logger filters by level and category prefix") {
    ogplay::core::Logger logger;
    logger.Write(ogplay::core::LogLevel::debug, "ogplay.gl.draw", "draw");
    logger.Write(ogplay::core::LogLevel::warn, "ogplay.gl.error", "error");
    logger.Write(ogplay::core::LogLevel::error, "ogplay.hle.jni", "jni");

    const auto records = logger.Snapshot(ogplay::core::LogLevel::warn, "ogplay.gl");
    REQUIRE(records.size() == 1);
    CHECK(records.front().category == "ogplay.gl.error");
}

TEST_CASE("logger rejects unstructured records") {
    ogplay::core::Logger logger;
    CHECK_THROWS_AS(logger.Write(ogplay::core::LogLevel::info, "", "message"),
                    std::invalid_argument);
    CHECK_THROWS_AS(logger.Write(ogplay::core::LogLevel::info, "ogplay.core", ""),
                    std::invalid_argument);
}


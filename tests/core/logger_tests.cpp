#include <doctest/doctest.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"

namespace {

class TestSymbols final : public ogplay::core::GuestSymbolProvider {
public:
    std::optional<ogplay::core::SymbolizedAddress> Resolve(const std::uint64_t address) const override {
        if (address != 0x1234) return std::nullopt;
        return ogplay::core::SymbolizedAddress{"libguest.so", "guest_entry+0x4", 0x234, "guest.cpp:7"};
    }
};

class CollectingSink final : public ogplay::core::LogSink {
public:
    void Consume(const ogplay::core::LogRecord&,
                 const std::string_view text,
                 const std::string_view json) override {
        texts.emplace_back(text);
        json_records.emplace_back(json);
    }
    std::vector<std::string> texts;
    std::vector<std::string> json_records;
};

std::string ReadUtf8(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

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

TEST_CASE("logger applies hierarchical sink filters and automatic deduplication") {
    ogplay::core::Logger logger;
    auto sink = std::make_shared<CollectingSink>();
    logger.AddSink(sink, ogplay::core::LogLevel::debug, "ogplay.gl");

    logger.Write(ogplay::core::LogLevel::debug, "ogplay.gl.draw", "draw");
    logger.Write(ogplay::core::LogLevel::debug, "ogplay.gl.draw", "draw");
    logger.Write(ogplay::core::LogLevel::error, "ogplay.hle.jni", "failure");

    REQUIRE(sink->texts.size() == 1);
    CHECK(logger.SuppressedCount("ogplay.gl.draw", "draw") == 1);
}

TEST_CASE("logger supports deterministic per-frame and token-bucket limits") {
    ogplay::core::Logger logger;
    const ogplay::core::RateLimitPolicy every_two{
        .mode = ogplay::core::RateLimitMode::every_n_frames, .frame_interval = 2};
    for (std::uint64_t frame = 1; frame <= 4; ++frame) {
        logger.Write(ogplay::core::LogLevel::warn, "session.metric", "periodic",
                     {.frame = frame}, {}, every_two);
    }
    const ogplay::core::RateLimitPolicy bucket{
        .mode = ogplay::core::RateLimitMode::token_bucket,
        .tokens_per_second = 0.0001, .burst = 2.0};
    logger.Write(ogplay::core::LogLevel::warn, "hle.hot", "bucket", {}, {}, bucket);
    logger.Write(ogplay::core::LogLevel::warn, "hle.hot", "bucket", {}, {}, bucket);
    logger.Write(ogplay::core::LogLevel::warn, "hle.hot", "bucket", {}, {}, bucket);

    CHECK(logger.Snapshot(std::nullopt, "session.metric").size() == 2);
    CHECK(logger.Snapshot(std::nullopt, "hle.hot").size() == 2);
    CHECK(logger.SuppressedCount("hle.hot", "bucket") == 1);
}

TEST_CASE("guest addresses are symbolized in text and JSON") {
    ogplay::core::Logger logger;
    logger.SetSymbolProvider(std::make_shared<TestSymbols>());
    logger.Write(ogplay::core::LogLevel::error, "cpu.fault", "guest fault", {},
                 {{"pc", ogplay::core::GuestAddress{0x1234}}});

    const auto record = logger.Snapshot().front();
    CHECK(logger.RenderText(record).find("guest_entry") != std::string::npos);
    CHECK(logger.RenderJson(record).find("\"module\":\"libguest.so\"") != std::string::npos);
}

TEST_CASE("multiline text fields are rendered as indented blocks") {
    ogplay::core::Logger logger;
    logger.Write(ogplay::core::LogLevel::error, "cpu.fault", "guest fault",
                 {}, {{"reason", std::string{"outer\n  inner"}}});

    const auto record = logger.Snapshot().front();
    CHECK(logger.RenderText(record) ==
          "error [f=0] cpu.fault guest fault reason=\n"
          "  outer\n"
          "    inner");
    CHECK(logger.RenderJson(record).find("outer\\n  inner") !=
          std::string::npos);
}

TEST_CASE("diagnostic bundle contains ring, session and unimplemented state") {
    const auto directory = std::filesystem::temp_directory_path() / "ogplay-diagnostic-test";
    std::filesystem::remove_all(directory);
    ogplay::core::Logger logger;
    logger.Write(ogplay::core::LogLevel::fatal, "cpu.fault", "fatal guest fault");
    ogplay::core::CapabilityLedger ledger;
    ledger.RecordUnimplemented("syscall.999", 0x5678);

    logger.DumpDiagnosticBundle(directory, ledger, "fault summary", "{\"phase\":\"faulted\"}");

    CHECK(ReadUtf8(directory / "summary.txt") == "fault summary");
    CHECK(ReadUtf8(directory / "ring.jsonl").find("fatal guest fault") != std::string::npos);
    CHECK(ReadUtf8(directory / "unimplemented.json").find("syscall.999") != std::string::npos);
    CHECK(ReadUtf8(directory / "session.json").find("faulted") != std::string::npos);
    std::filesystem::remove_all(directory);
}

TEST_CASE("diagnostic bundle rejects malformed session JSON") {
    const auto directory = std::filesystem::temp_directory_path() /
                           "ogplay-invalid-diagnostic-test";
    std::filesystem::remove_all(directory);
    ogplay::core::Logger logger;
    ogplay::core::CapabilityLedger ledger;

    CHECK_THROWS_AS(logger.DumpDiagnosticBundle(directory, ledger, "summary",
                                                R"({"phase":})"),
                    std::invalid_argument);
    std::filesystem::remove_all(directory);
}

TEST_CASE("console and file sinks render text and JSONL") {
    std::FILE* console_file = nullptr;
#ifdef _MSC_VER
    REQUIRE(::tmpfile_s(&console_file) == 0);
#else
    console_file = std::tmpfile();
#endif
    REQUIRE(console_file != nullptr);
    {
        ogplay::core::Logger logger;
        logger.AddSink(std::make_shared<ogplay::core::ConsoleSink>(console_file),
                       ogplay::core::LogLevel::info);
        logger.Write(ogplay::core::LogLevel::warn, "ogplay.test", "console record");
    }
    std::rewind(console_file);
    std::array<char, 256> console_buffer{};
    const auto console_bytes = std::fread(console_buffer.data(), sizeof(char),
                                          console_buffer.size() - 1U, console_file);
    std::fclose(console_file);
    CHECK(std::string_view(console_buffer.data(), console_bytes).find("console record") !=
          std::string_view::npos);

    const auto path = std::filesystem::temp_directory_path() / "ogplay-jsonl-sink-test.jsonl";
    std::filesystem::remove(path);
    {
        ogplay::core::Logger logger;
        logger.AddSink(std::make_shared<ogplay::core::FileSink>(path, ogplay::core::LogFormat::jsonl),
                       ogplay::core::LogLevel::debug);
        logger.Write(ogplay::core::LogLevel::error, "ogplay.test", "file record");
    }
    const auto jsonl = ReadUtf8(path);
    CHECK(jsonl.find("\"message\":\"file record\"") != std::string::npos);
    std::filesystem::remove(path);
}

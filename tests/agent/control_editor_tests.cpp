#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "ogplay/agent/control_editor.h"

TEST_CASE("INP-5 control editor channel answers in order on the serving thread") {
    ogplay::agent::ControlEditorChannel channel;
    std::atomic<bool> stop{false};
    std::thread::id served_on;
    std::thread main_loop([&] {
        while (!stop) {
            channel.ServePending([&](std::string_view method, ogplay::core::JsonValue params) {
                served_on = std::this_thread::get_id();
                if (method == "control.fail") throw std::invalid_argument("bad params");
                const auto value = params.IsValid() ? params.Member("n") : std::nullopt;
                return ogplay::agent::ControlResult(value ? std::to_string(*value->Integer()) : "null");
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    ogplay::core::JsonParseError error;
    const auto params = ogplay::core::JsonDocument::ParseStrict(R"({"n":7})", error);
    REQUIRE(params.has_value());
    const auto answer = channel.Call("control.get", params->Root());
    const auto failed = channel.Call("control.fail", {});
    const auto main_id = main_loop.get_id();
    stop = true;
    main_loop.join();
    CHECK(answer.ok);
    CHECK(answer.json == R"({"result":7})");
    CHECK(served_on == main_id);
    CHECK_FALSE(failed.ok);
    CHECK(failed.json.find("-32602") != std::string::npos);
}

TEST_CASE("INP-5 unanswered requests time out and are withdrawn; closing rejects callers") {
    ogplay::agent::ControlEditorChannel channel;
    const auto busy = channel.Call("control.apply", {}, std::chrono::milliseconds(20));
    CHECK_FALSE(busy.ok);
    CHECK(busy.json.find("busy") != std::string::npos);
    bool served = false;
    channel.ServePending([&](std::string_view, ogplay::core::JsonValue) {
        served = true;
        return ogplay::agent::ControlResult("null");
    });
    CHECK_FALSE(served);

    std::thread closer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        channel.Close();
    });
    const auto closing = channel.Call("control.get", {}, std::chrono::seconds(5));
    closer.join();
    CHECK_FALSE(closing.ok);
    CHECK(closing.json.find("closing") != std::string::npos);
    CHECK(channel.Call("control.get", {}).json.find("closing") != std::string::npos);
}

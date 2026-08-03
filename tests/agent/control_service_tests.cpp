#include <doctest/doctest.h>

#include "ogplay/agent/control_service.h"
#include "ogplay/agent/json_rpc.h"
#include "ogplay/hal/clock.h"

namespace {

struct Fixture {
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    ogplay::hal::FixedStepClock clock{1'000, 60'000};
    ogplay::session::Session session{clock};
    ogplay::agent::ControlService service{ledger, logger, session};
    ogplay::agent::JsonRpcAdapter rpc{service};
};

class TestSymbols final : public ogplay::core::GuestSymbolProvider {
public:
    std::optional<ogplay::core::SymbolizedAddress> Resolve(const std::uint64_t address) const override {
        if (address != 4096) return std::nullopt;
        return ogplay::core::SymbolizedAddress{"libsample.so", "sample_entry", 16, "sample.cpp:4"};
    }
};

}  // namespace

TEST_CASE("control service exposes deterministic session state") {
    Fixture fixture;
    const auto ping = fixture.service.Request("system.ping");
    CHECK(ping.ok);
    CHECK(ping.json.find("ogplay-agent") != std::string::npos);

    CHECK(fixture.service.Request("session.state").json.find("\"phase\":\"idle\"") !=
          std::string::npos);
    CHECK(fixture.service.Request("session.open").ok);
    ogplay::agent::ControlParams step_params;
    step_params.frames = 6;
    const auto stepped = fixture.service.Request("run.step", step_params);
    CHECK(stepped.ok);
    CHECK(stepped.json.find("\"frame\":6") != std::string::npos);
    CHECK(stepped.json.find("\"guest_ticks\":6000") != std::string::npos);
}

TEST_CASE("unavailable operations and unknown methods fail explicitly") {
    Fixture fixture;
    CHECK_FALSE(fixture.service.Request("run.step").ok);
    CHECK_FALSE(fixture.service.Request("sym.resolve").ok);
    const auto unknown = fixture.service.Request("unknown.method");
    CHECK_FALSE(unknown.ok);
    CHECK(unknown.json.find("method_not_found") != std::string::npos);
}

TEST_CASE("unimplemented hits are queryable") {
    Fixture fixture;
    fixture.ledger.RecordUnimplemented("syscall.999", 0x1234);
    const auto response = fixture.service.Request("hle.unimplemented");
    CHECK(response.ok);
    CHECK(response.json.find("syscall.999") != std::string::npos);
    CHECK(response.json.find("\"count\":1") != std::string::npos);
}

TEST_CASE("null calls and guest symbols are first-class queries") {
    Fixture fixture;
    fixture.ledger.RecordNullCall(0x2000, "optional.render");
    fixture.logger.SetSymbolProvider(std::make_shared<TestSymbols>());

    const auto null_calls = fixture.service.Request("hle.null_calls");
    CHECK(null_calls.ok);
    CHECK(null_calls.json.find("optional.render") != std::string::npos);

    ogplay::agent::ControlParams symbol_params;
    symbol_params.address = 4096;
    const auto symbol = fixture.service.Request("sym.resolve", symbol_params);
    CHECK(symbol.ok);
    CHECK(symbol.json.find("sample_entry") != std::string::npos);

    const auto rpc_symbol = fixture.rpc.Handle(
        R"({"jsonrpc":"2.0","id":7,"method":"sym.resolve","params":{"address":4096}})");
    CHECK(rpc_symbol.find("libsample.so") != std::string::npos);
}

TEST_CASE("JSON-RPC drives an empty session without sleep") {
    Fixture fixture;
    const auto opened = fixture.rpc.Handle(
        R"({"jsonrpc":"2.0","id":1,"method":"session.open"})");
    CHECK(opened.find("\"id\":1") != std::string::npos);
    CHECK(opened.find("\"phase\":\"running\"") != std::string::npos);

    const auto stepped = fixture.rpc.Handle(
        R"({"jsonrpc":"2.0","id":"step","method":"run.step","params":{"frames":5}})");
    CHECK(stepped.find("\"id\":\"step\"") != std::string::npos);
    CHECK(stepped.find("\"frame\":5") != std::string::npos);

    const auto timeout = fixture.rpc.Handle(
        R"({"jsonrpc":"2.0","id":3,"method":"run.until","params":{"target_frame":20,"max_frames":4}})");
    CHECK(timeout.find("\"reached\":false") != std::string::npos);
    CHECK(timeout.find("\"frame\":9") != std::string::npos);
}

TEST_CASE("JSON-RPC uses standard parse, request and method errors") {
    Fixture fixture;
    CHECK(fixture.rpc.Handle("not-json").find("\"code\":-32700") != std::string::npos);
    CHECK(fixture.rpc.Handle(R"({"jsonrpc":"1.0","id":1,"method":"system.ping"})")
              .find("\"code\":-32600") != std::string::npos);
    CHECK(fixture.rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"missing"})")
              .find("\"code\":-32601") != std::string::npos);
}

#include <doctest/doctest.h>

#include "ogplay/agent/control_service.h"

TEST_CASE("control service exposes M0 state as structured JSON") {
    ogplay::core::CapabilityLedger ledger;
    ledger.Register({.id = "agent.session", .status = ogplay::core::CapabilityStatus::stub});
    ogplay::core::Logger logger;
    ogplay::agent::ControlService service(ledger, logger);

    const auto ping = service.Request("system.ping");
    CHECK(ping.ok);
    CHECK(ping.json.find("ogplay-agent") != std::string::npos);

    const auto state = service.Request("session.state");
    CHECK(state.ok);
    CHECK(state.json.find("\"phase\":\"idle\"") != std::string::npos);
}

TEST_CASE("unavailable operations and unknown methods fail explicitly") {
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    ogplay::agent::ControlService service(ledger, logger);

    CHECK_FALSE(service.Request("run.step").ok);
    CHECK_FALSE(service.Request("sym.resolve").ok);
    const auto unknown = service.Request("unknown.method");
    CHECK_FALSE(unknown.ok);
    CHECK(unknown.json.find("method_not_found") != std::string::npos);
}

TEST_CASE("unimplemented hits are queryable") {
    ogplay::core::CapabilityLedger ledger;
    ledger.RecordUnimplemented("syscall.999", 0x1234);
    ogplay::core::Logger logger;
    ogplay::agent::ControlService service(ledger, logger);

    const auto response = service.Request("hle.unimplemented");
    CHECK(response.ok);
    CHECK(response.json.find("syscall.999") != std::string::npos);
    CHECK(response.json.find("\"count\":1") != std::string::npos);
}


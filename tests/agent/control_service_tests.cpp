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

class TestGpu final : public ogplay::core::GpuStateProvider {
public:
    ogplay::core::GpuStats Stats() const override {
        return {12, 3, 2, 1, 4, {{7, 8, "GL_COLOR_ATTACHMENT0"}}};
    }

    std::vector<ogplay::core::GpuRenderTarget> RenderTargets() const override {
        return {{7, 640, 360, "RGBA8", {"color0", "depth"}, true}};
    }

    ogplay::core::GpuCapabilities Capabilities() const override {
        return {{"GL_EXT_debug_marker", "GL_OES_rgb8_rgba8"},
                {{"GL_MAX_TEXTURE_SIZE", 4096}}, "angle-d3d11-hardware"};
    }

    std::vector<ogplay::core::GpuTraceEntry> Trace(
        const std::string_view filter, const std::size_t limit) const override {
        last_filter = filter;
        last_limit = limit;
        if (exceed_limit) {
            return std::vector<ogplay::core::GpuTraceEntry>(limit + 1);
        }
        return {{"glClear", {{"mask", "GL_COLOR_BUFFER_BIT"}}, std::nullopt},
                {"glDrawArrays", {{"count", "3"}, {"mode", "GL_TRIANGLES"}}, 0x0502U}};
    }

    mutable std::string last_filter;
    mutable std::size_t last_limit{};
    bool exceed_limit{};
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
    CHECK(fixture.rpc.Handle(
              R"({"jsonrpc":"2.0","id":1,"id":2,"method":"system.ping"})")
              .find("\"code\":-32600") != std::string::npos);
    CHECK(fixture.rpc.Handle(
              R"({"jsonrpc":"2.0","id":1,"method":"system.ping","nested":{"method":"missing"}})")
              .find("\"result\"") != std::string::npos);
}

TEST_CASE("GPU agent queries serialize strong typed provider snapshots") {
    Fixture fixture;
    TestGpu gpu;
    ogplay::agent::ControlService service{
        fixture.ledger, fixture.logger, fixture.session, &gpu};

    const auto stats = service.Request("gpu.stats");
    CHECK(stats.ok);
    CHECK(stats.json.find("\"draws\":12") != std::string::npos);
    CHECK(stats.json.find("\"fbo\":7") != std::string::npos);
    CHECK(stats.json.find("GL_COLOR_ATTACHMENT0") != std::string::npos);

    const auto targets = service.Request("gpu.render_targets");
    CHECK(targets.ok);
    CHECK(targets.json.find("\"size\":{\"width\":640,\"height\":360}") !=
          std::string::npos);
    CHECK(targets.json.find("\"created_by_guest\":true") != std::string::npos);

    const auto capabilities = service.Request("gpu.capabilities");
    CHECK(capabilities.ok);
    CHECK(capabilities.json.find("GL_MAX_TEXTURE_SIZE\":4096") != std::string::npos);
    CHECK(capabilities.json.find("angle-d3d11-hardware") != std::string::npos);
}

TEST_CASE("GPU trace forwards filter and bounded limit through JSON-RPC") {
    Fixture fixture;
    TestGpu gpu;
    ogplay::agent::ControlService service{
        fixture.ledger, fixture.logger, fixture.session, &gpu};
    ogplay::agent::JsonRpcAdapter rpc{service};

    const auto response = rpc.Handle(
        R"({"jsonrpc":"2.0","id":"gpu","method":"gpu.trace","params":{"filter":"draw","limit":2}})");
    CHECK(response.find("\"id\":\"gpu\"") != std::string::npos);
    CHECK(response.find("glClear") != std::string::npos);
    CHECK(response.find("GL_TRIANGLES") != std::string::npos);
    CHECK(response.find("\"error\":1282") != std::string::npos);
    CHECK(gpu.last_filter == "draw");
    CHECK(gpu.last_limit == 2);
}

TEST_CASE("GPU queries fail explicitly without a valid bounded provider") {
    Fixture fixture;
    const auto unavailable = fixture.service.Request("gpu.stats");
    CHECK_FALSE(unavailable.ok);
    CHECK(unavailable.json.find("gpu_unavailable") != std::string::npos);

    TestGpu gpu;
    ogplay::agent::ControlService service{
        fixture.ledger, fixture.logger, fixture.session, &gpu};
    ogplay::agent::ControlParams invalid;
    invalid.limit = 0;
    CHECK(service.Request("gpu.trace", invalid).json.find("invalid_params") !=
          std::string::npos);
    invalid.limit = 1001;
    CHECK(service.Request("gpu.trace", invalid).json.find("invalid_params") !=
          std::string::npos);
    invalid.limit = 1;
    gpu.exceed_limit = true;
    CHECK(service.Request("gpu.trace", invalid).json.find("invalid_state") !=
          std::string::npos);
}

#include <doctest/doctest.h>
#if (defined(_WIN32) || defined(__APPLE__)) && OGPLAY_TEST_HAS_GUI
#include "ogplay/agent/dashboard.h"
#include "ogplay/agent/mcp_protocol.h"
#include "ogplay/app/logging.h"
#include "ogplay/frontend/mcp_http_server.h"
#include "ogplay/app/host.h"
#include "ogplay/hal/webview_host.h"
#include "../../src/frontend/gui/process_manager.h"
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include "gui_window_test_support.h"
#endif
#include <atomic>
#include <fstream>
#include <thread>

namespace {
struct DashboardFixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("ogplay-dashboard-window-" + std::to_string(ogplay::app::MonotonicClock::NowNs()));
    DashboardFixture() { std::filesystem::create_directories(root); }
    ~DashboardFixture() { std::error_code error; std::filesystem::remove_all(root, error); }
    void Write(const char* file, const char* text) { std::ofstream(root / file) << text; }
};
}
TEST_CASE("GUI Dashboard windows load without launcher bridge and reopen" * doctest::skip()) {
    using namespace ogplay;
    using namespace ogplay::app;
    DashboardFixture fixture;
    fixture.Write("manifest.json", "{}");
    fixture.Write("index.html", "<!doctype html><title>Dashboard isolation test</title><script src='dashboard.js'></script>");
    fixture.Write("dashboard.css", "body{color:white;background:#202020}");
    fixture.Write("THIRD-PARTY-LICENSES.txt", "fixture");
    fixture.Write("dashboard.js", R"(if(location.protocol==='http:' && typeof window.rpc==='undefined')fetch('/dash/rpc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({jsonrpc:'2.0',id:1,method:'dash.snapshot',params:{sections:['gpu']}})});)");
    std::atomic<unsigned> loads{};
    agent::DashboardSources sources;
    sources.gpu = [&]() -> std::optional<core::GpuStats> { ++loads; return std::nullopt; };
    agent::FrameSnapshotStore frames; agent::McpInputQueue inputs;
    auto server = frontend::McpHttpServer::Start(0, frames, inputs,
        {std::make_shared<agent::DashboardService>(sources), fixture.root});
    app::Logger logger;
    std::unique_ptr<hal::WebViewHost> host;
    unsigned stage{};
    const auto deadline = app::MonotonicClock::NowNs() + 15000000000ULL;
    host = hal::CreateWebViewHost({fixture.root / "index.html", 1, fixture.root / "capture.png"}, {
        [](std::string_view) { return std::string("{}"); },
        [&] {
            if (app::MonotonicClock::NowNs() > deadline) { host->RecordSmokeResponse(); return; }
            if (stage == 0) { ++stage; host->OpenDashboard("fixture", server->Port()); host->OpenDashboard("fixture", server->Port()); }
            else if (stage == 1 && loads.load() == 1) { ++stage; host->CloseDashboard("fixture"); }
            else if (stage == 2) { ++stage; host->OpenDashboard("fixture", server->Port()); }
            else if (stage == 3 && loads.load() == 2) { ++stage; host->CloseDashboard("fixture"); host->RecordSmokeResponse(); }
        }}, logger);
    CHECK(host->Run() == 0);
    CHECK(stage == 4);
    CHECK(loads.load() == 2);
    host.reset();
    // Exercise the actual WM_CLOSE path with a Dashboard still open.
    bool close_sent = false;
    const auto close_deadline = app::MonotonicClock::NowNs() + 5000000000ULL;
    host = hal::CreateWebViewHost({fixture.root / "index.html", std::nullopt, {}}, {
        [](std::string_view) { return std::string("{}"); },
        [&] {
            if (!close_sent) {
                close_sent = true;
                host->OpenDashboard("fixture", server->Port());
#if defined(_WIN32)
                EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
                    wchar_t title[64]{}; GetWindowTextW(window, title, 64);
                    if (std::wstring_view(title) == L"OGPlay") SendMessageW(window, WM_CLOSE, 0, 0);
                    return TRUE;
                }, 0);
#else
                REQUIRE(ogplay::test::CloseWindowTitled("OGPlay"));
#endif
            }
            if (app::MonotonicClock::NowNs() > close_deadline) throw std::runtime_error("main close timed out");
        }}, logger);
    CHECK(host->Run() == 0);
    CHECK(close_sent);
}
TEST_CASE("GUI Dashboard process tracking rejects duplicate ports and reaps exits") {
    using namespace ogplay;
    using namespace ogplay::app;
    DashboardFixture fixture; app::Logger logger;
    frontend::GuiProcessManager processes(logger);
    frontend::LaunchPlan plan;
    plan.package = "fixture"; plan.log_path = fixture.root / "run.log";
#if defined(_WIN32)
    plan.argv = {"powershell.exe", "-NoProfile", "-NonInteractive", "-Command", "Start-Sleep -Milliseconds 800; exit 7"};
#else
    plan.argv = {"/bin/sh", "-c", "echo child-stderr >&2; sleep 0.8; exit 7"};
#endif
    plan.mcp_port = 1; plan.dashboard_auto_open = true;
    processes.Launch(plan);
    REQUIRE(processes.Dashboards().size() == 1);
    CHECK(processes.Dashboards()[0].process_id != 0);
    CHECK_THROWS(static_cast<void>(processes.DashboardPort("fixture")));
    CHECK(processes.TakeAutoOpen().empty());
    plan.package = "other";
    CHECK_THROWS(processes.Launch(plan));
    std::vector<frontend::GameExit> exits;
    const auto deadline = app::MonotonicClock::NowNs() + 10000000000ULL;
    while (exits.empty() && app::MonotonicClock::NowNs() < deadline) {
        exits = processes.Poll(); std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(exits.size() == 1);
    CHECK(exits[0].exit_code == 7);
#if !defined(_WIN32)
    // The launcher reads this log tail for non-zero exits.
    std::ifstream log(fixture.root / "run.log");
    CHECK(std::string{std::istreambuf_iterator<char>(log), {}} == "child-stderr\n");
#endif
    CHECK(processes.Dashboards().empty());
    CHECK_THROWS(static_cast<void>(processes.DashboardPort("fixture")));
}
TEST_CASE("GUI Dashboard tracking destruction leaves child alive") {
    using namespace ogplay;
    using namespace ogplay::app;
    DashboardFixture fixture; app::Logger logger;
#if defined(_WIN32)
    fixture.Write("child.ps1", "Start-Sleep -Milliseconds 800\n[System.IO.File]::WriteAllText((Join-Path $PSScriptRoot 'alive.txt'), 'alive')\n");
#endif
    {
        frontend::GuiProcessManager processes(logger);
        frontend::LaunchPlan plan;
        plan.package = "fixture"; plan.log_path = fixture.root / "run.log";
#if defined(_WIN32)
        plan.argv = {"powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", (fixture.root / "child.ps1").string()};
#else
        plan.argv = {"/bin/sh", "-c", "sleep 0.8; echo alive > '" + (fixture.root / "alive.txt").string() + "'"};
#endif
        processes.Launch(plan);
        CHECK(processes.Dashboards()[0].status == "disabled");
    }
    const auto deadline = app::MonotonicClock::NowNs() + 10000000000ULL;
    while (!std::filesystem::exists(fixture.root / "alive.txt") && app::MonotonicClock::NowNs() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(std::filesystem::exists(fixture.root / "alive.txt"));
}
#endif

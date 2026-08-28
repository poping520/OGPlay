#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

#include "ogplay/runtime/debug/stall_diagnostics.h"

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <Aclapi.h>
#endif

namespace {

std::filesystem::path DiagnosticTestDirectory(const std::string_view name) {
    return std::filesystem::temp_directory_path() /
           ("ogplay-diag-test-" + std::string(name) + "-" +
            std::to_string(ogplay::runtime::debug::CurrentHostThreadId()));
}

std::uint64_t ControlPid(const std::filesystem::path& control) {
    const auto stem = control.stem().string();
    return std::stoull(stem.substr(std::string("diag-control-").size()));
}

const ogplay::runtime::debug::DiagnosticSectionInfo* FindSection(
    const ogplay::runtime::debug::GuestStallSnapshot& snapshot,
    const std::string_view name) {
    const auto found = std::ranges::find(snapshot.sections, name,
        &ogplay::runtime::debug::DiagnosticSectionInfo::name);
    return found == snapshot.sections.end() ? nullptr : &*found;
}

#if defined(_WIN32)
bool IsCurrentUserOnly(const std::filesystem::path& path) {
    PACL acl{};
    PSECURITY_DESCRIPTOR descriptor{};
    if (GetNamedSecurityInfoW(
            const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr,
            &descriptor) != ERROR_SUCCESS || acl == nullptr) return false;
    ACL_SIZE_INFORMATION info{};
    bool result = GetAclInformation(
        acl, &info, sizeof(info), AclSizeInformation) && info.AceCount == 1U;
    void* raw_ace{};
    if (result) result = GetAce(acl, 0U, &raw_ace) != 0;
    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
    result = result && ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE;
    HANDLE token{};
    DWORD bytes{};
    std::vector<std::byte> token_user;
    if (result) {
        result = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != 0;
    }
    if (result) {
        static_cast<void>(GetTokenInformation(
            token, TokenUser, nullptr, 0U, &bytes));
        token_user.resize(bytes);
        result = bytes != 0U && GetTokenInformation(
            token, TokenUser, token_user.data(), bytes, &bytes) != 0;
    }
    if (result) {
        const auto user = reinterpret_cast<const TOKEN_USER*>(
            token_user.data());
        result = EqualSid(user->User.Sid,
                          const_cast<DWORD*>(&ace->SidStart)) != 0;
    }
    if (token != nullptr) CloseHandle(token);
    if (descriptor != nullptr) LocalFree(descriptor);
    return result;
}
#endif

}  // namespace

TEST_CASE("stall snapshot keeps bounded cross-layer evidence without claiming deadlock") {
    using namespace ogplay::runtime;
    auto state = std::make_shared<debug::DiagnosticState>(4U, 2U);
    state->RecordSyscall(7U, 240U, 0, SupervisorCallProgress::handled_idle);
    state->SetNativeMethodResolver([](const std::uint64_t id) {
        return id == 42U ? "Lx;.native()V" : "";
    });
    const auto call = state->BeginNativeCall(11U, 7U, 42U);
    const auto execution = state->EnterExecution(
        7U, 11U, "guest_call", "InvokeA32GuestCall", 0x1234U);
    state->SetLifecyclePhase("teardown.thread_join", true);

    const auto snapshot = state->Collect("test");
    CHECK(snapshot.trigger == "test");
    CHECK(snapshot.lifecycle_phase == "teardown.thread_join");
    CHECK(snapshot.teardown_active);
    REQUIRE(snapshot.executions.size() == 1U);
    CHECK(snapshot.executions.front().host_tid != 0U);
    REQUIRE(snapshot.syscalls.size() == 1U);
    REQUIRE(snapshot.native_calls.size() == 1U);
    CHECK(snapshot.native_calls.front().method == "Lx;.native()V");
    const auto json = debug::RenderGuestStallSnapshotJson(snapshot);
    CHECK(json.find("\"schema_version\":1") != std::string::npos);
    CHECK(json.find("\"confirmed_cycles\":[]") != std::string::npos);

    state->EndNativeCall(call, false);
    state->LeaveExecution(execution);
}

TEST_CASE("stall snapshot rings overwrite oldest entries and report drops") {
    using namespace ogplay::runtime;
    auto state = std::make_shared<debug::DiagnosticState>(2U, 1U);
    state->RecordSyscall(1U, 1U, 0, SupervisorCallProgress::handled_idle);
    state->RecordSyscall(1U, 2U, 0, SupervisorCallProgress::handled_idle);
    state->RecordSyscall(1U, 3U, 0, SupervisorCallProgress::handled_idle);
    static_cast<void>(state->BeginNativeCall(1U, 1U, 10U));
    static_cast<void>(state->BeginNativeCall(1U, 1U, 11U));
    const auto threw = state->BeginNativeCall(1U, 1U, 12U);
    state->EndNativeCall(threw, true);

    const auto snapshot = state->Collect("overflow");
    REQUIRE(snapshot.syscalls.size() == 2U);
    CHECK(snapshot.syscalls[0].syscall_nr == 2U);
    CHECK(snapshot.syscalls[1].syscall_nr == 3U);
    CHECK(snapshot.syscalls_dropped == 1U);
    REQUIRE(snapshot.native_calls.size() == 2U);
    CHECK(snapshot.native_calls[0].method_id == 12U);
    CHECK(snapshot.native_calls[1].method_id == 12U);
    CHECK(snapshot.native_calls[1].phase == debug::DiagnosticNativePhase::threw);
    CHECK(snapshot.native_calls_dropped == 2U);
}

TEST_CASE("zero-capacity diagnostic rings retain no hot-path events") {
    using namespace ogplay::runtime;
    auto state = std::make_shared<debug::DiagnosticState>(0U, 0U);
    state->RecordSyscall(1U, 240U, 0,
                         SupervisorCallProgress::handled_idle);
    const auto call = state->BeginNativeCall(1U, 1U, 7U);
    state->EndNativeCall(call, false);
    const auto snapshot = state->Collect("disabled-rings");
    CHECK(snapshot.syscalls.empty());
    CHECK(snapshot.native_calls.empty());
    CHECK(snapshot.syscalls_dropped == 0U);
    CHECK(snapshot.native_calls_dropped == 0U);
}

TEST_CASE("monitor owner edges confirm only an explicit wait cycle") {
    using namespace ogplay::runtime;
    auto state = std::make_shared<debug::DiagnosticState>();
    state->SetMonitorProvider([] {
        return std::optional<std::vector<debug::DiagnosticMonitor>>(
            std::vector<debug::DiagnosticMonitor>{
                {1U, 2U, 1U, {3U}, {}},
                {2U, 3U, 1U, {2U}, {}}});
    });
    const auto snapshot = state->Collect("monitor-cycle");
    REQUIRE(snapshot.confirmed_cycles.size() == 1U);
    const std::vector<std::uint64_t> expected{2U, 3U};
    CHECK(snapshot.confirmed_cycles.front() == expected);
    CHECK(debug::RenderGuestStallSnapshotText(snapshot).find(
              "confirmed_cycle contexts=2->3->2") != std::string::npos);
}

TEST_CASE("busy supplemental source degrades one section without losing others") {
    using namespace ogplay::runtime;
    auto state = std::make_shared<debug::DiagnosticState>();
    state->SetDexVmProvider([]()
        -> std::optional<debug::DiagnosticDexVmSnapshot> {
        return std::nullopt;
    });
    state->SetMonitorProvider([] {
        return std::optional<std::vector<debug::DiagnosticMonitor>>(
            std::vector<debug::DiagnosticMonitor>{{7U, 2U, 1U, {3U}, {}}});
    });
    state->SetGlesProvider([]()
        -> std::optional<std::vector<debug::DiagnosticGlesEvent>> {
        throw std::runtime_error("synthetic provider failure");
    });
    state->SetNativeMethodResolver([](std::uint64_t) {
        return "https://guest.invalid/private";
    });
    static_cast<void>(state->BeginNativeCall(2U, 2U, 99U));
    state->RecordSyscall(2U, 240U, 0,
                         SupervisorCallProgress::handled_advanced);

    const auto snapshot = state->Collect("C:\\private\\guest.apk");
    CHECK(snapshot.trigger == "<redacted>");
    REQUIRE(FindSection(snapshot, "dexvm") != nullptr);
    CHECK(FindSection(snapshot, "dexvm")->status ==
          debug::DiagnosticSectionStatus::unavailable);
    CHECK(FindSection(snapshot, "dexvm")->reason == "busy");
    REQUIRE(FindSection(snapshot, "syscalls") != nullptr);
    CHECK(FindSection(snapshot, "syscalls")->status ==
          debug::DiagnosticSectionStatus::complete);
    REQUIRE(snapshot.monitors.size() == 1U);
    REQUIRE(FindSection(snapshot, "gles") != nullptr);
    CHECK(FindSection(snapshot, "gles")->reason == "provider_error");
    const auto json = debug::RenderGuestStallSnapshotJson(snapshot);
    CHECK(json.find("C:\\\\private") == std::string::npos);
    CHECK(json.find("https://guest.invalid") == std::string::npos);
    CHECK(json.find("<redacted>") != std::string::npos);
}

TEST_CASE("diagnostic coordinator supports direct external and teardown-timeout triggers") {
    using namespace std::chrono_literals;
    using namespace ogplay::runtime;
    const auto directory = DiagnosticTestDirectory("coordinator");
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    auto state = std::make_shared<debug::DiagnosticState>();
    auto coordinator = debug::DiagCoordinator::Start(
        state, {.output_directory = directory,
                .teardown_timeout = 30ms,
                .maximum_snapshots = 4U});

    const auto direct = coordinator->RequestSnapshotAndWait("direct", 2s);
    REQUIRE(direct.has_value());
    CHECK(std::filesystem::is_regular_file(direct->text));
    CHECK(std::filesystem::is_regular_file(direct->json));
#if defined(_WIN32)
    CHECK(IsCurrentUserOnly(direct->text));
    CHECK(IsCurrentUserOnly(direct->json));
    CHECK(IsCurrentUserOnly(coordinator->ControlFile()));
#endif

    const auto pid = ControlPid(coordinator->ControlFile());
    std::string external_trigger;
    CHECK_NOTHROW(external_trigger =
        debug::TriggerExternalDiagnostic(pid, directory));
    CHECK_FALSE(external_trigger.empty());
    std::size_t snapshot_count{};
    for (std::size_t attempt = 0; attempt < 200U; ++attempt) {
        snapshot_count = 0U;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().extension() == ".json" &&
                entry.path().filename().string().starts_with("diag-")) {
                ++snapshot_count;
            }
        }
        if (snapshot_count >= 2U) break;
        std::this_thread::sleep_for(5ms);
    }
    CHECK(snapshot_count >= 2U);

    state->SetLifecyclePhase("teardown.thread_join", true);
    for (std::size_t attempt = 0; attempt < 200U; ++attempt) {
        snapshot_count = 0U;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().extension() == ".json" &&
                entry.path().filename().string().starts_with("diag-")) {
                ++snapshot_count;
            }
        }
        if (snapshot_count >= 3U) break;
        std::this_thread::sleep_for(5ms);
    }
    CHECK(snapshot_count >= 3U);
    coordinator.reset();
    CHECK_FALSE(std::filesystem::exists(
        directory / ("diag-control-" + std::to_string(pid) + ".json")));
    std::filesystem::remove_all(directory, error);
}

TEST_CASE("diagnostic coordinator enforces directory quota and exits with queued work") {
    using namespace std::chrono_literals;
    using namespace ogplay::runtime;
    const auto directory = DiagnosticTestDirectory("quota-exit");
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    auto state = std::make_shared<debug::DiagnosticState>();
    auto coordinator = debug::DiagCoordinator::Start(
        state, {.output_directory = directory,
                .maximum_snapshots = 2U,
                .maximum_file_bytes = 1024U,
                .maximum_directory_bytes = 2048U});
    for (std::size_t index = 0; index < 4U; ++index) {
        REQUIRE(coordinator->RequestSnapshotAndWait("quota", 2s));
    }
    std::size_t snapshots{};
    std::uintmax_t bytes{};
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() ||
            !entry.path().filename().string().starts_with("diag-")) continue;
        bytes += entry.file_size();
        if (entry.path().extension() == ".json") ++snapshots;
    }
    CHECK(snapshots <= 2U);
    CHECK(bytes <= 2048U);
    std::atomic<bool> provider_entered{};
    std::atomic<bool> provider_release{};
    state->SetDexVmProvider([&]()
        -> std::optional<debug::DiagnosticDexVmSnapshot> {
        provider_entered.store(true, std::memory_order_release);
        while (!provider_release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        return debug::DiagnosticDexVmSnapshot{};
    });
    coordinator->RequestSnapshot("in-flight");
    while (!provider_entered.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    for (std::size_t index = 0; index < 16U; ++index) {
        coordinator->RequestSnapshot("queued");
    }
    std::thread releaser([&] {
        std::this_thread::sleep_for(10ms);
        provider_release.store(true, std::memory_order_release);
    });
    const auto started = std::chrono::steady_clock::now();
    coordinator.reset();
    releaser.join();
    state->SetDexVmProvider({});
    CHECK(std::chrono::steady_clock::now() - started < 2s);
    std::filesystem::remove_all(directory, error);
}

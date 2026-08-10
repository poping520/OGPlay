#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/bus.h"
#include "ogplay/session/profile_guest_lifecycle.h"
#include "ogplay/session/profile_native_execution.h"

namespace {

struct ExecutionFixture final {
    ExecutionFixture() : bus(memory), cpu(bus) {
        memory.Map({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        memory.Map({stack, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
    }

    void Start() {
        memory.Protect({code, memory.PageSize()},
                       ogplay::memory::PageProtection::read |
                           ogplay::memory::PageProtection::execute);
        ogplay::cpu::A32State state;
        state.SetThreadId(thread_id);
        cpu.SetState(state);
        lifecycle.Register(thread_id);
    }

    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus;
    ogplay::cpu::InterpreterCpu cpu;
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::A32SyscallDispatcher dispatcher{
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger)};
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    const ogplay::memory::GuestAddress code{0x10000U};
    const ogplay::memory::GuestAddress stack{0x20000U};
    const std::uint64_t thread_id{91};
};

}  // namespace

TEST_CASE("Profile native execution preserves ordered invocation results") {
    ExecutionFixture fixture;
    fixture.bus.Write32(fixture.code, 0xe0800001U);         // add r0, r0, r1
    fixture.bus.Write32(fixture.code.Add(4), 0xe12fff1eU);  // bx lr
    const auto second = fixture.code.Add(16);
    fixture.bus.Write32(second, 0xe59d0000U);               // ldr r0, [sp]
    fixture.bus.Write32(second.Add(4), 0xe12fff1eU);        // bx lr
    const auto return_trap = fixture.code.Add(64);
    fixture.bus.Write32(return_trap, 0xef000001U);          // svc #1
    fixture.Start();

    const std::vector<ogplay::session::ProfileNativeInvocation> invocations{
        {1, "Java_sample_First", fixture.code, {4U, 5U, 0U, 0U}, {}},
        {3, "Java_sample_Second", second, {},
         std::vector<std::uint32_t>{17U, 0U}}};
    const auto results =
        ogplay::session::ExecuteProfileNativeInvocations(
            invocations, fixture.cpu, fixture.dispatcher,
            fixture.lifecycle, fixture.memory,
            fixture.stack.Add(fixture.memory.PageSize()), return_trap, 16);

    REQUIRE(results.size() == 2);
    CHECK(results[0].call_index == 1);
    CHECK(results[0].export_name == "Java_sample_First");
    CHECK(results[0].guest.return_value == 9U);
    CHECK(results[1].call_index == 3);
    CHECK(results[1].guest.return_value == 17U);
}

TEST_CASE("Profile native execution preflights the full batch") {
    ExecutionFixture fixture;
    fixture.bus.Write32(fixture.code, 0xef000002U);         // svc #2
    fixture.bus.Write32(fixture.code.Add(4), 0xe12fff1eU);  // bx lr
    const auto return_trap = fixture.code.Add(64);
    fixture.bus.Write32(return_trap, 0xef000001U);
    fixture.Start();
    const std::vector<ogplay::session::ProfileNativeInvocation> invocations{
        {0, "Java_sample_First", fixture.code, {}, {}},
        {1, "Java_sample_Bad", fixture.code, {},
         std::vector<std::uint32_t>{1U}}};
    std::uint32_t hle_calls{};

    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            ogplay::session::ExecuteProfileNativeInvocations(
                invocations, fixture.cpu, fixture.dispatcher,
                fixture.lifecycle, fixture.memory,
                fixture.stack.Add(fixture.memory.PageSize()), return_trap,
                16,
                [&hle_calls](ogplay::cpu::Cpu&,
                             const ogplay::cpu::RunResult&) {
                    ++hle_calls;
                    return true;
                })),
        "profile native execution contains an incomplete invocation",
        ogplay::session::ProfileNativeExecutionError);
    CHECK(hle_calls == 0U);
}

TEST_CASE("Profile native execution identifies the failing export") {
    ExecutionFixture fixture;
    fixture.bus.Write32(fixture.code, 0xef000004U);  // unhandled svc #4
    const auto return_trap = fixture.code.Add(64);
    fixture.bus.Write32(return_trap, 0xef000001U);
    fixture.Start();
    const std::array<ogplay::session::ProfileNativeInvocation, 1> invocations{{
        {7, "Java_sample_Failing", fixture.code, {}, {}},
    }};

    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            ogplay::session::ExecuteProfileNativeInvocations(
                invocations, fixture.cpu, fixture.dispatcher,
                fixture.lifecycle, fixture.memory,
                fixture.stack.Add(fixture.memory.PageSize()), return_trap,
                8)),
        doctest::Contains(
            "profile native call 7 (Java_sample_Failing) failed: "
            "A32 guest call stopped outside a handled boundary"),
        ogplay::session::ProfileNativeExecutionError);
}

TEST_CASE("Profile guest lifecycle executes declared phases and input in order") {
    using namespace ogplay;
    session::TitleProfile profile;
    profile.runtime = {
        19, session::ProfileLifecycle::gl_surface_view, {80, 48},
        {
            {session::ProfileNativeCallPhase::startup, "sample/Renderer",
             "start", "()V", session::ProfileNativeDispatch::instance, {}},
            {session::ProfileNativeCallPhase::resume, "sample/View",
             "resume", "()V", session::ProfileNativeDispatch::static_method, {}},
            {session::ProfileNativeCallPhase::frame, "sample/Renderer",
             "render", "()V", session::ProfileNativeDispatch::static_method, {}},
            {session::ProfileNativeCallPhase::pointer_down, "sample/View",
             "pointer", "(III)V", session::ProfileNativeDispatch::instance,
             {{session::ProfileNativeArgumentSource::input_x, 0},
              {session::ProfileNativeArgumentSource::input_y, 0},
              {session::ProfileNativeArgumentSource::input_pointer, 0}}},
            {session::ProfileNativeCallPhase::key_down, "sample/View",
             "key", "(I)V", session::ProfileNativeDispatch::instance,
             {{session::ProfileNativeArgumentSource::input_key, 0}}},
            {session::ProfileNativeCallPhase::pause, "sample/View",
             "pause", "()V", session::ProfileNativeDispatch::instance, {}},
            {session::ProfileNativeCallPhase::shutdown, "sample/Renderer",
             "stop", "()V", session::ProfileNativeDispatch::instance, {}},
        }};
    profile.java_classes = {
        {"sample/Renderer",
         {{"resource", "()I", "resource.value", true}}},
    };
    std::vector<session::ProfileNativeCallTarget> targets;
    for (std::size_t index = 0; index < profile.runtime.native_calls.size();
         ++index) {
        targets.push_back(
            {index, "Java_sample_" + std::to_string(index),
             memory::GuestAddress{
                 static_cast<std::uint32_t>(0x1000U + index * 16U)}});
    }
    runtime::JniEnvironment environment;
    environment.AttachThread(1);
    runtime::JniClassRegistry classes;
    std::vector<std::string> events;
    std::vector<std::array<std::uint32_t, 4>> registers;
    std::vector<std::vector<std::uint32_t>> stack_words;
    bool fail_present{};
    const auto lifecycle = session::ProfileGuestLifecycle::Create(
        profile, targets,
        {
            memory::GuestAddress{0x7000U},
            &environment,
            &classes,
            [&events, &registers,
             &stack_words](const runtime::A32GuestCallFrame& frame) {
                events.push_back("call:" +
                                 std::to_string(frame.target.Value()));
                registers.push_back(frame.registers);
                stack_words.emplace_back(
                    frame.stack_words.begin(), frame.stack_words.end());
                return runtime::A32GuestCallResult{};
            },
            [&events] { events.emplace_back("open"); },
            [&events, &fail_present] {
                events.emplace_back("present");
                if (fail_present) throw std::runtime_error("present failed");
            },
            [&events] { events.emplace_back("finalize"); },
            [&events] { events.emplace_back("close"); },
            [&events](const runtime::AndroidBoundaryInput&) {
                events.emplace_back("input");
            },
        });

    const auto renderer_class = classes.FindClass("sample/Renderer");
    REQUIRE(renderer_class.has_value());
    CHECK(classes.GetMethodId(
              *renderer_class, "resource", "()I", true)
              .has_value());
    CHECK_THROWS_AS(
        lifecycle->QueueInput(
            {runtime::AndroidBoundaryInputType::key,
             7, 0.0F, 0.0F, true}),
        session::ProfileGuestLifecycleError);
    CHECK(lifecycle->Start().state == session::LifecycleRunState::running);
    CHECK_THROWS_WITH_AS(
        lifecycle->QueueInput(
            {runtime::AndroidBoundaryInputType::pointer_motion,
             0, 80.0F, 0.0F, true}),
        "profile guest input is outside its validated surface",
        session::ProfileGuestLifecycleError);
    lifecycle->QueueInput(
        {runtime::AndroidBoundaryInputType::pointer_button,
         2, 10.0F, 20.0F, true});
    lifecycle->QueueInput(
        {runtime::AndroidBoundaryInputType::key,
         7, 0.0F, 0.0F, true});
    const auto stepped = lifecycle->StepFrame();
    CHECK(stepped.frame == 1);
    CHECK(stepped.clock_ticks == 1'000);
    fail_present = true;
    CHECK_THROWS_WITH_AS(
        static_cast<void>(lifecycle->StepFrame()),
        "present failed", std::runtime_error);
    CHECK(lifecycle->State().state == session::LifecycleRunState::failed);
    CHECK(lifecycle->State().frame == 1);
    CHECK(lifecycle->Stop().state == session::LifecycleRunState::stopped);

    const std::vector<std::string> expected{
        "open", "call:4096", "call:4112", "input", "call:4144",
        "input", "call:4160", "call:4128", "present", "call:4128",
        "present", "call:4176", "call:4192", "finalize", "close"};
    CHECK(events == expected);
    REQUIRE(registers.size() == 8);
    CHECK(registers[2][2] == 10U);
    CHECK(registers[2][3] == 20U);
    REQUIRE(stack_words[2].size() == 2);
    CHECK(stack_words[2][0] == 2U);
    CHECK(registers[3][2] == 7U);
}

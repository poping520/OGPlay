#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_set>

#include "ogplay/cpu/thread_group.h"

namespace {

class RecordingCpu final : public ogplay::cpu::Cpu {
public:
    ogplay::cpu::RunResult Run(std::uint64_t) override {
        return {0, ogplay::cpu::RunStopReason::budget_exhausted,
                ogplay::memory::GuestAddress{0}, 0, 0, std::nullopt};
    }
    ogplay::cpu::A32State GetState() const override { return state_; }
    void SetState(const ogplay::cpu::A32State& state) override { state_ = state; }
    void RequestHalt() noexcept override {}

private:
    ogplay::cpu::A32State state_;
};

}  // namespace

TEST_CASE("each guest thread owns one CPU on one real host thread") {
    constexpr std::size_t kThreadCount = 8;
    std::latch ready(kThreadCount);
    std::latch release(1);
    std::mutex observation_mutex;
    std::atomic_bool thread_ids_match{true};
    std::unordered_set<std::thread::id> host_ids;
    std::unordered_set<const ogplay::cpu::Cpu*> cpu_instances;
    ogplay::cpu::GuestThreadGroup group(
        [] { return std::make_unique<RecordingCpu>(); });

    for (std::size_t index = 0; index < kThreadCount; ++index) {
        ogplay::cpu::A32State state;
        state.SetRegister(ogplay::cpu::CoreRegister::r0,
                          static_cast<std::uint32_t>(index));
        const auto thread_id = static_cast<std::uint64_t>(index + 1);
        group.Spawn({thread_id, ogplay::memory::GuestAddress{0x30000U}, state},
                    [&, index, thread_id](ogplay::cpu::Cpu& cpu) {
                        {
                            std::scoped_lock lock(observation_mutex);
                            host_ids.insert(std::this_thread::get_id());
                            cpu_instances.insert(&cpu);
                        }
                        auto local = cpu.GetState();
                        if (local.ThreadId() != thread_id) {
                            thread_ids_match.store(false);
                        }
                        local.SetRegister(ogplay::cpu::CoreRegister::r0,
                                          static_cast<std::uint32_t>(index + 100));
                        cpu.SetState(local);
                        ready.count_down();
                        release.wait();
                    });
    }
    ready.wait();
    CHECK(group.ThreadCount() == kThreadCount);
    CHECK(group.ActiveCount() == kThreadCount);
    CHECK(host_ids.size() == kThreadCount);
    CHECK(cpu_instances.size() == kThreadCount);
    CHECK(thread_ids_match.load());
    release.count_down();

    for (std::size_t index = 0; index < kThreadCount; ++index) {
        const auto result = group.Join(index + 1);
        CHECK(result.thread_id == index + 1);
        CHECK(result.tls_base == ogplay::memory::GuestAddress{0x30000U});
        CHECK(result.cpu_state.Register(ogplay::cpu::CoreRegister::r0) ==
              index + 100);
    }
    CHECK(group.ActiveCount() == 0);
}

TEST_CASE("guest thread lifecycle rejects invalid use and propagates failure") {
    ogplay::cpu::GuestThreadGroup group(
        [] { return std::make_unique<RecordingCpu>(); });
    CHECK_THROWS_AS(group.Spawn({}, [](ogplay::cpu::Cpu&) {}),
                    std::invalid_argument);
    group.Spawn({7, ogplay::memory::GuestAddress{0}, {}}, [](ogplay::cpu::Cpu&) {
        throw std::runtime_error("thread failure");
    });
    const auto join = [&group](const std::uint64_t thread_id) {
        static_cast<void>(group.Join(thread_id));
    };
    CHECK_THROWS_WITH_AS(join(7), "thread failure", std::runtime_error);
    CHECK_THROWS_AS(join(7), std::logic_error);
    CHECK_THROWS_AS(join(99), std::out_of_range);
}

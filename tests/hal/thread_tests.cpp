#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <latch>
#include <stdexcept>
#include <thread>

#include "ogplay/hal/thread.h"

TEST_CASE("host thread HAL starts real concurrent threads and joins them") {
    constexpr std::size_t kThreadCount = 4;
    std::latch ready(kThreadCount);
    std::latch release(1);
    std::array<std::thread::id, kThreadCount> observed_ids{};
    std::array<std::unique_ptr<ogplay::hal::HostThread>, kThreadCount> threads;

    for (std::size_t index = 0; index < threads.size(); ++index) {
        threads[index] = ogplay::hal::StartHostThread([&, index] {
            observed_ids[index] = std::this_thread::get_id();
            ready.count_down();
            release.wait();
        });
    }
    ready.wait();
    for (std::size_t index = 0; index < threads.size(); ++index) {
        CHECK(threads[index]->Joinable());
        CHECK(threads[index]->Id() == observed_ids[index]);
        for (std::size_t other = 0; other < index; ++other) {
            CHECK(observed_ids[index] != observed_ids[other]);
        }
    }
    release.count_down();
    for (auto& thread : threads) {
        thread->Join();
        CHECK_FALSE(thread->Joinable());
    }
}

TEST_CASE("host thread HAL rejects empty entries and repeated joins") {
    CHECK_THROWS_AS([] {
        auto thread = ogplay::hal::StartHostThread({});
        static_cast<void>(thread);
    }(), std::invalid_argument);
    auto thread = ogplay::hal::StartHostThread([] {});
    thread->Join();
    CHECK_THROWS_AS(thread->Join(), std::logic_error);
}

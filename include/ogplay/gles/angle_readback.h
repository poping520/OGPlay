#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "ogplay/gles/angle_frame.h"

namespace ogplay::gles {

// Private host presentation transport. The producer remains current on its
// guest GL thread; a shared host-only context collects fenced PBO pixels.
// Callback runs on the collector, never calls guest code, and is joined by Stop.
class AsyncAngleReadback final {
public:
    using Publish = std::function<void(std::vector<std::uint8_t>)>;
    [[nodiscard]] static std::shared_ptr<AsyncAngleReadback> Create(
        const AngleFrame& source, Publish publish);
    ~AsyncAngleReadback();
    AsyncAngleReadback(const AsyncAngleReadback&) = delete;
    AsyncAngleReadback& operator=(const AsyncAngleReadback&) = delete;
    [[nodiscard]] bool Matches(const AngleFrame& source) const noexcept;
    // False when retired; otherwise queues current pixels. Backpressure only
    // when both bounded slots are occupied. All producer pack state is restored.
    [[nodiscard]] bool Enqueue();
    void RethrowFailure() const;
    void Stop();
private:
    class Impl;
    explicit AsyncAngleReadback(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::gles

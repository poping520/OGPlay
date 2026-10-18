#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace ogplay::runtime {

struct SurfaceStreamFrame final {
    std::uint32_t width{}, height{};
    std::int64_t timestamp_ns{};
    // Tightly packed RGBA8, row zero at GL's bottom edge. The producer
    // supplies the transform for its image layout; timestamps are source PTS.
    std::vector<std::uint8_t> rgba;
    std::array<float, 16> transform{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};

class SurfaceStreamBudget final {
public:
    explicit SurfaceStreamBudget(std::size_t limit = 64U * 1024U * 1024U) : limit_(limit) {}
    void Reserve(std::size_t bytes);
    void Release(std::size_t bytes) noexcept;
    std::size_t Used() const;
private:
    mutable std::mutex mutex_;
    std::size_t used_{}, limit_{};
};

struct SurfaceProducerLease final {
    std::uint64_t stream_identity{}, epoch{};
    bool operator==(const SurfaceProducerLease&) const = default;
};

// No VM, guest pointers, GL or callback execution. All operations are bounded
// and nonblocking; callers perform graphics/callback work after taking a lease.
class SurfaceStream final {
public:
    enum class SubmitResult { queued, replaced, busy };
    struct Snapshot final {
        std::uint64_t sequence{};
        std::uint64_t stream_identity{};
        std::shared_ptr<const SurfaceStreamFrame> frame;
    };
    SurfaceStream(bool single_buffer, std::shared_ptr<SurfaceStreamBudget> budget);
    SurfaceProducerLease Connect();
    void Disconnect(SurfaceProducerLease producer);
    SubmitResult Submit(SurfaceProducerLease producer, SurfaceStreamFrame frame);
    Snapshot Pending() const;
    Snapshot Acquire();
    void Abort(const Snapshot& snapshot);
    Snapshot Current() const;
    void Commit(const Snapshot& snapshot);
    void ReleaseImage();
    bool TakeNotification();
    void Abandon();
    bool Live() const;
    bool SingleBuffered() const noexcept { return single_buffer_; }
    void SetDefaultSize(std::uint32_t width, std::uint32_t height);
    std::array<std::uint32_t, 2> DefaultSize() const;
private:
    void RequireProducer(SurfaceProducerLease producer) const;
    const std::uint64_t identity_;
    const bool single_buffer_;
    std::shared_ptr<SurfaceStreamBudget> budget_;
    mutable std::mutex mutex_;
    bool live_{true}, connected_{}, notification_{};
    std::uint64_t epoch_{}, sequence_{};
    Snapshot pending_, acquired_, current_;
    std::array<std::uint32_t, 2> default_size_{1, 1};
};

} // namespace ogplay::runtime

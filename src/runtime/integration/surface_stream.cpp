#include "ogplay/runtime/integration/surface_stream.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ogplay::runtime {
namespace {
std::uint64_t NextIdentity() {
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load();
    for (;;) {
        if (value == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("surface identity exhausted");
        if (next.compare_exchange_weak(value, value + 1)) return value;
    }
}
}  // namespace

void SurfaceStreamBudget::Reserve(const std::size_t bytes) {
    std::scoped_lock lock(mutex_);
    if (bytes > limit_ - used_) throw std::length_error("surface stream memory budget exhausted");
    used_ += bytes;
}

void SurfaceStreamBudget::Release(const std::size_t bytes) noexcept {
    std::scoped_lock lock(mutex_);
    used_ -= bytes;
}

std::size_t SurfaceStreamBudget::Used() const {
    std::scoped_lock lock(mutex_);
    return used_;
}

SurfaceStream::SurfaceStream(const bool single, std::shared_ptr<SurfaceStreamBudget> budget)
    : identity_(NextIdentity()), single_buffer_(single), budget_(std::move(budget)) {
    if (!budget_) throw std::invalid_argument("surface stream requires a memory budget");
}

SurfaceProducerLease SurfaceStream::Connect() {
    std::scoped_lock lock(mutex_);
    if (!live_ || connected_) throw std::logic_error("surface producer unavailable or already connected");
    if (epoch_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("surface producer epoch exhausted");
    connected_ = true;
    return {identity_, ++epoch_};
}

void SurfaceStream::RequireProducer(const SurfaceProducerLease producer) const {
    if (!live_ || !connected_ || producer.stream_identity != identity_ || producer.epoch == 0 ||
        producer.epoch != epoch_)
        throw std::invalid_argument("surface producer lease is stale");
}

void SurfaceStream::Disconnect(const SurfaceProducerLease producer) {
    std::scoped_lock lock(mutex_);
    RequireProducer(producer);
    connected_ = false;
    pending_ = {};
    notification_ = false;
}

SurfaceStream::SubmitResult SurfaceStream::Submit(const SurfaceProducerLease producer, SurfaceStreamFrame frame) {
    if (frame.width == 0 || frame.height == 0 || frame.width > 4096 || frame.height > 4096 ||
        frame.rgba.size() != static_cast<std::uint64_t>(frame.width) * frame.height * 4 ||
        !std::ranges::all_of(frame.transform, [](float value) { return std::isfinite(value); }))
        throw std::invalid_argument("surface image dimensions, layout or transform invalid");
    std::scoped_lock lock(mutex_);
    RequireProducer(producer);
    if (single_buffer_ && (pending_.frame || acquired_.frame || current_.frame)) return SubmitResult::busy;
    if (sequence_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("surface sequence exhausted");
    const auto bytes = frame.rgba.capacity();
    budget_->Reserve(bytes);
    std::unique_ptr<SurfaceStreamFrame> owned;
    try {
        owned = std::make_unique<SurfaceStreamFrame>(std::move(frame));
    } catch (...) {
        budget_->Release(bytes);
        throw;
    }
    const auto budget = budget_;
    std::shared_ptr<const SurfaceStreamFrame> image(owned.release(), [budget, bytes](const SurfaceStreamFrame* value) {
        delete value;
        budget->Release(bytes);
    });
    const auto replaced = bool(pending_.frame);
    pending_ = {++sequence_, identity_, std::move(image)};
    // API19 asynchronous replacement of a droppable pending buffer does not
    // send another onFrameAvailable callback (BufferQueue::queueBuffer).
    if (!replaced) notification_ = true;
    return replaced ? SubmitResult::replaced : SubmitResult::queued;
}

SurfaceStream::Snapshot SurfaceStream::Pending() const {
    std::scoped_lock lock(mutex_);
    return pending_;
}

SurfaceStream::Snapshot SurfaceStream::Current() const {
    std::scoped_lock lock(mutex_);
    return current_;
}

SurfaceStream::Snapshot SurfaceStream::Acquire() {
    std::scoped_lock lock(mutex_);
    if (!live_ || acquired_.frame) throw std::logic_error("surface acquisition unavailable");
    acquired_ = std::move(pending_);
    pending_ = {};
    return acquired_;
}

void SurfaceStream::Abort(const Snapshot& snapshot) {
    std::scoped_lock lock(mutex_);
    if (!snapshot.frame) return;
    if (snapshot.stream_identity != identity_ || acquired_.sequence != snapshot.sequence ||
        acquired_.frame != snapshot.frame)
        throw std::invalid_argument("invalid surface acquisition rollback");
    if (live_ && !pending_.frame) pending_ = acquired_;
    acquired_ = {};
}

void SurfaceStream::Commit(const Snapshot& snapshot) {
    std::scoped_lock lock(mutex_);
    if (!live_) throw std::logic_error("surface consumer is abandoned");
    if (!snapshot.frame || snapshot.stream_identity != identity_ || acquired_.sequence != snapshot.sequence ||
        acquired_.frame != snapshot.frame)
        throw std::invalid_argument("invalid surface acquisition");
    current_ = snapshot;
    acquired_ = {};
}

void SurfaceStream::ReleaseImage() {
    std::scoped_lock lock(mutex_);
    current_ = {};
}

bool SurfaceStream::TakeNotification() {
    std::scoped_lock lock(mutex_);
    const bool ready = notification_;
    notification_ = false;
    return ready && live_;
}

void SurfaceStream::Abandon() {
    std::scoped_lock lock(mutex_);
    live_ = false;
    connected_ = false;
    pending_ = {};
    acquired_ = {};
    current_ = {};
    notification_ = false;
}

bool SurfaceStream::Live() const {
    std::scoped_lock lock(mutex_);
    return live_;
}

void SurfaceStream::SetDefaultSize(const std::uint32_t width, const std::uint32_t height) {
    std::scoped_lock lock(mutex_);
    // API19 nativeSetDefaultBufferSize ignores BufferQueue's BAD_VALUE.
    if (width && height) default_size_ = {width, height};
}

std::array<std::uint32_t, 2> SurfaceStream::DefaultSize() const {
    std::scoped_lock lock(mutex_);
    return default_size_;
}
}  // namespace ogplay::runtime

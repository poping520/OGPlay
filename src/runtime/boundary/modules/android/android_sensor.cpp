#include "runtime/boundary/modules/android/android_module.h"

#include <bit>
#include <stdexcept>
#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"

namespace ogplay::runtime {
namespace {
constexpr memory::GuestAddress kSensorManager{0x6e008000U};
}

AndroidModule::~AndroidModule() {
    ShutdownLoopers();
    if (!sensor_list_.IsNull()) services_.address_space.Unmap(
        {sensor_list_, services_.address_space.PageSize()});
}

void AndroidModule::SetSensorDirectory(std::shared_ptr<const AndroidSensorDirectory> directory) {
    if (!directory) throw std::invalid_argument("sensor directory is missing");
    std::scoped_lock lock(mutex_);
    if (!sensor_queues_.empty()) throw std::logic_error("cannot replace a live sensor directory");
    sensor_directory_ = std::move(directory);
}

[[noreturn]] void AndroidModule::SensorFailure(std::string_view message, std::uint64_t thread) {
    if (ledger_) ledger_->RecordUnimplemented("dexvm.sensor_absent_facade", 0);
    if (logger_) logger_->Write(core::LogLevel::error, "runtime.android.sensor", message,
        {.guest_thread = thread}, {}, {.mode = core::RateLimitMode::none});
    throw std::invalid_argument(std::string(message));
}

std::int32_t AndroidModule::AddLooperFdLocked(memory::GuestAddress handle, std::int32_t fd,
    std::int32_t ident, std::uint32_t events, memory::GuestAddress callback, memory::GuestAddress data) {
    auto& looper = RequireLooper(handle);
    if (looper.retired || fd < 0 || ident < 0 || !looper.allow_non_callbacks ||
        !callback.IsNull() || !looper_hooks_.poll_events || (events & ~31U) != 0) return -1;
    const auto available = looper_hooks_.poll_events(fd);
    if (!available || (*available & 16U) != 0) return -1;
    looper.requests[fd] = {ident, events, data};
    ready_.notify_all();
    return 1;
}

std::int32_t AndroidModule::RemoveLooperFdLocked(memory::GuestAddress looper, std::int32_t fd) {
    return static_cast<std::int32_t>(RequireLooper(looper).requests.erase(fd));
}

void AndroidModule::CloseSensorQueue(const SensorQueue& queue) {
    if (const auto looper = loopers_.find(queue.looper.Value()); looper != loopers_.end()) {
        static_cast<void>(RemoveLooperFdLocked(queue.looper, queue.pipe[0]));
        if (--looper->second.references == 0) loopers_.erase(looper);
    }
    looper_hooks_.close_fd(queue.pipe[0]);
    looper_hooks_.close_fd(queue.pipe[1]);
}

void AndroidModule::RetireSensorQueues(std::optional<std::uint64_t> thread) {
    for (auto it = sensor_queues_.begin(); it != sensor_queues_.end();) {
        if (!thread || it->second.owner == *thread) {
            CloseSensorQueue(it->second);
            it = sensor_queues_.erase(it);
        } else ++it;
    }
}

BoundaryResult AndroidModule::SensorExport(const A32CallFrame& call, std::uint16_t id) {
    std::scoped_lock lock(mutex_);
    SweepLoopers();
    const auto thread = call.ThreadId();
    const auto args = call.RegisterArguments();
    if (loopers_shutdown_) SensorFailure("sensor process is shut down", thread);
    if (id == 85) return kSensorManager.Value();
    if (id >= 95) SensorFailure("ASensor is invalid in the empty device directory", thread);
    if (id <= 89) {
        if (call.Pointer<void>(0).Address() != kSensorManager) SensorFailure("ASensorManager is invalid", thread);
        if (sensor_directory_->Count() != 0) SensorFailure("nonempty sensor directory is unsupported", thread);
    }
    if (id == 86) {
        if (!call.Pointer<void>(1).IsNull()) {
            if (sensor_list_.IsNull()) {
                sensor_list_ = services_.address_space.MapAnywhere(
                    {memory::GuestAddress{0x60000000U}, 0xa0000000ULL},
                    services_.address_space.PageSize(), memory::PageProtection::read);
            }
            services_.Write32(args[1], sensor_list_.Value(), thread);
        }
        return 0;
    }
    if (id == 87) return 0; // There are no devices of any type.
    if (id == 88) {
        const auto looper = call.Pointer<void>(1).Address();
        if (looper.IsNull() || !loopers_.contains(looper.Value()) || RequireLooper(looper).retired)
            SensorFailure("sensor queue requires a live ALooper", thread);
        if (looper_hooks_.thread_running && !looper_hooks_.thread_running(thread))
            SensorFailure("sensor queue owner thread is retired", thread);
        if (!looper_hooks_.create_pipe || !looper_hooks_.close_fd)
            SensorFailure("sensor queue VFS pipe backend is unavailable", thread);
        if (sensor_queues_.size() >= 1024 || next_sensor_queue_.Value() >= 0x6ea00000U)
            SensorFailure("sensor queue handle budget exhausted", thread);
        if (RequireLooper(looper).references == UINT32_MAX) SensorFailure("sensor queue Looper reference overflow", thread);
        const auto pipe = looper_hooks_.create_pipe();
        ++RequireLooper(looper).references; // SensorEventQueue::looper is an sp<Looper>.
        const SensorQueue queue{thread, looper, pipe};
        const auto handle = next_sensor_queue_;
        try {
            const auto callback = call.Pointer<void>(3).Address();
            const auto result = AddLooperFdLocked(looper, pipe[0], std::bit_cast<std::int32_t>(args[2]),
                1, callback, call.Pointer<void>(4).Address());
            // API19 sensor.cpp deliberately ignores addFd's result. No devices
            // can make this pipe readable, so an unsupported callback is inert.
            if (result < 0) {
                if (ledger_) ledger_->RecordUnimplemented("runtime.android_boundary_hle", 0);
                if (logger_) logger_->Write(core::LogLevel::warn, "runtime.android.sensor",
                    "sensor queue Looper registration rejected; empty pipe cannot dispatch callbacks",
                    {.guest_thread = thread}, {{"callback", core::GuestAddress{callback.Value()}},
                    {"add_fd_result", static_cast<std::int64_t>(result)}}, {.mode = core::RateLimitMode::none});
            }
            sensor_queues_.emplace(handle.Value(), queue);
        } catch (...) { CloseSensorQueue(queue); throw; }
        next_sensor_queue_ = handle.Add(4);
        return handle.Value();
    }
    const auto key = id == 89 ? call.Pointer<void>(1).Address() : call.Pointer<void>(0).Address();
    const auto found = sensor_queues_.find(key.Value());
    if (found == sensor_queues_.end()) SensorFailure("ASensorEventQueue is stale or unregistered", thread);
    if (id == 89) {
        CloseSensorQueue(found->second);
        sensor_queues_.erase(found);
        return 0;
    }
    if (id >= 90 && id <= 92) SensorFailure("ASensor is invalid in the empty device directory", thread);
    if (id == 93) return std::bit_cast<std::uint32_t>(std::int32_t{-1}); // API19 revents != POLLIN quirk.
    if (id == 94) return 0; // BitTube maps EAGAIN to zero events.
    throw std::logic_error("unknown sensor export");
}
} // namespace ogplay::runtime

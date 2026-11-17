#include "runtime/boundary/modules/android/android_module.h"

#include <algorithm>
#include <bit>
#include <stdexcept>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"

namespace ogplay::runtime {
namespace {
constexpr memory::GuestAddress kSensorManager{0x6e008000U};
constexpr std::int32_t kBadValue = -22;
constexpr std::size_t kEventBatch = 256;
}  // namespace

AndroidModule::~AndroidModule() {
    ShutdownLoopers();
}

void AndroidModule::SetSensorDirectory(std::shared_ptr<const AndroidSensorDirectory> directory,
                                       std::shared_ptr<AndroidSensorSource> source) {
    if (!directory || (directory->Count() && !source))
        throw std::invalid_argument("sensor configuration is incomplete");
    std::scoped_lock lock(mutex_);
    if (sensor_list_ || !sensor_queues_.empty()) throw std::logic_error("cannot replace a live sensor directory");
    sensor_directory_ = std::move(directory);
    sensor_source_ = std::move(source);
}

[[noreturn]] void AndroidModule::SensorFailure(std::string_view message, std::uint64_t thread) {
    if (logger_)
        logger_->Write(core::LogLevel::error, "runtime.android.sensor", message, {.guest_thread = thread}, {},
                       {.mode = core::RateLimitMode::none});
    throw std::invalid_argument(std::string(message));
}

void AndroidModule::BuildSensorList(std::uint64_t thread) {
    if (sensor_list_) return;
    const auto sensors = sensor_directory_->Sensors();
    auto size = sensors.size() * sizeof(std::uint32_t);
    for (const auto& sensor : sensors)
        size += sensor.name.size() + sensor.vendor.size() + 2;
    const auto page = services_.address_space.PageSize();
    auto buffer = std::unique_ptr<GuestBuffer>(
        new GuestBuffer(services_.address_space, std::max<std::uint64_t>(page, (size + page - 1) / page * page)));
    const auto start = buffer->range.Start();
    auto strings_at = start.Add(sensors.size() * sizeof(std::uint32_t));
    std::vector<std::array<memory::GuestAddress, 2>> strings;
    for (std::size_t i = 0; i < sensors.size(); ++i) {
        const auto entry = start.Add(i * sizeof(std::uint32_t));
        services_.Write32(entry.Value(), entry.Value(), thread);
        std::array<memory::GuestAddress, 2> addresses;
        for (auto field = 0U; field < addresses.size(); ++field) {
            const auto& value = field == 0 ? sensors[i].name : sensors[i].vendor;
            addresses[field] = strings_at;
            services_.address_space.Write(strings_at, std::as_bytes(std::span(value.c_str(), value.size() + 1)),
                                          thread);
            strings_at = strings_at.Add(value.size() + 1);
        }
        strings.push_back(addresses);
    }
    services_.address_space.Protect(buffer->range, memory::PageProtection::read);
    sensor_strings_ = std::move(strings);
    sensor_list_ = std::move(buffer);
}

std::uint32_t AndroidModule::OpenSensorQueueLocked(std::uint64_t thread, memory::GuestAddress looper, int ident,
                                                   memory::GuestAddress callback, memory::GuestAddress data,
                                                   std::function<void()> on_readable) {
    if (looper.IsNull() || !loopers_.contains(looper.Value()) || RequireLooper(looper).retired)
        SensorFailure("sensor queue requires a live ALooper", thread);
    if (looper_hooks_.thread_running && !looper_hooks_.thread_running(thread))
        SensorFailure("sensor queue owner thread is retired", thread);
    if (!looper_hooks_.create_pipe || !looper_hooks_.close_fd || (sensor_source_ && !looper_hooks_.signal_pipe))
        SensorFailure("sensor queue VFS pipe backend is unavailable", thread);
    if (sensor_queues_.size() >= 1024 || next_sensor_queue_.Value() >= 0x6ea00000U)
        SensorFailure("sensor queue handle budget exhausted", thread);
    if (RequireLooper(looper).references == UINT32_MAX) SensorFailure("sensor queue Looper reference overflow", thread);

    auto stream = sensor_source_ ? sensor_source_->OpenStream() : nullptr;
    const auto pipe = looper_hooks_.create_pipe();
    ++RequireLooper(looper).references;
    SensorQueue queue{thread, looper, pipe, std::move(stream)};
    const auto handle = next_sensor_queue_;
    try {
        if (AddLooperFdLocked(looper, pipe[0], ident, 1, callback, data, std::move(on_readable)) < 0) {
            // API 19 retains the queue even when addFd rejects the registration.
            if (ledger_) ledger_->RecordUnimplemented("runtime.android_boundary_hle", 0);
            if (logger_)
                logger_->Write(core::LogLevel::warn, "runtime.android.sensor",
                               "sensor queue Looper registration rejected", {.guest_thread = thread});
        }
        sensor_queues_.emplace(handle.Value(), std::move(queue));
    } catch (...) {
        CloseSensorQueueLocked(queue);
        throw;
    }
    next_sensor_queue_ = handle.Add(4);
    return handle.Value();
}

std::uint32_t AndroidModule::OpenSensorQueue(std::uint64_t thread, std::function<void()> on_readable) {
    const auto looper = PrepareThreadLooper(thread);
    std::scoped_lock lock(mutex_);
    return OpenSensorQueueLocked(thread, looper, 0, memory::GuestAddress{}, memory::GuestAddress{},
                                 std::move(on_readable));
}

void AndroidModule::CloseSensorQueueLocked(const SensorQueue& queue) {
    if (const auto looper = loopers_.find(queue.looper.Value()); looper != loopers_.end()) {
        static_cast<void>(RemoveLooperFdLocked(queue.looper, queue.pipe[0]));
        if (--looper->second.references == 0) loopers_.erase(looper);
    }
    looper_hooks_.close_fd(queue.pipe[0]);
    looper_hooks_.close_fd(queue.pipe[1]);
}

void AndroidModule::CloseSensorQueue(std::uint32_t key) {
    std::scoped_lock lock(mutex_);
    if (const auto found = sensor_queues_.find(key); found != sensor_queues_.end()) {
        CloseSensorQueueLocked(found->second);
        sensor_queues_.erase(found);
    }
}

void AndroidModule::RetireSensorQueues(std::optional<std::uint64_t> thread) {
    for (auto it = sensor_queues_.begin(); it != sensor_queues_.end();) {
        if (!thread || it->second.owner == *thread || RequireLooper(it->second.looper).owner == *thread) {
            CloseSensorQueueLocked(it->second);
            it = sensor_queues_.erase(it);
        } else {
            ++it;
        }
    }
}

void AndroidModule::UpdateSensorQueueSignal(SensorQueue& queue) {
    const bool available = queue.stream && queue.stream->HasEvents();
    if (queue.signaled == available) return;
    looper_hooks_.signal_pipe(queue.pipe[available ? 1 : 0], available);
    queue.signaled = available;
    if (available) ready_.notify_all();
}

AndroidModule::SensorQueue& AndroidModule::RequireSensorQueue(std::uint32_t key, std::uint64_t thread) {
    const auto found = sensor_queues_.find(key);
    if (found == sensor_queues_.end()) SensorFailure("ASensorEventQueue is stale or unregistered", thread);
    return found->second;
}

bool AndroidModule::EnableSensor(std::uint32_t key, std::int32_t handle, std::int32_t period_us) {
    std::scoped_lock lock(mutex_);
    const auto found = sensor_queues_.find(key);
    if (found == sensor_queues_.end()) return false;
    auto& queue = found->second;
    const auto success = queue.stream && queue.stream->Enable(handle, period_us);
    UpdateSensorQueueSignal(queue);
    return success;
}

bool AndroidModule::DisableSensor(std::uint32_t key, std::int32_t handle) {
    std::scoped_lock lock(mutex_);
    const auto found = sensor_queues_.find(key);
    if (found == sensor_queues_.end()) return false;
    auto& queue = found->second;
    const auto success = queue.stream && queue.stream->Disable(handle);
    UpdateSensorQueueSignal(queue);
    return success;
}

bool AndroidModule::FlushSensors(std::uint32_t key) {
    std::scoped_lock lock(mutex_);
    const auto found = sensor_queues_.find(key);
    if (found == sensor_queues_.end()) return false;
    auto& queue = found->second;
    const auto success = queue.stream && queue.stream->Flush();
    UpdateSensorQueueSignal(queue);
    return success;
}

std::size_t AndroidModule::ReadSensors(std::uint32_t key, std::span<AndroidSensorSample> events) {
    std::scoped_lock lock(mutex_);
    const auto found = sensor_queues_.find(key);
    if (found == sensor_queues_.end()) return 0;
    auto& queue = found->second;
    const auto count = queue.stream ? queue.stream->Read(events) : 0;
    UpdateSensorQueueSignal(queue);
    return count;
}

BoundaryResult AndroidModule::SensorExport(const A32CallFrame& call, std::uint16_t id) {
    std::scoped_lock lock(mutex_);
    SweepLoopers();
    const auto thread = call.ThreadId();
    const auto args = call.RegisterArguments();
    if (loopers_shutdown_) SensorFailure("sensor process is shut down", thread);
    if (id == 85) return kSensorManager.Value();
    if (id <= 89 && call.Pointer<void>(0).Address() != kSensorManager)
        SensorFailure("ASensorManager is invalid", thread);
    if (id == 86 || id == 87 || id >= 95) BuildSensorList(thread);
    const auto sensors = sensor_directory_->Sensors();
    const auto list = sensor_list_ ? sensor_list_->range.Start() : memory::GuestAddress{};
    const auto sensor_at = [list](std::size_t i) { return list.Add(i * sizeof(std::uint32_t)); };
    const auto sensor_index = [&](memory::GuestAddress token) {
        for (std::size_t i = 0; i < sensors.size(); ++i)
            if (sensor_at(i) == token) return i;
        SensorFailure("ASensor is invalid", thread);
    };
    if (id == 86) {
        if (!call.Pointer<void>(1).IsNull()) services_.Write32(args[1], list.Value(), thread);
        return static_cast<std::uint32_t>(sensors.size());
    }
    if (id == 87) {
        const auto found = std::ranges::find(sensors, std::bit_cast<std::int32_t>(args[1]), &AndroidSensor::type);
        return found == sensors.end() ? 0 : sensor_at(static_cast<std::size_t>(found - sensors.begin())).Value();
    }
    if (id == 88)
        return OpenSensorQueueLocked(thread, call.Pointer<void>(1).Address(), std::bit_cast<int>(args[2]),
                                     call.Pointer<void>(3).Address(), call.Pointer<void>(4).Address());
    if (id >= 95) {
        const auto index = sensor_index(call.Pointer<void>(0).Address());
        const auto& sensor = sensors[index];
        switch (id) {
        case 95: return sensor_strings_[index][0].Value();
        case 96: return sensor_strings_[index][1].Value();
        case 97: return static_cast<std::uint32_t>(sensor.type);
        case 98: return std::bit_cast<std::uint32_t>(sensor.resolution);
        case 99: return static_cast<std::uint32_t>(sensor.min_delay_us);
        }
    }

    const auto key = args[id == 89 ? 1 : 0];
    auto& queue = RequireSensorQueue(key, thread);
    if (id == 89) {
        CloseSensorQueueLocked(queue);
        sensor_queues_.erase(key);
        return 0;
    }
    if (id >= 90 && id <= 92) {
        const auto& sensor = sensors[sensor_index(call.Pointer<void>(1).Address())];
        bool ok = false;
        if (id == 90) ok = queue.stream->Enable(sensor.handle, 0);
        if (id == 91) ok = queue.stream->Disable(sensor.handle);
        if (id == 92) ok = queue.stream->SetPeriod(sensor.handle, std::bit_cast<int>(args[2]));
        UpdateSensorQueueSignal(queue);
        return ok ? 0 : std::bit_cast<std::uint32_t>(kBadValue);
    }
    if (id == 93) {
        UpdateSensorQueueSignal(queue);
        return queue.signaled ? 1 : UINT32_MAX;
    }
    if (id == 94) {
        const auto capacity = std::min<std::size_t>(args[2], kEventBatch);
        if (!queue.stream || capacity == 0) return 0;
        const auto output = call.Pointer<AndroidSensorSample>(1).Address();
        services_.address_space.Validate({output, capacity * sizeof(AndroidSensorSample)}, memory::AccessType::write,
                                         thread);
        std::array<AndroidSensorSample, kEventBatch> events;
        const auto count = queue.stream->Read(std::span(events).first(capacity));
        services_.address_space.Write(output, std::as_bytes(std::span(events).first(count)), thread);
        UpdateSensorQueueSignal(queue);
        return static_cast<std::uint32_t>(count);
    }
    throw std::logic_error("unknown sensor export");
}
}  // namespace ogplay::runtime

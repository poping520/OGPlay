#include "ogplay/runtime/common/guest_address_layout.h"
#include "runtime/boundary/modules/android/android_module.h"

#include <algorithm>
#include <bit>
#include <stdexcept>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"

namespace ogplay::runtime {
namespace {
constexpr memory::GuestAddress kSensorManager{guest_layout::kSensorManagerHandle};
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
    if (sensor_queues_.size() >= 1024 || next_sensor_queue_.Value() >= guest_layout::kAssetDirectoryHandleBegin)
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

void AndroidModule::CheckSensorProcessLocked(std::uint64_t thread) {
    SweepLoopers();
    if (loopers_shutdown_) SensorFailure("sensor process is shut down", thread);
}

void AndroidModule::RequireSensorManager(const A32CallFrame& call) {
    if (call.Pointer<void>(0).Address() != kSensorManager) SensorFailure("ASensorManager is invalid", call.ThreadId());
}

memory::GuestAddress AndroidModule::SensorTokenLocked(std::size_t index) const {
    const auto list = sensor_list_ ? sensor_list_->range.Start() : memory::GuestAddress{};
    return list.Add(index * sizeof(std::uint32_t));
}

std::size_t AndroidModule::SensorIndexLocked(memory::GuestAddress token, std::uint64_t thread) {
    const auto count = sensor_directory_->Sensors().size();
    for (std::size_t i = 0; i < count; ++i)
        if (SensorTokenLocked(i) == token) return i;
    SensorFailure("ASensor is invalid", thread);
}

BoundaryResult AndroidModule::ASensorManager_getInstance(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    CheckSensorProcessLocked(call.ThreadId());
    return kSensorManager.Value();
}

BoundaryResult AndroidModule::ASensorManager_getSensorList(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto thread = call.ThreadId();
    CheckSensorProcessLocked(thread);
    RequireSensorManager(call);
    BuildSensorList(thread);
    const auto output = call.Pointer<void>(1);
    if (!output.IsNull()) services_.Write32(output.Address().Value(), SensorTokenLocked(0).Value(), thread);
    return static_cast<std::uint32_t>(sensor_directory_->Sensors().size());
}

BoundaryResult AndroidModule::ASensorManager_getDefaultSensor(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto thread = call.ThreadId();
    CheckSensorProcessLocked(thread);
    RequireSensorManager(call);
    BuildSensorList(thread);
    const auto sensors = sensor_directory_->Sensors();
    const auto type = std::bit_cast<std::int32_t>(call.RegisterArguments()[1]);
    const auto found = std::ranges::find(sensors, type, &AndroidSensor::type);
    return found == sensors.end() ? 0 : SensorTokenLocked(static_cast<std::size_t>(found - sensors.begin())).Value();
}

BoundaryResult AndroidModule::ASensorManager_createEventQueue(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto thread = call.ThreadId();
    CheckSensorProcessLocked(thread);
    RequireSensorManager(call);
    return OpenSensorQueueLocked(thread, call.Pointer<void>(1).Address(),
                                 std::bit_cast<int>(call.RegisterArguments()[2]), call.Pointer<void>(3).Address(),
                                 call.Pointer<void>(4).Address());
}

BoundaryResult AndroidModule::ASensorManager_destroyEventQueue(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto thread = call.ThreadId();
    CheckSensorProcessLocked(thread);
    RequireSensorManager(call);
    const auto key = call.RegisterArguments()[1];
    auto& queue = RequireSensorQueue(key, thread);
    CloseSensorQueueLocked(queue);
    sensor_queues_.erase(key);
    return 0;
}

#define OGPLAY_SENSOR_GETTER(name, value)                                                                              \
    BoundaryResult AndroidModule::name(const A32CallFrame& call) {                                                     \
        std::scoped_lock lock(mutex_);                                                                                 \
        const auto thread = call.ThreadId();                                                                           \
        CheckSensorProcessLocked(thread);                                                                              \
        BuildSensorList(thread);                                                                                       \
        const auto index = SensorIndexLocked(call.Pointer<void>(0).Address(), thread);                                 \
        return value;                                                                                                  \
    }

OGPLAY_SENSOR_GETTER(ASensor_getName, sensor_strings_[index][0].Value())
OGPLAY_SENSOR_GETTER(ASensor_getVendor, sensor_strings_[index][1].Value())
OGPLAY_SENSOR_GETTER(ASensor_getType, static_cast<std::uint32_t>(sensor_directory_->Sensors()[index].type))
OGPLAY_SENSOR_GETTER(ASensor_getResolution,
                     std::bit_cast<std::uint32_t>(sensor_directory_->Sensors()[index].resolution))
OGPLAY_SENSOR_GETTER(ASensor_getMinDelay, static_cast<std::uint32_t>(sensor_directory_->Sensors()[index].min_delay_us))

#undef OGPLAY_SENSOR_GETTER

BoundaryResult AndroidModule::ASensorEventQueue_enableSensor(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto thread = call.ThreadId();
    CheckSensorProcessLocked(thread);
    auto& queue = RequireSensorQueue(call.RegisterArguments()[0], thread);
    const auto index = SensorIndexLocked(call.Pointer<void>(1).Address(), thread);
    const auto& sensor = sensor_directory_->Sensors()[index];
    const auto ok = queue.stream->Enable(sensor.handle, 0);
    UpdateSensorQueueSignal(queue);
    return ok ? 0 : std::bit_cast<std::uint32_t>(kBadValue);
}

BoundaryResult AndroidModule::ASensorEventQueue_disableSensor(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto thread = call.ThreadId();
    CheckSensorProcessLocked(thread);
    auto& queue = RequireSensorQueue(call.RegisterArguments()[0], thread);
    const auto index = SensorIndexLocked(call.Pointer<void>(1).Address(), thread);
    const auto& sensor = sensor_directory_->Sensors()[index];
    const auto ok = queue.stream->Disable(sensor.handle);
    UpdateSensorQueueSignal(queue);
    return ok ? 0 : std::bit_cast<std::uint32_t>(kBadValue);
}

BoundaryResult AndroidModule::ASensorEventQueue_setEventRate(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto thread = call.ThreadId();
    CheckSensorProcessLocked(thread);
    const auto args = call.RegisterArguments();
    auto& queue = RequireSensorQueue(args[0], thread);
    const auto index = SensorIndexLocked(call.Pointer<void>(1).Address(), thread);
    const auto& sensor = sensor_directory_->Sensors()[index];
    const auto ok = queue.stream->SetPeriod(sensor.handle, std::bit_cast<int>(args[2]));
    UpdateSensorQueueSignal(queue);
    return ok ? 0 : std::bit_cast<std::uint32_t>(kBadValue);
}

BoundaryResult AndroidModule::ASensorEventQueue_hasEvents(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto thread = call.ThreadId();
    CheckSensorProcessLocked(thread);
    auto& queue = RequireSensorQueue(call.RegisterArguments()[0], thread);
    UpdateSensorQueueSignal(queue);
    return queue.signaled ? 1 : UINT32_MAX;
}

BoundaryResult AndroidModule::ASensorEventQueue_getEvents(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto thread = call.ThreadId();
    CheckSensorProcessLocked(thread);
    const auto args = call.RegisterArguments();
    auto& queue = RequireSensorQueue(args[0], thread);
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

}  // namespace ogplay::runtime

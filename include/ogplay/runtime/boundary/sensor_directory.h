#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ogplay::runtime {

struct AndroidSensor final {
    std::string name, vendor;
    std::int32_t version{}, handle{}, type{};
    float max_range{}, resolution{}, power{};
    std::int32_t min_delay_us{}, fifo_reserved_count{}, fifo_max_count{};
};

class AndroidSensorDirectory final {
public:
    explicit AndroidSensorDirectory(std::vector<AndroidSensor> sensors = {}) : sensors_(std::move(sensors)) {}

    [[nodiscard]] std::span<const AndroidSensor> Sensors() const { return sensors_; }

    [[nodiscard]] std::size_t Count() const { return sensors_.size(); }

private:
    const std::vector<AndroidSensor> sensors_;
};

// Native KitKat sensors_event_t layout. Accuracy occupies vector.status.
struct AndroidSensorSample final {
    std::int32_t version{104}, sensor{}, type{}, reserved{};
    std::int64_t timestamp{};
    std::array<float, 16> values{};
    std::array<std::int32_t, 4> padding{};
};

static_assert(sizeof(AndroidSensorSample) == 104);

class AndroidSensorStream {
public:
    virtual ~AndroidSensorStream() = default;
    virtual bool Enable(std::int32_t handle, std::int32_t period_us) = 0;
    virtual bool Disable(std::int32_t handle) = 0;
    virtual bool SetPeriod(std::int32_t handle, std::int32_t period_us) = 0;
    virtual bool Flush() = 0;
    [[nodiscard]] virtual bool HasEvents() = 0;
    virtual std::size_t Read(std::span<AndroidSensorSample> events) = 0;
};

// Each client gets an independent queue. A source publishes timestamps in the
// Android elapsedRealtimeNanos domain. Pollers pull samples on Clock progress.
class AndroidSensorSource {
public:
    virtual ~AndroidSensorSource() = default;
    virtual std::unique_ptr<AndroidSensorStream> OpenStream() = 0;
};

class AndroidSensorQueues {
public:
    virtual ~AndroidSensorQueues() = default;
    virtual std::uint32_t OpenSensorQueue(std::uint64_t thread, std::function<void()> on_readable) = 0;
    virtual void CloseSensorQueue(std::uint32_t queue) = 0;
    virtual bool EnableSensor(std::uint32_t queue, std::int32_t handle, std::int32_t period_us) = 0;
    virtual bool DisableSensor(std::uint32_t queue, std::int32_t handle) = 0;
    virtual bool FlushSensors(std::uint32_t queue) = 0;
    virtual std::size_t ReadSensors(std::uint32_t queue, std::span<AndroidSensorSample> events) = 0;
};

[[nodiscard]] std::shared_ptr<const AndroidSensorDirectory> StaticAndroidSensorDirectory();
[[nodiscard]] std::shared_ptr<AndroidSensorSource>
MakeStaticAndroidSensorSource(std::shared_ptr<const AndroidSensorDirectory> directory,
                              std::function<std::int64_t()> elapsed_nanos);

}  // namespace ogplay::runtime

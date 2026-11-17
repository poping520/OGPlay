#include "ogplay/runtime/boundary/sensor_directory.h"

#include <algorithm>
#include <bit>
#include <deque>
#include <map>

namespace ogplay::runtime {
namespace {
AndroidSensorSample StaticSample(const AndroidSensor& sensor, std::int64_t timestamp) {
    AndroidSensorSample event{.sensor = sensor.handle, .type = sensor.type, .timestamp = timestamp};
    // Screen up and magnetic north along +Y, with a downward field inclination.
    switch (sensor.type) {
    case 1:
    case 9: event.values[2] = 9.80665F; break;
    case 2:
        event.values[1] = 24.0F;
        event.values[2] = -43.0F;
        break;
    case 11:
        event.values[3] = 1.0F;
        event.values[4] = -1.0F;
        break;
    default: break;
    }
    if (sensor.type == 1 || sensor.type == 2 || sensor.type == 3 || sensor.type == 4)
        event.values[3] = std::bit_cast<float>(std::uint32_t{3});  // SENSOR_STATUS_ACCURACY_HIGH
    return event;
}

class StaticSensorStream final : public AndroidSensorStream {
public:
    StaticSensorStream(std::shared_ptr<const AndroidSensorDirectory> directory, std::function<std::int64_t()> clock)
        : directory_(std::move(directory)), clock_(std::move(clock)) {}

    bool Enable(std::int32_t handle, std::int32_t period_us) override {
        const auto sensors = directory_->Sensors();
        const auto sensor = std::ranges::find(sensors, handle, &AndroidSensor::handle);
        if (sensor == sensors.end() || period_us < 0) return false;
        if (active_.contains(handle)) return SetPeriod(handle, period_us);
        active_.emplace(handle, Subscription{&*sensor, clock_(), Period(*sensor, period_us)});
        return true;
    }

    bool Disable(std::int32_t handle) override {
        std::erase_if(pending_, [handle](const auto& event) {
            return event.sensor == handle ||
                   (event.type == 0 && std::bit_cast<std::int32_t>(event.values[1]) == handle);
        });
        return active_.erase(handle) != 0;
    }

    bool SetPeriod(std::int32_t handle, std::int32_t period_us) override {
        const auto found = active_.find(handle);
        if (found == active_.end() || period_us < 0) return false;
        auto& entry = found->second;
        if (entry.emitted) entry.next = entry.next - entry.period + Period(*entry.sensor, period_us);
        entry.period = Period(*entry.sensor, period_us);
        return true;
    }

    bool Flush() override {
        Collect();
        for (const auto& [handle, _] : active_) {
            AndroidSensorSample event;
            event.values[0] = std::bit_cast<float>(std::int32_t{1});  // META_DATA_FLUSH_COMPLETE
            event.values[1] = std::bit_cast<float>(handle);
            pending_.push_back(event);
        }
        return true;
    }

    bool HasEvents() override {
        Collect();
        return !pending_.empty();
    }

    std::size_t Read(std::span<AndroidSensorSample> events) override {
        Collect();
        const auto count = std::min(events.size(), pending_.size());
        for (auto& event : events.first(count)) {
            event = pending_.front();
            pending_.pop_front();
        }
        return count;
    }

private:
    struct Subscription {
        const AndroidSensor* sensor;
        std::int64_t next, period;
        bool emitted{};
    };

    static std::int64_t Period(const AndroidSensor& sensor, std::int32_t period_us) {
        return static_cast<std::int64_t>(std::max({1, sensor.min_delay_us, period_us})) * 1'000;
    }

    void Collect() {
        constexpr std::int64_t kBacklog = 256;
        const auto now = clock_();
        for (auto& [_, entry] : active_) {
            // Drop old samples after a long idle period, preserving phase.
            if (entry.emitted && now > entry.next && (now - entry.next) / entry.period >= kBacklog)
                entry.next += ((now - entry.next) / entry.period - (kBacklog - 1)) * entry.period;
        }
        while (pending_.size() < kBacklog && !active_.empty()) {
            auto earliest = std::ranges::min_element(active_, {}, [](const auto& pair) { return pair.second.next; });
            auto& entry = earliest->second;
            const auto timestamp = std::max(entry.next, last_timestamp_ + 1);
            if (timestamp > now) break;
            pending_.push_back(StaticSample(*entry.sensor, timestamp));
            last_timestamp_ = timestamp;
            entry.emitted = true;
            entry.next += entry.period;
        }
    }

    std::shared_ptr<const AndroidSensorDirectory> directory_;
    std::function<std::int64_t()> clock_;
    std::map<std::int32_t, Subscription> active_;
    std::deque<AndroidSensorSample> pending_;
    std::int64_t last_timestamp_{-1};
};

class StaticSensorSource final : public AndroidSensorSource {
public:
    StaticSensorSource(std::shared_ptr<const AndroidSensorDirectory> directory, std::function<std::int64_t()> clock)
        : directory_(std::move(directory)), clock_(std::move(clock)) {}

    std::unique_ptr<AndroidSensorStream> OpenStream() override {
        return std::make_unique<StaticSensorStream>(directory_, clock_);
    }

private:
    std::shared_ptr<const AndroidSensorDirectory> directory_;
    std::function<std::int64_t()> clock_;
};
}  // namespace

std::shared_ptr<const AndroidSensorDirectory> StaticAndroidSensorDirectory() {
    static const auto directory = std::make_shared<const AndroidSensorDirectory>(std::vector<AndroidSensor>{
        {"OGPlay Accelerometer", "OGPlay", 1, 1, 1, 39.2266F, 0.0012F, 0.20F, 5'000},
        {"OGPlay Magnetic Field", "OGPlay", 1, 2, 2, 1200.F, 0.15F, 0.50F, 10'000},
        {"OGPlay Orientation", "OGPlay", 1, 3, 3, 360.F, 0.01F, 0.70F, 10'000},
        {"OGPlay Gyroscope", "OGPlay", 1, 4, 4, 34.9066F, 0.0011F, 3.60F, 5'000},
        {"OGPlay Gravity", "OGPlay", 1, 9, 9, 39.2266F, 0.0012F, 0.20F, 5'000},
        {"OGPlay Linear Acceleration", "OGPlay", 1, 10, 10, 39.2266F, 0.0012F, 0.20F, 5'000},
        {"OGPlay Rotation Vector", "OGPlay", 1, 11, 11, 1.F, 0.00001F, 4.30F, 5'000},
    });
    return directory;
}

std::shared_ptr<AndroidSensorSource>
MakeStaticAndroidSensorSource(std::shared_ptr<const AndroidSensorDirectory> directory,
                              std::function<std::int64_t()> elapsed_nanos) {
    return std::make_shared<StaticSensorSource>(std::move(directory), std::move(elapsed_nanos));
}
}  // namespace ogplay::runtime

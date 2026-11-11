#pragma once

#include "ogplay/runtime/boundary/android_boundary_hle.h"

#include <array>
#include <deque>
#include <map>
#include <optional>

namespace ogplay::runtime::android_intrinsics {
// API19 default lsq2 strategy: 20 samples, 100ms horizon, 40ms stopped-pointer
// detection and quadratic least squares. No host clock is sampled here.
class VelocityTracker final {
public:
    struct Estimator {
        std::array<float, 5> x{}, y{};
        int degree{};
        float confidence{1};
    };

    void Clear();
    void Add(const AndroidBoundaryInput& motion);
    void Compute(int units, float maximum);
    std::array<float, 2> Velocity(int id) const;
    std::optional<Estimator> Estimate(int id) const;

private:
    struct Point {
        std::int64_t time{};
        float x{}, y{};
    };

    void AddSample(const AndroidInputSample& sample);
    std::map<int, std::deque<Point>> history_;
    std::map<int, std::array<float, 2>> calculated_;
    std::int64_t last_time_{};
    int active_{-1};
};
}  // namespace ogplay::runtime::android_intrinsics

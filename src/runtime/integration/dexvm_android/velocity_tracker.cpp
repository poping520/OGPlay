// Least-squares solver and tracking rules adapted from Android 4.4.4
// frameworks/native/libs/input/VelocityTracker.cpp.
// Copyright (C) 2012 The Android Open Source Project
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.
#include "velocity_tracker.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace ogplay::runtime::android_intrinsics {
namespace {
float Dot(const std::array<float, 20>& a, const std::array<float, 20>& b, std::size_t m) {
    float result = 0;
    for (std::size_t i = 0; i < m; ++i)
        result += a[i] * b[i];
    return result;
}

bool Solve(const std::array<float, 20>& t, const std::array<float, 20>& values, std::size_t count, std::size_t n,
           std::array<float, 5>& coefficients, float& determination) {
    std::array<std::array<float, 20>, 3> a{}, q{};
    std::array<std::array<float, 3>, 3> r{};
    for (std::size_t h = 0; h < count; ++h) {
        a[0][h] = 1;
        for (std::size_t i = 1; i < n; ++i)
            a[i][h] = a[i - 1][h] * t[h];
    }
    for (std::size_t j = 0; j < n; ++j) {
        q[j] = a[j];
        for (std::size_t i = 0; i < j; ++i) {
            const auto dot = Dot(q[j], q[i], count);
            for (std::size_t h = 0; h < count; ++h)
                q[j][h] -= dot * q[i][h];
        }
        const auto norm = std::sqrt(Dot(q[j], q[j], count));
        if (norm < 0.000001f) return false;
        const auto inverse = 1.0f / norm;
        for (std::size_t h = 0; h < count; ++h)
            q[j][h] *= inverse;
        for (std::size_t i = j; i < n; ++i)
            r[j][i] = Dot(q[j], a[i], count);
    }
    for (std::size_t i = n; i-- > 0;) {
        coefficients[i] = Dot(q[i], values, count);
        for (std::size_t j = n - 1; j > i; --j)
            coefficients[i] -= r[i][j] * coefficients[j];
        coefficients[i] /= r[i][i];
    }
    float mean = 0;
    for (std::size_t h = 0; h < count; ++h)
        mean += values[h];
    mean /= static_cast<float>(count);
    float error = 0, total = 0;
    for (std::size_t h = 0; h < count; ++h) {
        float e = values[h] - coefficients[0], term = 1;
        for (std::size_t i = 1; i < n; ++i) {
            term *= t[h];
            e -= term * coefficients[i];
        }
        error += e * e;
        const auto variance = values[h] - mean;
        total += variance * variance;
    }
    determination = total > 0.000001f ? 1.0f - error / total : 1;
    return true;
}
}  // namespace

void VelocityTracker::Clear() {
    history_.clear();
    calculated_.clear();
    active_ = -1;
    last_time_ = 0;
}

void VelocityTracker::AddSample(const AndroidInputSample& sample) {
    const bool common = std::ranges::any_of(sample.pointers, [&](const auto& p) { return history_.contains(p.id); });
    if (common && sample.event_time_ns >= last_time_ + 40'000'000) history_.clear();
    last_time_ = sample.event_time_ns;
    std::set<int> ids;
    for (const auto& p : sample.pointers) {
        if (p.id < 0 || p.id > 31 || !ids.insert(p.id).second) throw std::invalid_argument("invalid motion pointer id");
        auto& h = history_[p.id];
        h.push_back({sample.event_time_ns, p.axes[0], p.axes[1]});
        if (h.size() > 20) h.pop_front();
    }
    std::erase_if(history_, [&](const auto& p) { return !ids.contains(p.first); });
    if (!ids.contains(active_)) active_ = ids.empty() ? -1 : *ids.begin();
}

void VelocityTracker::Add(const AndroidBoundaryInput& m) {
    const auto action = m.action & 255;
    if (action == 0 || action == 9)
        Clear();
    else if (action == 5) {
        const auto index = static_cast<std::size_t>((m.action >> 8) & 255);
        if (index >= m.pointers.size()) throw std::invalid_argument("invalid pointer-down index");
        history_.erase(m.pointers[index].id);
    } else if (action != 2 && action != 7)
        return;
    for (const auto& sample : m.history)
        AddSample(sample);
    AddSample({m.event_time_ns, m.pointers});
}

std::optional<VelocityTracker::Estimator> VelocityTracker::Estimate(int id) const {
    if (id == -1) id = active_;
    const auto found = history_.find(id);
    if (found == history_.end() || found->second.empty()) return std::nullopt;
    const auto& history = found->second;
    const auto time = history.back().time;
    std::array<float, 20> x{}, y{}, t{};
    std::size_t count = 0;
    for (auto i = history.rbegin(); i != history.rend() && count < 20; ++i) {
        const auto age = time - i->time;
        if (age > 100'000'000) break;
        x[count] = i->x;
        y[count] = i->y;
        t[count] = -static_cast<float>(age) * 0.000000001f;
        ++count;
    }
    if (!count) return std::nullopt;
    Estimator e;
    const auto degree = std::min<std::size_t>(2, count - 1);
    float xd{}, yd{};
    if (degree >= 1 && Solve(t, x, count, degree + 1, e.x, xd) && Solve(t, y, count, degree + 1, e.y, yd)) {
        e.degree = static_cast<int>(degree);
        e.confidence = xd * yd;
    } else {
        e.x = {};
        e.y = {};
        e.x[0] = x[0];
        e.y[0] = y[0];
    }
    return e;
}

void VelocityTracker::Compute(int units, float maximum) {
    calculated_.clear();
    for (const auto& [id, _] : history_) {
        const auto e = Estimate(id);
        std::array<float, 2> velocity{};
        if (e && e->degree >= 1) {
            velocity = {e->x[1] * static_cast<float>(units) / 1000, e->y[1] * static_cast<float>(units) / 1000};
            for (auto& v : velocity) {
                if (v > maximum)
                    v = maximum;
                else if (v < -maximum)
                    v = -maximum;
            }
        }
        calculated_.emplace(id, velocity);
    }
}

std::array<float, 2> VelocityTracker::Velocity(int id) const {
    const auto i = calculated_.find(id == -1 ? active_ : id);
    return i == calculated_.end() ? std::array<float, 2>{} : i->second;
}
}  // namespace ogplay::runtime::android_intrinsics

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>

namespace ogplay::runtime {

class GlApiRouting final {
public:
    void Activate() noexcept {
        std::scoped_lock lock(mutex_);
        active_ = true;
    }
    void Deactivate() noexcept {
        std::scoped_lock lock(mutex_);
        active_ = false;
        current_versions_.clear();
    }
    void Bind(const std::uint64_t thread_id,
              const std::uint32_t client_version) {
        std::scoped_lock lock(mutex_);
        current_versions_[thread_id] = client_version;
    }
    void Release(const std::uint64_t thread_id) noexcept {
        std::scoped_lock lock(mutex_);
        current_versions_.erase(thread_id);
    }
    [[nodiscard]] std::optional<std::uint32_t> CurrentVersion(
        const std::uint64_t thread_id) const noexcept {
        std::scoped_lock lock(mutex_);
        if (!active_) return std::nullopt;
        const auto found = current_versions_.find(thread_id);
        return found == current_versions_.end()
                   ? std::optional<std::uint32_t>{0U}
                   : std::optional<std::uint32_t>{found->second};
    }
private:
    mutable std::mutex mutex_;
    bool active_{};
    std::map<std::uint64_t, std::uint32_t> current_versions_;
};

}  // namespace ogplay::runtime

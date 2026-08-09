#pragma once

#include <cstdint>

namespace ogplay::audio {

class JavaSoundPoolState final {
public:
    void Initialize() noexcept {
        if (active_) return;
        active_ = true;
        ++initialization_count_;
    }

    void Destroy() noexcept {
        if (!active_) return;
        active_ = false;
        ++destruction_count_;
    }

    [[nodiscard]] bool Active() const noexcept { return active_; }
    [[nodiscard]] std::uint64_t DestructionCount() const noexcept {
        return destruction_count_;
    }
    [[nodiscard]] std::uint64_t InitializationCount() const noexcept {
        return initialization_count_;
    }

private:
    bool active_{true};
    std::uint64_t initialization_count_{};
    std::uint64_t destruction_count_{};
};

}  // namespace ogplay::audio

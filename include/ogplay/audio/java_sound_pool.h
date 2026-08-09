#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace ogplay::audio {

enum class JavaSoundPoolKind : std::uint8_t { pool, big };

class JavaSoundPoolState final {
public:
    void Initialize() {
        std::scoped_lock lock(mutex_);
        if (active_) return;
        active_ = true;
        ++initialization_count_;
    }

    void Destroy() {
        std::scoped_lock lock(mutex_);
        if (!active_) return;
        active_ = false;
        voices_.clear();
        ++destruction_count_;
    }

    [[nodiscard]] bool TrackVoice(const std::uint32_t resource,
                                  const std::uint32_t instance,
                                  const JavaSoundPoolKind kind) {
        std::scoped_lock lock(mutex_);
        if (!active_) return false;
        const Voice voice{resource, instance, kind};
        if (std::ranges::find(voices_, voice) == voices_.end()) {
            voices_.push_back(voice);
        }
        return true;
    }

    [[nodiscard]] std::size_t StopAll(
        const JavaSoundPoolKind kind,
        const std::optional<std::int32_t> except_resource = std::nullopt) {
        std::scoped_lock lock(mutex_);
        const auto before = voices_.size();
        std::erase_if(voices_, [kind, except_resource](const Voice& voice) {
            return voice.kind == kind &&
                   (!except_resource.has_value() ||
                    static_cast<std::int32_t>(voice.resource) !=
                        *except_resource);
        });
        return before - voices_.size();
    }

    [[nodiscard]] std::size_t StopAllSounds() {
        std::scoped_lock lock(mutex_);
        const auto stopped = voices_.size();
        voices_.clear();
        return stopped;
    }

    [[nodiscard]] bool Active() const {
        std::scoped_lock lock(mutex_);
        return active_;
    }
    [[nodiscard]] std::size_t ActiveVoiceCount() const {
        std::scoped_lock lock(mutex_);
        return voices_.size();
    }
    [[nodiscard]] std::uint64_t DestructionCount() const {
        std::scoped_lock lock(mutex_);
        return destruction_count_;
    }
    [[nodiscard]] std::uint64_t InitializationCount() const {
        std::scoped_lock lock(mutex_);
        return initialization_count_;
    }

private:
    struct Voice final {
        std::uint32_t resource{};
        std::uint32_t instance{};
        JavaSoundPoolKind kind{JavaSoundPoolKind::pool};

        bool operator==(const Voice&) const = default;
    };

    mutable std::mutex mutex_;
    bool active_{true};
    std::uint64_t initialization_count_{};
    std::uint64_t destruction_count_{};
    std::vector<Voice> voices_;
};

}  // namespace ogplay::audio

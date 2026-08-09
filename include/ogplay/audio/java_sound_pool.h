#pragma once

#include <algorithm>
#include <cmath>
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
        loaded_resources_.clear();
        pending_resources_.clear();
        ++destruction_count_;
    }

    [[nodiscard]] bool RequestLoad(const JavaSoundPoolKind kind,
                                   const std::int32_t resource) {
        std::scoped_lock lock(mutex_);
        return RequestLoadLocked(kind, resource);
    }

    [[nodiscard]] bool MarkLoaded(const JavaSoundPoolKind kind,
                                  const std::int32_t resource) {
        std::scoped_lock lock(mutex_);
        if (!active_) return false;
        const Resource key{kind, resource};
        if (std::ranges::find(loaded_resources_, key) ==
            loaded_resources_.end()) {
            loaded_resources_.push_back(key);
        }
        static_cast<void>(std::erase(pending_resources_, key));
        return true;
    }

    [[nodiscard]] bool IsLoaded(const JavaSoundPoolKind kind,
                                const std::int32_t resource) const {
        std::scoped_lock lock(mutex_);
        return active_ &&
               std::ranges::find(loaded_resources_,
                                 Resource{kind, resource}) !=
                   loaded_resources_.end();
    }

    [[nodiscard]] bool Unload(const JavaSoundPoolKind kind,
                              const std::int32_t resource) {
        std::scoped_lock lock(mutex_);
        const Resource key{kind, resource};
        auto changed = std::erase(loaded_resources_, key) != 0U;
        changed = std::erase(pending_resources_, key) != 0U || changed;
        const auto voices_before = voices_.size();
        std::erase_if(voices_, [kind, resource](const Voice& voice) {
            return voice.kind == kind && voice.resource == resource;
        });
        return voices_.size() != voices_before || changed;
    }

    [[nodiscard]] bool Play(const JavaSoundPoolKind kind,
                            const std::int32_t resource,
                            const std::int32_t instance, const float volume) {
        std::scoped_lock lock(mutex_);
        if (!active_ || !std::isfinite(volume) || volume < 0.0F ||
            volume > 1.0F) {
            return false;
        }
        if (std::ranges::find(loaded_resources_, Resource{kind, resource}) ==
            loaded_resources_.end()) {
            static_cast<void>(RequestLoadLocked(kind, resource));
            return false;
        }
        const auto found = std::ranges::find_if(
            voices_, [kind, resource, instance](const Voice& voice) {
                return voice.kind == kind && voice.resource == resource &&
                       voice.instance == instance;
            });
        if (found == voices_.end()) {
            voices_.push_back({resource, instance, kind, volume});
        } else {
            found->volume = volume;
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
    [[nodiscard]] std::size_t LoadedResourceCount() const {
        std::scoped_lock lock(mutex_);
        return loaded_resources_.size();
    }
    [[nodiscard]] std::size_t PendingLoadCount() const {
        std::scoped_lock lock(mutex_);
        return pending_resources_.size();
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
    struct Resource final {
        JavaSoundPoolKind kind{JavaSoundPoolKind::pool};
        std::int32_t resource{};

        bool operator==(const Resource&) const = default;
    };

    struct Voice final {
        std::int32_t resource{};
        std::int32_t instance{};
        JavaSoundPoolKind kind{JavaSoundPoolKind::pool};
        float volume{1.0F};

        bool operator==(const Voice&) const = default;
    };

    [[nodiscard]] bool RequestLoadLocked(const JavaSoundPoolKind kind,
                                         const std::int32_t resource) {
        if (!active_) return false;
        const Resource key{kind, resource};
        if (std::ranges::find(loaded_resources_, key) !=
            loaded_resources_.end()) {
            return true;
        }
        if (std::ranges::find(pending_resources_, key) ==
            pending_resources_.end()) {
            pending_resources_.push_back(key);
        }
        return true;
    }

    mutable std::mutex mutex_;
    bool active_{true};
    std::uint64_t initialization_count_{};
    std::uint64_t destruction_count_{};
    std::vector<Resource> loaded_resources_;
    std::vector<Resource> pending_resources_;
    std::vector<Voice> voices_;
};

}  // namespace ogplay::audio

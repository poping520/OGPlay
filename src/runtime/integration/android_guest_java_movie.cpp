#include "ogplay/runtime/integration/android_guest_call_session.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime {
namespace {

constexpr std::size_t kMaximumMovieNameCodeUnits = 4096;

void ValidateVolume(const float volume) {
    if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F) {
        throw AndroidGuestCallSessionError(
            "Android guest media volume is outside 0..1");
    }
}

[[nodiscard]] std::vector<std::byte> LoadSoundResource(
    const audio::JavaSoundPoolMixer::EncodedResourceLoader& loader,
    const JniInvocation& invocation) {
    const auto resource = std::get<JniInt>(invocation.arguments.front());
    if (!loader || resource < 0) {
        throw AndroidGuestCallSessionError(
            "Android guest numbered sound resource loader is unavailable");
    }
    auto contents = loader(resource);
    if (contents.empty() ||
        contents.size() >
            static_cast<std::size_t>(std::numeric_limits<JniSize>::max())) {
        throw AndroidGuestCallSessionError(
            "Android guest numbered sound resource is missing or oversized");
    }
    return contents;
}

[[nodiscard]] JniValue PublishBytes(
    JniEnvironment& environment, JniPrimitiveArrayStore& arrays,
    const JniInvocation& invocation,
    const std::span<const std::byte> contents) {
    const auto identity = arrays.New(
        JniPrimitiveKind::byte, static_cast<JniSize>(contents.size()));
    try {
        std::vector<JniByte> bytes;
        bytes.reserve(contents.size());
        for (const auto byte : contents) {
            bytes.push_back(static_cast<JniByte>(
                std::to_integer<std::uint8_t>(byte)));
        }
        arrays.SetRegion(identity, 0,
                         JniPrimitiveArrayData{std::move(bytes)});
        return JniValue{environment.PublishLocalObject(
            invocation.thread_id, identity)};
    } catch (...) {
        arrays.Delete(identity);
        throw;
    }
}

}  // namespace

void AndroidGuestMovieState::Request(
    const std::uint64_t thread_id, const std::span<const JniChar> name) {
    if (thread_id == 0 || name.size() > kMaximumMovieNameCodeUnits) {
        throw AndroidGuestCallSessionError(
            "Android guest movie request is invalid");
    }
    std::scoped_lock lock(mutex_);
    ++request_count_;
    latest_ = AndroidGuestMovieRequest{
        request_count_, thread_id, {name.begin(), name.end()}};
}

std::optional<AndroidGuestMovieRequest>
AndroidGuestMovieState::Latest() const {
    std::scoped_lock lock(mutex_);
    return latest_;
}

std::uint64_t AndroidGuestMovieState::RequestCount() const {
    std::scoped_lock lock(mutex_);
    return request_count_;
}

void AndroidGuestLegacyMediaState::Record(const std::string_view method) {
    std::scoped_lock lock(mutex_);
    ++callback_counts_[std::string(method)];
}

void AndroidGuestLegacyMediaState::SetMasterVolume(const float volume) {
    ValidateVolume(volume);
    std::scoped_lock lock(mutex_);
    master_volume_ = volume;
}

void AndroidGuestLegacyMediaState::SetMusicVolume(
    const std::int32_t resource, const float volume) {
    ValidateVolume(volume);
    std::scoped_lock lock(mutex_);
    music_volumes_[resource] = volume;
}

float AndroidGuestLegacyMediaState::MasterVolume() const {
    std::scoped_lock lock(mutex_);
    return master_volume_;
}

float AndroidGuestLegacyMediaState::MusicVolume(
    const std::int32_t resource) const {
    std::scoped_lock lock(mutex_);
    const auto found = music_volumes_.find(resource);
    return found == music_volumes_.end() ? 1.0F : found->second;
}

std::uint64_t AndroidGuestLegacyMediaState::CallbackCount(
    const std::string_view method) const {
    std::scoped_lock lock(mutex_);
    const auto found = callback_counts_.find(method);
    return found == callback_counts_.end() ? 0U : found->second;
}

void BindAndroidGuestJavaMovieHandlers(
    JniInvocationEngine& invocations, JniEnvironment& environment,
    JniStringStore& strings, AndroidGuestMovieState& movie_state) {
    invocations.RegisterHandler(
        "audio.load_movie",
        [&environment, &strings, &movie_state](
            const JniInvocation& invocation) {
            const auto reference =
                std::get<JniReference>(invocation.arguments[0]);
            const auto identity = environment.ResolveObjectForHle(
                invocation.thread_id, reference);
            if (!identity.has_value()) {
                throw AndroidGuestCallSessionError(
                    "Android guest movie name cannot be null");
            }
            const auto length = strings.Length(*identity);
            movie_state.Request(
                invocation.thread_id, strings.Region(*identity, 0, length));
            return JniValue{std::monostate{}};
        });
}

void BindAndroidGuestJavaMediaHandlers(
    JniInvocationEngine& invocations, JniEnvironment& environment,
    JniStringStore& strings, JniPrimitiveArrayStore& arrays,
    AndroidGuestMovieState& movie_state,
    AndroidGuestLegacyMediaState& media_state,
    const audio::JavaSoundPoolMixer::EncodedResourceLoader& resource_loader) {
    invocations.RegisterHandler(
        "audio.load_movie",
        [&environment, &strings, &movie_state, &media_state](
            const JniInvocation& invocation) {
            media_state.Record(invocation.method.declaration.name);
            const auto reference =
                std::get<JniReference>(invocation.arguments[0]);
            const auto identity = environment.ResolveObjectForHle(
                invocation.thread_id, reference);
            if (!identity.has_value()) {
                throw AndroidGuestCallSessionError(
                    "Android guest movie name cannot be null");
            }
            const auto length = strings.Length(*identity);
            const auto name = strings.Region(*identity, 0, length);
            movie_state.Request(invocation.thread_id, name);
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "resource.sound_full",
        [&environment, &arrays, resource_loader](
            const JniInvocation& invocation) {
            const auto contents = LoadSoundResource(resource_loader, invocation);
            return PublishBytes(environment, arrays, invocation, contents);
        });
    invocations.RegisterHandler(
        "resource.sound_length",
        [resource_loader](const JniInvocation& invocation) {
            return JniValue{static_cast<JniInt>(
                LoadSoundResource(resource_loader, invocation).size())};
        });
    invocations.RegisterHandler(
        "audio.java_noop",
        [&media_state](const JniInvocation& invocation) {
            media_state.Record(invocation.method.declaration.name);
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.java_true",
        [&media_state](const JniInvocation& invocation) {
            media_state.Record(invocation.method.declaration.name);
            return JniValue{JniInt{1}};
        });
    invocations.RegisterHandler(
        "audio.java_false",
        [&media_state](const JniInvocation& invocation) {
            media_state.Record(invocation.method.declaration.name);
            return JniValue{JniBoolean{0}};
        });
    invocations.RegisterHandler(
        "audio.java_zero",
        [&media_state](const JniInvocation& invocation) {
            media_state.Record(invocation.method.declaration.name);
            return JniValue{JniInt{0}};
        });
    invocations.RegisterHandler(
        "audio.java_unavailable",
        [&media_state](const JniInvocation& invocation) {
            media_state.Record(invocation.method.declaration.name);
            return JniValue{JniInt{-1}};
        });
    invocations.RegisterHandler(
        "audio.master_volume.get",
        [&media_state](const JniInvocation& invocation) {
            media_state.Record(invocation.method.declaration.name);
            return JniValue{JniFloat{media_state.MasterVolume()}};
        });
    invocations.RegisterHandler(
        "audio.master_volume.set",
        [&media_state](const JniInvocation& invocation) {
            media_state.Record(invocation.method.declaration.name);
            media_state.SetMasterVolume(
                std::get<JniFloat>(invocation.arguments[0]));
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.music_volume.get",
        [&media_state](const JniInvocation& invocation) {
            media_state.Record(invocation.method.declaration.name);
            return JniValue{JniFloat{media_state.MusicVolume(
                std::get<JniInt>(invocation.arguments[0]))}};
        });
    invocations.RegisterHandler(
        "audio.music_volume.set_one",
        [&media_state](const JniInvocation& invocation) {
            media_state.Record(invocation.method.declaration.name);
            media_state.SetMusicVolume(
                std::get<JniInt>(invocation.arguments[0]),
                std::get<JniFloat>(invocation.arguments[1]));
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.music_volume.set",
        [&media_state](const JniInvocation& invocation) {
            media_state.Record(invocation.method.declaration.name);
            media_state.SetMusicVolume(
                std::get<JniInt>(invocation.arguments[1]),
                std::get<JniFloat>(invocation.arguments[0]));
            return JniValue{std::monostate{}};
        });
}

}  // namespace ogplay::runtime

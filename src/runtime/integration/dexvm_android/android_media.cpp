#include "rom_window.h"
// DVM-80: API-family translation unit. Physical consolidation only.

#include <ranges>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <thread>

#include "ogplay/audio/encoded_music.h"
#include "ogplay/audio/open_sles_pcm_mixer.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

// ---- migrated from android_media_AudioManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {
class VfsVideoSource final : public video::VideoDataSource {
public:
    explicit VfsVideoSource(std::shared_ptr<const VfsReadLease> lease) : lease_(std::move(lease)) {}

    std::uint64_t Size() const noexcept override { return lease_->Size(); }

    std::size_t ReadAt(const std::uint64_t offset, const std::span<std::byte> destination) const override {
        return lease_->ReadAt(offset, destination);
    }

private:
    std::shared_ptr<const VfsReadLease> lease_;
};
}  // namespace

Decl Declare_android_media_AudioSystem_Natives(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/media/AudioSystem;");
    builder.MergesBootClass();
    builder.StaticMethod(
        "isStreamActive", "(II)Z",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[0].AsInt() != 3 || call.arguments[1].AsInt() != 0) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.audio.stream_activity", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "only current music stream activity is available"};
            }
            if (context->encoded_music != nullptr && context->encoded_music->AnyPlaying()) {
                return dx::VmValue::Int(1);
            }
            if (context->encoded_audio_playback != nullptr &&
                context->encoded_audio_playback->ActiveVoiceCount() > 0U) {
                return dx::VmValue::Int(1);
            }
            if (context->pcm_playback != nullptr) {
                for (const auto& [_, track] : context->audio_tracks) {
                    if (context->pcm_playback->PlayState(track.player) == audio::OpenSlesPlayState::playing) {
                        return dx::VmValue::Int(1);
                    }
                }
            }
            return dx::VmValue::Int(0);
        },
        dx::kAccPublic | dx::kAccStatic | dx::kAccNative);
    return std::move(builder).Build();
}

Decl Declare_local_AudioService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalAudioService;",
                                                    "Landroid/media/IAudioService$Stub;");
    builder.VirtualMethod("getRingerMode", "()I", [](dx::IntrinsicContext&) { return dx::VmValue::Int(2); });
    builder.VirtualMethod("getStreamMaxVolume", "(I)I", [](dx::IntrinsicContext&) { return dx::VmValue::Int(15); });
    builder.VirtualMethod("getStreamVolume", "(I)I", [context](dx::IntrinsicContext& call) {
        const auto stream = call.arguments[0].AsInt();
        if (stream < 0 || stream >= 10) return dx::VmValue::Int(0);
        std::scoped_lock lock(context->audio_policy_mutex);
        if (context->stream_mute[static_cast<std::size_t>(stream)]) {
            return dx::VmValue::Int(0);
        }
        return dx::VmValue::Int(context->stream_volume[static_cast<std::size_t>(stream)]);
    });
    builder.VirtualMethod("setStreamVolume", "(IIILjava/lang/String;)V", [context](dx::IntrinsicContext& call) {
        const auto stream = call.arguments[0].AsInt();
        if (stream < 0 || stream >= 10) return dx::VmValue::Void();
        std::scoped_lock lock(context->audio_policy_mutex);
        context->stream_volume[static_cast<std::size_t>(stream)] = std::clamp(call.arguments[1].AsInt(), 0, 15);
        ApplyAudioStreamPolicy(*context, stream);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setStreamMute", "(IZLandroid/os/IBinder;)V", [context](dx::IntrinsicContext& call) {
        const auto stream = call.arguments[0].AsInt();
        if (stream < 0 || stream >= 10) return dx::VmValue::Void();
        std::scoped_lock lock(context->audio_policy_mutex);
        context->stream_mute[static_cast<std::size_t>(stream)] = call.arguments[1].AsInt() != 0;
        ApplyAudioStreamPolicy(*context, stream);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {
namespace {
class EncodedVideoSource final : public video::VideoDataSource {
public:
    explicit EncodedVideoSource(std::shared_ptr<const audio::EncodedAudioDataSource> source)
        : source_(std::move(source)) {}

    std::uint64_t Size() const noexcept override { return source_->Size(); }

    std::size_t ReadAt(std::uint64_t offset, std::span<std::byte> bytes) const override {
        return source_->ReadAt(offset, bytes);
    }

private:
    std::shared_ptr<const audio::EncodedAudioDataSource> source_;
};

}  // namespace

void ReleaseAndroidMediaVideo(DexVmAndroidContext& context, std::uint32_t owner) {
    std::unique_ptr<video::VideoPlayer> retired;
    {
        std::scoped_lock lock(context.video_views_mutex);
        const auto found = context.video_views.find(owner);
        if (found == context.video_views.end()) return;
        if (context.rom_windows && !found->second.display_window.IsNull())
            context.rom_windows->system.ClearFrame(android_intrinsics::WindowId{found->second.display_window.Value()},
                                                   found->second.latest_frame);
        retired = std::move(found->second.player);
        context.video_views.erase(found);
    }
}

bool PrepareAndroidMediaVideo(DexVmAndroidContext& context, const std::uint32_t owner) {
    auto& media = context.media_players.at(owner);
    if (!media.source_data || !context.video_source_player_factory) return false;
    std::scoped_lock lock(context.video_views_mutex);
    if (!context.video_views.contains(owner) && context.video_views.size() >= 8)
        throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "video instance budget exceeded"};
    if (media.video_generation == std::numeric_limits<std::int32_t>::max())
        throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "video generation budget exceeded"};
    if (!media.display_window.IsNull()) {
        for (const auto& [handle, video] : context.video_views)
            if (handle != owner && video.display_window == media.display_window)
                throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                         "Surface already has a video producer"};
    }
    DexVmAndroidContext::VideoViewState state;
    try {
        state.player = context.video_source_player_factory(std::make_shared<EncodedVideoSource>(media.source_data));
        if (!state.player) throw video::VideoPlayerError("video decoder returned no player");
        video::ValidateVideoMetadata(state.player->Metadata());
    } catch (const video::VideoCodecUnsupportedError&) {
        throw;  // callers record the missing codec per name
    } catch (const video::VideoPlayerError&) {
        return false;
    }
    state.display_window = media.display_window;
    state.generation = ++media.video_generation;
    state.duration_ms = state.player->Metadata().duration_ms;
    state.guest_path = media.source.name;
    state.looping = media.looping;
    state.audio_stream = media.audio_stream;
    state.left_volume = media.left_volume;
    state.right_volume = media.right_volume;
    state.pending_event = 1;
    context.video_views.insert_or_assign(owner, std::move(state));
    // One active decoder owns the source; the inactive music slot is only the
    // stable native-context token and releases its encoded data/prepare task.
    context.encoded_music->Reset(media.music);
    media.phase = DexVmAndroidContext::MediaPlayerState::Phase::prepared;
    return true;
}

void BindAndroidMediaSurface(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context,
                             std::uint32_t owner, dexvm::VmObjectRef surface) {
    auto& media = context->media_players.at(owner);
    memory::GuestAddress target;
    if (surface.IsValid()) {
        const auto window = android_intrinsics::ResolveRomSurfaceWindow(vm, context, surface);
        if (!window && ResolveAndroidSurfaceStream(vm, *context, surface))
            throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                     "MediaPlayer does not produce SurfaceStream buffers"};
        if (!window)
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "video Surface has no live window"};
        target = window->identity;
    }
    std::scoped_lock lock(context->video_views_mutex);
    if (media.display_surface == surface && media.display_window == target) return;
    if (!target.IsNull()) {
        for (const auto& [handle, state] : context->video_views)
            if (handle != owner && state.display_window == target)
                throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                         "Surface already has a video producer"};
    }
    media.display_surface = surface;
    media.display_window = target;
    if (const auto found = context->video_views.find(owner); found != context->video_views.end()) {
        auto& state = found->second;
        if (context->rom_windows && !state.display_window.IsNull())
            context->rom_windows->system.ClearFrame(android_intrinsics::WindowId{state.display_window.Value()},
                                                    state.latest_frame);
        state.display_window = target;
        state.latest_frame.reset();
        state.frame_requested = !target.IsNull();
        if (state.frame_requested && !state.playing) {
            state.player->SeekTo(state.base_position_ms);
            state.pcm_phase = 0;
            state.pcm_carry.clear();
        }
    }
}

void ApplyAudioStreamPolicy(DexVmAndroidContext& context, std::int32_t stream) {
    const auto index = static_cast<std::size_t>(stream);
    const auto gain =
        context.stream_mute.at(index) ? 0.0F : static_cast<float>(context.stream_volume.at(index)) / 15.0F;
    if (context.pcm_playback) context.pcm_playback->SetStreamGain(stream, gain);
    if (context.encoded_audio_playback) context.encoded_audio_playback->SetStreamGain(stream, gain);
    if (context.encoded_music) context.encoded_music->SetStreamGain(stream, gain);
}

void RegisterAndroidAudioTrackStateTable(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context) {
    if (context == nullptr) return;
    vm.RegisterIntrinsicStateTable(
        {"android.media.AudioTrack",
         [context](const dexvm::VmObjectRef owner, const dexvm::VmRootVisitor& visit) {
             if (const auto found = context->audio_tracks.find(owner.Value());
                 found != context->audio_tracks.end() && found->second.jni_weak.IsValid()) {
                 visit(found->second.jni_weak);
             }
             if (const auto found = context->media_players.find(owner.Value()); found != context->media_players.end()) {
                 if (found->second.jni_weak.IsValid()) visit(found->second.jni_weak);
                 if (found->second.display_surface.IsValid()) visit(found->second.display_surface);
             }
             if (const auto found = context->sound_pools.find(owner.Value());
                 found != context->sound_pools.end() && found->second.jni_weak.IsValid()) {
                 visit(found->second.jni_weak);
             }
         },
         [context](const dexvm::VmObjectRef object) {
             const auto found = context->audio_tracks.find(object.Value());
             if (found != context->audio_tracks.end()) {
                 if (context->pcm_playback != nullptr) {
                     context->pcm_playback->DestroyPlayer(found->second.player);
                 }
                 context->audio_tracks.erase(found);
             }
             const auto media = context->media_players.find(object.Value());
             if (media != context->media_players.end()) {
                 ReleaseAndroidMediaVideo(*context, object.Value());
                 if (context->encoded_music != nullptr) {
                     context->encoded_music->Destroy(media->second.music);
                 }
                 if (media->second.source.lease != 0U) {
                     context->encoded_audio_leases.erase(media->second.source.lease);
                 }
                 context->media_players.erase(media);
             }
             const auto pool = context->sound_pools.find(object.Value());
             if (pool != context->sound_pools.end()) {
                 if (context->encoded_audio_playback != nullptr) {
                     for (const auto& source : context->encoded_audio_playback->PoolSources(pool->second.pool)) {
                         if (source.lease != 0U) {
                             context->encoded_audio_leases.erase(source.lease);
                         }
                     }
                     context->encoded_audio_playback->DestroyPool(pool->second.pool);
                 }
                 context->sound_pools.erase(pool);
             }
         },
         {}});
}
}  // namespace ogplay::runtime

#include <algorithm>
#include <stdexcept>

#include "ogplay/audio/pcm_mix.h"

#include "ogplay/video/rgba_canvas.h"

#include "shared.h"

namespace ogplay::runtime {

namespace {

// Mixes one view's audio into the stereo accumulator. Nearest-neighbour
// resampling with an integer phase accumulator: every output frame maps to
// the current source frame; the phase advances by the source rate and
// consumes source frames whenever it crosses the output rate, so batches of
// any size stay drift-free. Fetch includes skipped source frames so
// downsampling does not restart at the next unread sample of this chunk.
bool MixOneVideoView(DexVmAndroidContext::VideoViewState& state, const std::span<std::int64_t> accumulator,
                     const std::uint32_t output_rate, const float stream_gain) {
    const auto& metadata = state.player->Metadata();
    if (!metadata.HasAudio()) return false;
    const auto channels = static_cast<std::size_t>(metadata.audio_channels);
    const auto source_rate = metadata.audio_sample_rate;
    const auto output_frames = accumulator.size() / 2U;

    const auto consumed_index = static_cast<std::size_t>(
        (state.pcm_phase + static_cast<std::uint64_t>(output_frames) * source_rate) / output_rate);
    const bool has_carry = !state.pcm_carry.empty();
    const auto fetch_frames = consumed_index + 1U - (has_carry ? 1U : 0U);
    std::vector<std::int16_t> source((has_carry ? 1U : 0U) * channels);
    if (has_carry) {
        std::copy(state.pcm_carry.begin(), state.pcm_carry.end(), source.begin());
    }
    if (fetch_frames > 0U) {
        std::vector<std::int16_t> fetched(fetch_frames * channels);
        const auto got = state.player->ReadPcm(fetched);
        fetched.resize(got * channels);
        source.insert(source.end(), fetched.begin(), fetched.end());
    }
    const auto available = source.size() / channels;
    if (available == 0U) return false;

    auto phase = state.pcm_phase;
    std::size_t index = 0;
    for (std::size_t frame = 0; frame < output_frames; ++frame) {
        if (index >= available) break;
        const auto* samples = source.data() + index * channels;
        const auto left = samples[0];
        const auto right = channels >= 2U ? samples[1] : samples[0];
        accumulator[frame * 2U] += static_cast<std::int64_t>(std::lround(left * state.left_volume * stream_gain));
        accumulator[frame * 2U + 1U] +=
            static_cast<std::int64_t>(std::lround(right * state.right_volume * stream_gain));
        phase += source_rate;
        while (phase >= output_rate) {
            phase -= output_rate;
            ++index;
        }
    }
    state.pcm_phase = phase;
    if (index < available) {
        state.pcm_carry.assign(source.begin() + static_cast<std::ptrdiff_t>(index * channels),
                               source.begin() + static_cast<std::ptrdiff_t>((index + 1U) * channels));
    } else {
        state.pcm_carry.clear();
    }
    return true;
}

}  // namespace

bool AnyVideoPlaying(const DexVmAndroidContext& context) {
    std::scoped_lock lock(context.video_views_mutex);
    for (const auto& [handle, state] : context.video_views) {
        if (state.player != nullptr && state.playing) return true;
    }
    return false;
}

std::size_t MixVideoPcmIntoAccumulator(DexVmAndroidContext& context, const std::span<std::int64_t> interleaved_stereo,
                                       const std::uint32_t output_rate) {
    if (output_rate == 0U || interleaved_stereo.empty() || interleaved_stereo.size() % 2U != 0U) {
        throw std::invalid_argument("video PCM mix needs a non-empty stereo buffer and a positive "
                                    "rate");
    }
    std::array<float, 10> gains{};
    {
        std::scoped_lock policy(context.audio_policy_mutex);
        for (std::size_t i = 0; i < gains.size(); ++i)
            gains[i] = context.stream_mute[i] ? 0.0F : context.stream_volume[i] / 15.0F;
    }
    std::scoped_lock lock(context.video_views_mutex);
    std::size_t contributed = 0;
    for (auto& [handle, state] : context.video_views) {
        if (state.player == nullptr || !state.playing) continue;
        try {
            if (MixOneVideoView(state, interleaved_stereo, output_rate,
                                gains.at(static_cast<std::size_t>(state.audio_stream))))
                ++contributed;
        } catch (const video::VideoPlayerError&) {
            // Audio workers publish only a fact. The guest main pump owns Java callbacks.
            state.frame_requested = false;
            state.playing = false;
            state.pending_event = 100;
        }
    }
    return contributed;
}

std::optional<std::string> PumpAndroidAudioTracks(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    const auto post = [&](const char* descriptor, const dexvm::VmObjectRef weak, const std::int32_t what,
                          const std::int32_t arg1 = 0, const std::int32_t arg2 = 0) -> std::optional<std::string> {
        if (!weak.IsValid()) return std::nullopt;
        const auto klass = vm.Linker().ResolveDescriptor(descriptor);
        const auto method =
            vm.Linker().FindDirectMethod(klass, "postEventFromNative", "(Ljava/lang/Object;IIILjava/lang/Object;)V");
        if (!method.has_value()) {
            return std::string(descriptor) + " postEventFromNative is missing";
        }
        const auto outcome =
            vm.Call(*method, std::vector<dexvm::VmValue>{dexvm::VmValue::Ref(weak), dexvm::VmValue::Int(what),
                                                         dexvm::VmValue::Int(arg1), dexvm::VmValue::Int(arg2),
                                                         dexvm::VmValue::Ref(dexvm::VmObjectRef{})});
        if (outcome.exception.IsValid()) {
            return "postEventFromNative raised: " + outcome.exception_message;
        }
        return std::nullopt;
    };

    std::vector<std::uint32_t> handles;
    handles.reserve(context.audio_tracks.size());
    for (const auto& [handle, _] : context.audio_tracks) {
        handles.push_back(handle);
    }
    if (context.pcm_playback != nullptr) {
        for (const auto handle : handles) {
            auto found = context.audio_tracks.find(handle);
            if (found == context.audio_tracks.end() || !context.pcm_playback->HasPlayer(found->second.player)) {
                continue;
            }
            auto& state = found->second;
            const auto head = context.pcm_playback->PositionFrames(state.player);
            if (head < state.last_notified_head || head < state.last_observed_head) {
                state.RetirePendingPeriodicCallbacks();
                state.last_notified_head = head;
                state.last_observed_head = head;
                continue;
            }
            if (context.pcm_playback->PlayState(state.player) != audio::OpenSlesPlayState::playing) {
                state.RetirePendingPeriodicCallbacks();
                state.last_notified_head = head;
                state.last_observed_head = head;
                continue;
            }
            const auto period = state.notification_period;
            const auto prior_head = state.last_notified_head;
            const auto prior_observed_head = state.last_observed_head;
            const auto weak = state.jni_weak;
            const bool marker_due = state.marker_position > 0 && !state.marker_fired &&
                                    prior_head < static_cast<std::uint32_t>(state.marker_position) &&
                                    head >= static_cast<std::uint32_t>(state.marker_position);
            const auto periodic_count =
                period > 0 ? head / static_cast<std::uint32_t>(period) - prior_head / static_cast<std::uint32_t>(period)
                           : 0U;
            if (period > 0) {
                state.periodic_callbacks_generated += head / static_cast<std::uint32_t>(period) -
                                                      prior_observed_head / static_cast<std::uint32_t>(period);
            } else {
                state.last_notified_head = head;
            }
            state.last_observed_head = head;
            if (marker_due) state.marker_fired = true;
            if (marker_due) {
                if (const auto error = post("Landroid/media/AudioTrack;", weak, 3); error.has_value()) {
                    return error;
                }
            }
            for (std::uint32_t index = 0; index < periodic_count; ++index) {
                const auto delivered_head =
                    static_cast<std::uint32_t>((prior_head / static_cast<std::uint32_t>(period) + index + 1U) *
                                               static_cast<std::uint32_t>(period));
                const auto expected_previous_head =
                    index == 0U ? prior_head : delivered_head - static_cast<std::uint32_t>(period);
                found = context.audio_tracks.find(handle);
                if (found == context.audio_tracks.end() || found->second.notification_period != period ||
                    found->second.last_notified_head != expected_previous_head) {
                    break;
                }
                // Advance only the threshold represented by this event. If a
                // synchronous refill stops this pass, later lifecycle safe
                // points must still observe every remaining crossed period.
                found->second.last_notified_head = delivered_head;
                if (const auto error = post("Landroid/media/AudioTrack;", weak, 4); error.has_value()) {
                    found->second.last_notified_head = expected_previous_head;
                    return error;
                }
                ++found->second.periodic_callbacks_delivered;
                break;  // Let the ROM Looper deliver the period before observing another one.
            }
        }
    }

    std::vector<std::uint32_t> media_handles;
    media_handles.reserve(context.media_players.size());
    for (const auto& [handle, _] : context.media_players) {
        media_handles.push_back(handle);
    }
    for (const auto handle : media_handles) {
        using Phase = DexVmAndroidContext::MediaPlayerState::Phase;
        const auto found = context.media_players.find(handle);
        if (found == context.media_players.end()) continue;
        bool is_video{};
        {
            std::scoped_lock lock(context.video_views_mutex);
            is_video = context.video_views.contains(handle);
        }
        if (!is_video && context.encoded_music && context.encoded_music->TakeDecodeFailure(found->second.music)) {
            found->second.phase = Phase::error;
            found->second.error_event_pending = true;
        }
        if (found->second.error_event_pending) {
            found->second.error_event_pending = false;
            if (const auto error = post("Landroid/media/MediaPlayer;", found->second.jni_weak, 100, 1, -38);
                error.has_value())
                return error;
            continue;
        }
        if (found->second.phase == Phase::preparing) {
            const auto status = context.encoded_music == nullptr
                                    ? audio::EncodedMusicMixer::PrepareStatus::failed
                                    : context.encoded_music->PollPrepare(found->second.music);
            if (status == audio::EncodedMusicMixer::PrepareStatus::pending) {
                continue;
            }
            found->second.phase =
                status == audio::EncodedMusicMixer::PrepareStatus::ready ? Phase::prepared : Phase::error;
            if (status == audio::EncodedMusicMixer::PrepareStatus::failed || !found->second.display_window.IsNull()) {
                try {
                    if (PrepareAndroidMediaVideo(context, handle)) continue;
                } catch (const video::VideoCodecUnsupportedError& error) {
                    if (auto* ledger = vm.Ledger())
                        ledger->RecordUnimplemented("android.video.codec." + error.codec(), 0);
                    if (auto* logger = vm.Log())
                        logger->Write(core::LogLevel::warn, "runtime.video.prepare", error.what());
                } catch (const dexvm::VmJavaThrow& error) {
                    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.media.video_prepare", 0);
                    if (auto* logger = vm.Log())
                        logger->Write(core::LogLevel::warn, "runtime.video.prepare", error.message);
                }
            }
            if (found->second.phase == Phase::error) {
                if (const auto error = post("Landroid/media/MediaPlayer;", found->second.jni_weak, 100, 1, -1);
                    error.has_value()) {
                    return error;
                }
                continue;
            }
            found->second.prepared_event_pending = true;
        }
        if (found->second.prepared_event_pending) {
            found->second.prepared_event_pending = false;
            if (const auto error = post("Landroid/media/MediaPlayer;", found->second.jni_weak, 1); error.has_value()) {
                return error;
            }
            // Listener may release/reset the player and invalidate this entry.
            continue;
        }
        if (!is_video && context.encoded_music != nullptr && context.encoded_music->Completed(found->second.music)) {
            found->second.phase = Phase::completed;
            if (const auto error = post("Landroid/media/MediaPlayer;", found->second.jni_weak, 2); error.has_value()) {
                return error;
            }
            continue;
        }
        if (found->second.seek_event_pending) {
            found->second.seek_event_pending = false;
            if (const auto error = post("Landroid/media/MediaPlayer;", found->second.jni_weak, 4); error.has_value()) {
                return error;
            }
        }
    }

    std::vector<std::uint32_t> pool_handles;
    pool_handles.reserve(context.sound_pools.size());
    for (const auto& [handle, _] : context.sound_pools) {
        pool_handles.push_back(handle);
    }
    for (const auto handle : pool_handles) {
        const auto found = context.sound_pools.find(handle);
        if (found == context.sound_pools.end()) continue;
        auto pending = std::move(found->second.pending_loads);
        found->second.pending_loads.clear();
        const auto weak = found->second.jni_weak;
        for (const auto& load : pending) {
            if (const auto error = post("Landroid/media/SoundPool$SoundPoolImpl;", weak, 1, load.sound, load.status);
                error.has_value()) {
                return error;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> PumpVideoViews(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    std::vector<std::uint64_t> handles;
    {
        std::scoped_lock lock(context.video_views_mutex);
        for (const auto& [handle, _] : context.video_views)
            handles.push_back(handle);
    }
    for (const auto handle : handles) {
        std::int32_t generation{};
        std::int32_t event{};
        {
            std::scoped_lock lock(context.video_views_mutex);
            const auto found = context.video_views.find(handle);
            if (found == context.video_views.end()) continue;
            generation = found->second.generation;
            event = std::exchange(found->second.pending_event, 0);
        }
        if (event != 0) {
            if (auto error = android_intrinsics::InvokeVideoEvent(vm, context, handle, generation, event)) return error;
        }
        std::shared_ptr<const video::VideoFrame> frame;
        memory::GuestAddress window;
        event = 0;
        {
            std::scoped_lock lock(context.video_views_mutex);
            const auto found = context.video_views.find(handle);
            if (found == context.video_views.end() || found->second.generation != generation) continue;
            auto& state = found->second;
            window = state.display_window;
            if (!state.player || (!state.playing && !state.frame_requested)) continue;
            const auto now = context.uptime_millis.load();
            const auto position = android_intrinsics::VideoPositionOf(state, now);
            try {
                if (state.looping && state.playing) {
                    const auto cycles =
                        (state.base_position_ms + std::max<std::int64_t>(0, now - state.start_uptime_ms)) /
                        state.duration_ms;
                    if (cycles != state.loop_index) {
                        state.player->SeekTo(position);
                        state.loop_index = cycles;
                        state.pcm_phase = 0;
                        state.pcm_carry.clear();
                        state.latest_frame.reset();
                    }
                }
                if (auto decoded = state.player->TakeFrame(position)) {
                    frame = std::make_shared<const video::VideoFrame>(std::move(*decoded));
                    state.latest_frame = frame;
                    state.frame_requested = false;
                }
                if (state.playing && !state.looping && position >= state.duration_ms && !state.completed) {
                    state.playing = false;
                    state.base_position_ms = state.duration_ms;
                    state.completed = true;
                    event = 2;
                }
            } catch (const video::VideoPlayerError& error) {
                if (auto* logger = vm.Log()) logger->Write(core::LogLevel::warn, "runtime.video.decode", error.what());
                state.frame_requested = false;
                state.playing = false;
                state.latest_frame.reset();
                event = 100;
            }
        }
        if (frame && !window.IsNull() && context.rom_windows) {
            const auto id = android_intrinsics::WindowId{window.Value()};
            if (context.rom_windows->system.Exists(id))
                context.rom_windows->system.Post(id, std::move(frame), false, true);
        }
        if (event != 0) {
            if (auto error = android_intrinsics::InvokeVideoEvent(vm, context, handle, generation, event)) return error;
        }
    }
    return std::nullopt;
}

std::vector<AndroidAudioTrackDiagnosticSnapshot> SnapshotAndroidAudioTracks(const DexVmAndroidContext& context) {
    std::vector<AndroidAudioTrackDiagnosticSnapshot> snapshots;
    if (context.pcm_playback == nullptr) return snapshots;
    snapshots.reserve(context.audio_tracks.size());
    for (const auto& [receiver, state] : context.audio_tracks) {
        if (!context.pcm_playback->HasPlayer(state.player)) continue;
        const auto player = context.pcm_playback->Snapshot(state.player);
        snapshots.push_back({
            receiver,
            state.player,
            state.written_frames,
            player.queued_bytes,
            player.consumed_source_frames,
            player.underrun_output_frames,
            player.underrun_count,
            state.periodic_callbacks_generated,
            state.periodic_callbacks_delivered,
            state.PendingPeriodicCallbacks(),
        });
    }
    std::ranges::sort(snapshots, {}, &AndroidAudioTrackDiagnosticSnapshot::receiver);
    return snapshots;
}

}  // namespace ogplay::runtime

namespace ogplay::runtime {
std::optional<std::vector<AndroidAudioTrackDiagnosticSnapshot>>
TrySnapshotAndroidAudioTracks(dexvm::Interpreter& vm, const DexVmAndroidContext& context) {
    auto& lock = vm.ExecutionLock();
    if (!lock.TryAcquire()) return std::nullopt;

    struct Release {
        dexvm::VmExecutionLock& lock;

        ~Release() { lock.Release(); }
    } release{lock};

    if (!context.pcm_playback) return std::nullopt;
    std::vector<AndroidAudioTrackDiagnosticSnapshot> result;
    for (const auto& [receiver, state] : context.audio_tracks) {
        if (result.size() == 129) break;
        const auto player = context.pcm_playback->TrySnapshot(state.player);
        if (!player) return std::nullopt;
        result.push_back({receiver, state.player, state.written_frames, player->queued_bytes,
                          player->consumed_source_frames, player->underrun_output_frames, player->underrun_count,
                          state.periodic_callbacks_generated, state.periodic_callbacks_delivered,
                          state.PendingPeriodicCallbacks()});
    }
    return result;
}

std::optional<AndroidUiSnapshot> TrySnapshotAndroidUi(dexvm::Interpreter& vm, const DexVmAndroidContext& context) {
    auto& lock = vm.ExecutionLock();
    if (!lock.TryAcquire()) return std::nullopt;

    struct Release {
        dexvm::VmExecutionLock& lock;

        ~Release() { lock.Release(); }
    } release{lock};

    AndroidUiSnapshot result;
    if (context.rom_windows) {
        result.windows = context.rom_windows->clients.size();
        const auto focus = context.rom_windows->system.Focus();
        if (focus.value) result.focus = focus.value;
    }
    return result;
}

std::optional<std::vector<AndroidVideoSnapshot>> TrySnapshotAndroidVideo(const DexVmAndroidContext& context) {
    std::unique_lock lock(context.video_views_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return std::nullopt;
    std::vector<AndroidVideoSnapshot> result;
    for (const auto& [receiver, state] : context.video_views) {
        if (result.size() == 128) break;
        result.push_back({receiver, state.duration_ms, state.base_position_ms, state.playing, state.completed,
                          state.player != nullptr});
    }
    return result;
}
}  // namespace ogplay::runtime

namespace ogplay::runtime {
void ReleaseAndroidMediaResources(DexVmAndroidContext& context) {
    // The audio producer is stopped and guest workers are joined by the owner.
    // Detach decoder state under the shared lock, then join decoders outside it.
    decltype(context.video_views) videos;
    {
        std::scoped_lock lock(context.video_views_mutex);
        videos.swap(context.video_views);
    }
    videos.clear();
    for (const auto& [_, state] : context.media_players)
        if (context.encoded_music && state.music) context.encoded_music->Destroy(state.music);
    context.media_players.clear();
    for (const auto& [_, state] : context.sound_pools)
        if (context.encoded_audio_playback) context.encoded_audio_playback->DestroyPool(state.pool);
    context.sound_pools.clear();
    for (const auto& [_, state] : context.audio_tracks)
        if (context.pcm_playback) context.pcm_playback->DestroyPlayer(state.player);
    context.audio_tracks.clear();
    context.encoded_audio_leases.clear();
}
}  // namespace ogplay::runtime

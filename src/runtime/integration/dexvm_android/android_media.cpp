// DVM-80: API-family translation unit. Physical consolidation only.

#include <algorithm>
#include <cmath>
#include <limits>

#include "ogplay/audio/encoded_music.h"
#include "ogplay/audio/open_sles_pcm_mixer.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

// ---- migrated from android_media_AudioManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {
class VfsVideoSource final : public video::VideoDataSource {
public:
    explicit VfsVideoSource(std::shared_ptr<const VfsReadLease> lease)
        : lease_(std::move(lease)) {}
    std::uint64_t Size() const noexcept override { return lease_->Size(); }
    std::size_t ReadAt(const std::uint64_t offset,
                       const std::span<std::byte> destination) const override {
        return lease_->ReadAt(offset, destination);
    }
private:
    std::shared_ptr<const VfsReadLease> lease_;
};
}  // namespace

Decl Declare_android_media_AudioManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/media/AudioManager;", "Ljava/lang/Object;");
    builder.FinalMethod("getRingerMode", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(2);
    });
    builder.FinalMethod("isMusicActive", "()Z",
        [context](dx::IntrinsicContext&) {
            if (context->encoded_music != nullptr &&
                context->encoded_music->AnyPlaying()) {
                return dx::VmValue::Int(1);
            }
            if (context->encoded_audio_playback != nullptr &&
                context->encoded_audio_playback->ActiveVoiceCount() > 0U) {
                return dx::VmValue::Int(1);
            }
            if (context->pcm_playback != nullptr) {
                for (const auto& [_, track] : context->audio_tracks) {
                    if (context->pcm_playback->PlayState(track.player) ==
                        audio::OpenSlesPlayState::playing) {
                        return dx::VmValue::Int(1);
                    }
                }
            }
            return dx::VmValue::Int(0);
        });
    builder.FinalMethod("getStreamMaxVolume", "(I)I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(15); });
    builder.FinalMethod("getStreamVolume", "(I)I",
        [context](dx::IntrinsicContext& call) {
            const auto stream = call.arguments[0].AsInt();
            if (stream < 0 || stream >= 10) return dx::VmValue::Int(0);
            std::scoped_lock lock(context->audio_policy_mutex);
            if (context->stream_mute[static_cast<std::size_t>(stream)]) {
                return dx::VmValue::Int(0);
            }
            return dx::VmValue::Int(
                context->stream_volume[static_cast<std::size_t>(stream)]);
        });
    builder.FinalMethod("setStreamVolume", "(III)V",
        [context](dx::IntrinsicContext& call) {
            const auto stream = call.arguments[0].AsInt();
            if (stream < 0 || stream >= 10) return dx::VmValue::Void();
            std::scoped_lock lock(context->audio_policy_mutex);
            context->stream_volume[static_cast<std::size_t>(stream)] =
                std::clamp(call.arguments[1].AsInt(), 0, 15);
            ApplyAudioStreamPolicy(*context, stream);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setStreamMute", "(IZ)V",
        [context](dx::IntrinsicContext& call) {
            const auto stream = call.arguments[0].AsInt();
            if (stream < 0 || stream >= 10) return dx::VmValue::Void();
            std::scoped_lock lock(context->audio_policy_mutex);
            context->stream_mute[static_cast<std::size_t>(stream)] =
                call.arguments[1].AsInt() != 0;
            ApplyAudioStreamPolicy(*context, stream);
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


namespace ogplay::runtime {
void ApplyAudioStreamPolicy(DexVmAndroidContext& context, std::int32_t stream) {
    const auto index = static_cast<std::size_t>(stream);
    const auto gain = context.stream_mute.at(index) ? 0.0F :
        static_cast<float>(context.stream_volume.at(index)) / 15.0F;
    if (context.pcm_playback) context.pcm_playback->SetStreamGain(stream, gain);
    if (context.encoded_audio_playback) context.encoded_audio_playback->SetStreamGain(stream, gain);
    if (context.encoded_music) context.encoded_music->SetStreamGain(stream, gain);
}
void RegisterAndroidAudioTrackStateTable(
    dexvm::Interpreter& vm,
    const std::shared_ptr<DexVmAndroidContext>& context) {
    if (context == nullptr) return;
    vm.RegisterIntrinsicStateTable({
        "android.media.AudioTrack",
        [context](const dexvm::VmObjectRef owner,
                  const dexvm::VmRootVisitor& visit) {
            if (const auto found = context->audio_tracks.find(owner.Value());
                found != context->audio_tracks.end() &&
                found->second.jni_weak.IsValid()) {
                visit(found->second.jni_weak);
            }
            if (const auto found = context->media_players.find(owner.Value());
                found != context->media_players.end() &&
                found->second.jni_weak.IsValid()) {
                visit(found->second.jni_weak);
            }
            if (const auto found = context->sound_pools.find(owner.Value());
                found != context->sound_pools.end() &&
                found->second.jni_weak.IsValid()) {
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
                if (context->encoded_music != nullptr) {
                    context->encoded_music->Destroy(media->second.music);
                }
                if (media->second.source.lease != 0U) {
                    context->encoded_audio_leases.erase(
                        media->second.source.lease);
                }
                context->media_players.erase(media);
            }
            const auto pool = context->sound_pools.find(object.Value());
            if (pool != context->sound_pools.end()) {
                if (context->encoded_audio_playback != nullptr) {
                    for (const auto& source :
                         context->encoded_audio_playback->PoolSources(
                             pool->second.pool)) {
                        if (source.lease != 0U) {
                            context->encoded_audio_leases.erase(source.lease);
                        }
                    }
                    context->encoded_audio_playback->DestroyPool(
                        pool->second.pool);
                }
                context->sound_pools.erase(pool);
            }
        },
        {}});
}
}  // namespace ogplay::runtime



// ---- migrated from android_widget_VideoView.cpp ----
// VideoView handlers: real decoded playback through the injected
// VideoPlayer factory (ADR-0021). Playback state math and the onCompletion
// invocation are shared with the guest video pump via shared.h.

#include <algorithm>

#include "ogplay/runtime/vfs/vfs.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_VideoView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/VideoView;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.FinalMethod("setVideoPath", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock video_lock(context->video_views_mutex);
            const auto handle = call.receiver.Value();
            context->video_views.erase(handle);
            context->pending_video_completion.erase(handle);
            const auto path_ref = call.arguments[0].ref;
            if (!path_ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "setVideoPath path is null"};
            }
            const auto guest_path = call.vm.StringUtf8(path_ref);
            if (!context->video_source_player_factory) {
                GuestLog(call, core::LogLevel::warn,
                         "VideoView.setVideoPath: no video decoder is "
                         "available; playback of " + guest_path +
                             " will complete immediately (recorded gap)");
                return dx::VmValue::Void();
            }
            if (context->vfs == nullptr) {
                GuestLog(call, core::LogLevel::warn,
                         "VideoView.setVideoPath: no guest filesystem is "
                         "bound; playback of " + guest_path + " will "
                         "complete immediately (recorded gap)");
                return dx::VmValue::Void();
            }
            try {
                DexVmAndroidContext::VideoViewState state;
                const auto descriptor = context->vfs->Open(
                    guest_path, {.read = true});
                try {
                    auto lease = context->vfs->CaptureReadLease(descriptor, 0);
                    context->vfs->Close(descriptor);
                    state.player = context->video_source_player_factory(
                        std::make_shared<VfsVideoSource>(std::move(lease)));
                } catch (...) {
                    try { context->vfs->Close(descriptor); } catch (...) {}
                    throw;
                }
                state.guest_path = guest_path;
                state.duration_ms = state.player->Metadata().duration_ms;
                context->video_views[handle] = std::move(state);
            } catch (const VfsError& error) {
                GuestLog(call, core::LogLevel::warn,
                         "VideoView.setVideoPath: " + guest_path +
                             " is not resolvable: " + error.what());
                return dx::VmValue::Void();
            }
            catch (const video::VideoPlayerError& error) {
                GuestLog(call, core::LogLevel::warn,
                         "VideoView.setVideoPath: cannot open " + guest_path +
                             ": " + error.what() +
                             "; playback will complete immediately");
                return dx::VmValue::Void();
            }
            GuestLog(call, core::LogLevel::info,
                     "VideoView.setVideoPath: decoding " + guest_path);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("start", "()V",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock video_lock(context->video_views_mutex);
            const auto handle = call.receiver.Value();
            auto* state = VideoStateOf(context, handle);
            if (state == nullptr || state->player == nullptr) {
                // Honest fallback: no decoded playback exists, so schedule
                // completion at the next video-pump boundary. A real
                // MediaPlayer callback is asynchronous and must not re-enter
                // guest code from inside start().
                GuestLog(call, core::LogLevel::warn,
                         "VideoView.start: no decoded playback; reporting "
                         "deferred completion");
                context->pending_video_completion.insert(handle);
                return dx::VmValue::Void();
            }
            if (state->completed) {
                state->player->SeekTo(0);
                state->base_position_ms = 0;
                state->completed = false;
                state->pcm_phase = 0;
                state->pcm_carry.clear();
            }
            state->playing = true;
            state->start_uptime_ms = context->uptime_millis.load();
            return dx::VmValue::Void();
        });
    builder.FinalMethod("pause", "()V",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock video_lock(context->video_views_mutex);
            auto* state = VideoStateOf(context, call.receiver.Value());
            if (state != nullptr && state->playing) {
                state->base_position_ms =
                    VideoPositionOf(*state, context->uptime_millis.load());
                state->playing = false;
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("seekTo", "(I)V",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock video_lock(context->video_views_mutex);
            auto* state = VideoStateOf(context, call.receiver.Value());
            if (state == nullptr || state->player == nullptr) {
                return dx::VmValue::Void();
            }
            const auto requested = static_cast<std::int64_t>(
                call.arguments[0].AsInt());
            const auto position = std::clamp<std::int64_t>(
                requested, 0, state->duration_ms);
            state->player->SeekTo(position);
            state->base_position_ms = position;
            state->start_uptime_ms = context->uptime_millis.load();
            state->completed = false;
            state->pcm_phase = 0;
            state->pcm_carry.clear();
            return dx::VmValue::Void();
        });
    builder.FinalMethod("stopPlayback", "()V",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock video_lock(context->video_views_mutex);
            context->video_views.erase(call.receiver.Value());
            context->pending_video_completion.erase(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getDuration", "()I",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock video_lock(context->video_views_mutex);
            const auto* state =
                VideoStateOf(context, call.receiver.Value());
            return dx::VmValue::Int(state == nullptr
                                        ? 0
                                        : static_cast<std::int32_t>(
                                              state->duration_ms));
        });
    builder.FinalMethod("getCurrentPosition", "()I",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock video_lock(context->video_views_mutex);
            const auto* state = VideoStateOf(context, call.receiver.Value());
            if (state == nullptr) return dx::VmValue::Int(0);
            return dx::VmValue::Int(static_cast<std::int32_t>(
                VideoPositionOf(*state, context->uptime_millis.load())));
        });
    builder.FinalMethod("canSeekForward", "()Z",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock video_lock(context->video_views_mutex);
            // AOSP reports the prepared stream capability. Every host
            // VideoPlayer implements bounded SeekTo, while an unopened or
            // released view has not reached the prepared state.
            const auto* state = VideoStateOf(context, call.receiver.Value());
            return dx::VmValue::Int(
                state != nullptr && state->player != nullptr ? 1 : 0);
        });
    builder.FinalMethod("canSeekBackward", "()Z",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock video_lock(context->video_views_mutex);
            const auto* state = VideoStateOf(context, call.receiver.Value());
            return dx::VmValue::Int(
                state != nullptr && state->player != nullptr ? 1 : 0);
        });
    builder.FinalMethod("canPause", "()Z",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock video_lock(context->video_views_mutex);
            const auto* state = VideoStateOf(context, call.receiver.Value());
            return dx::VmValue::Int(
                state != nullptr && state->player != nullptr ? 1 : 0);
        });
    builder.FinalMethod("setOnCompletionListener", "(Landroid/media/MediaPlayer$OnCompletionListener;)V",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock video_lock(context->video_views_mutex);
            context->video_completion[call.receiver.Value()] =
                call.arguments[0].ref;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setOnErrorListener",
        "(Landroid/media/MediaPlayer$OnErrorListener;)V",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock video_lock(context->video_views_mutex);
            const auto listener = call.arguments[0].ref;
            if (listener.IsValid()) {
                context->video_errors[call.receiver.Value()] = listener;
            } else {
                context->video_errors.erase(call.receiver.Value());
            }
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from support_video.cpp ----
// Guest video pump (ADR-0021): publishes decoded frames, mixes video PCM
// into the session stereo buffer, and fires onCompletion at the frame
// boundary. The VideoView intrinsic handlers live in the per-class
// declaration file (android_widget_VideoView.cpp); the playback-position
// math and listener invocation they share with this pump are in shared.h.

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
bool MixOneVideoView(DexVmAndroidContext::VideoViewState& state,
                     const std::span<std::int64_t> accumulator,
                     const std::uint32_t output_rate) {
    const auto& metadata = state.player->Metadata();
    if (!metadata.HasAudio()) return false;
    const auto channels =
        static_cast<std::size_t>(metadata.audio_channels);
    const auto source_rate = metadata.audio_sample_rate;
    const auto output_frames = accumulator.size() / 2U;

    const auto consumed_index = static_cast<std::size_t>(
        (state.pcm_phase +
         static_cast<std::uint64_t>(output_frames) * source_rate) /
        output_rate);
    const bool has_carry = !state.pcm_carry.empty();
    const auto fetch_frames =
        consumed_index + 1U - (has_carry ? 1U : 0U);
    std::vector<std::int16_t> source((has_carry ? 1U : 0U) * channels);
    if (has_carry) {
        std::copy(state.pcm_carry.begin(), state.pcm_carry.end(),
                  source.begin());
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
        accumulator[frame * 2U] += left;
        accumulator[frame * 2U + 1U] += right;
        phase += source_rate;
        while (phase >= output_rate) {
            phase -= output_rate;
            ++index;
        }
    }
    state.pcm_phase = phase;
    if (index < available) {
        state.pcm_carry.assign(source.begin() + static_cast<std::ptrdiff_t>(
                                                    index * channels),
                               source.begin() + static_cast<std::ptrdiff_t>(
                                                    (index + 1U) * channels));
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

std::size_t MixVideoPcmIntoAccumulator(
    DexVmAndroidContext& context,
    const std::span<std::int64_t> interleaved_stereo,
    const std::uint32_t output_rate) {
    if (output_rate == 0U || interleaved_stereo.empty() ||
        interleaved_stereo.size() % 2U != 0U) {
        throw std::invalid_argument(
            "video PCM mix needs a non-empty stereo buffer and a positive "
            "rate");
    }
    std::scoped_lock lock(context.video_views_mutex);
    std::size_t contributed = 0;
    for (auto& [handle, state] : context.video_views) {
        if (state.player == nullptr || !state.playing) continue;
        if (MixOneVideoView(state, interleaved_stereo, output_rate)) {
            ++contributed;
        }
    }
    return contributed;
}

std::size_t MixVideoPcmIntoStereo(
    DexVmAndroidContext& context,
    const std::span<std::int16_t> interleaved_stereo,
    const std::uint32_t output_rate) {
    std::vector<std::int64_t> accumulator(interleaved_stereo.size());
    audio::CopyPcm16IntoAccumulator(interleaved_stereo, accumulator);
    const auto contributed =
        MixVideoPcmIntoAccumulator(context, accumulator, output_rate);
    audio::SaturateStereoPcm16(accumulator, interleaved_stereo);
    return contributed;
}

std::optional<std::string> PumpAndroidAudioTracks(
    dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    const auto post = [&](const char* descriptor, const dexvm::VmObjectRef weak,
                          const std::int32_t what, const std::int32_t arg1 = 0,
                          const std::int32_t arg2 = 0)
        -> std::optional<std::string> {
        if (!weak.IsValid()) return std::nullopt;
        const auto klass = vm.Linker().ResolveDescriptor(descriptor);
        const auto method = vm.Linker().FindDirectMethod(
            klass, "postEventFromNative",
            "(Ljava/lang/Object;IIILjava/lang/Object;)V");
        if (!method.has_value()) {
            return std::string(descriptor) + " postEventFromNative is missing";
        }
        const auto outcome = vm.Call(
            *method,
            std::vector<dexvm::VmValue>{
                dexvm::VmValue::Ref(weak), dexvm::VmValue::Int(what),
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
            if (found == context.audio_tracks.end() ||
                !context.pcm_playback->HasPlayer(found->second.player)) {
                continue;
            }
            auto& state = found->second;
            const auto head =
                context.pcm_playback->PositionFrames(state.player);
            if (head < state.last_notified_head ||
                head < state.last_observed_head) {
                state.RetirePendingPeriodicCallbacks();
                state.last_notified_head = head;
                state.last_observed_head = head;
                continue;
            }
            if (context.pcm_playback->PlayState(state.player) !=
                audio::OpenSlesPlayState::playing) {
                state.RetirePendingPeriodicCallbacks();
                state.last_notified_head = head;
                state.last_observed_head = head;
                continue;
            }
            const auto player = state.player;
            const auto period = state.notification_period;
            const auto prior_head = state.last_notified_head;
            const auto prior_observed_head = state.last_observed_head;
            const auto weak = state.jni_weak;
            const bool marker_due = state.marker_position > 0 &&
                !state.marker_fired &&
                prior_head < static_cast<std::uint32_t>(state.marker_position) &&
                head >= static_cast<std::uint32_t>(state.marker_position);
            const auto periodic_count = period > 0
                ? head / static_cast<std::uint32_t>(period) -
                      prior_head / static_cast<std::uint32_t>(period)
                : 0U;
            if (period > 0) {
                state.periodic_callbacks_generated +=
                    head / static_cast<std::uint32_t>(period) -
                    prior_observed_head / static_cast<std::uint32_t>(period);
            } else {
                state.last_notified_head = head;
            }
            state.last_observed_head = head;
            if (marker_due) state.marker_fired = true;
            if (marker_due) {
                if (const auto error =
                        post("Landroid/media/AudioTrack;", weak, 3);
                    error.has_value()) {
                    return error;
                }
            }
            for (std::uint32_t index = 0; index < periodic_count; ++index) {
                const auto delivered_head = static_cast<std::uint32_t>(
                    (prior_head / static_cast<std::uint32_t>(period) +
                     index + 1U) * static_cast<std::uint32_t>(period));
                const auto expected_previous_head = index == 0U
                    ? prior_head
                    : delivered_head - static_cast<std::uint32_t>(period);
                found = context.audio_tracks.find(handle);
                if (found == context.audio_tracks.end() ||
                    found->second.notification_period != period ||
                    found->second.last_notified_head != expected_previous_head) {
                    break;
                }
                // Advance only the threshold represented by this event. If a
                // synchronous refill stops this pass, later lifecycle safe
                // points must still observe every remaining crossed period.
                found->second.last_notified_head = delivered_head;
                const auto queued_before =
                    context.pcm_playback->QueuedBytes(found->second.player);
                if (const auto error =
                        post("Landroid/media/AudioTrack;", weak, 4);
                    error.has_value()) {
                    found->second.last_notified_head = expected_previous_head;
                    return error;
                }
                ++found->second.periodic_callbacks_delivered;
                // Deliver the Handler message before posting another overdue
                // period so a refill write can coalesce the rest.
                if (const auto error = PumpJavaThreads(vm, context);
                    error.has_value()) {
                    return error;
                }
                found = context.audio_tracks.find(handle);
                if (found == context.audio_tracks.end() ||
                    found->second.player != player ||
                    found->second.notification_period != period ||
                    found->second.last_notified_head != delivered_head) {
                    break;
                }
                if (context.pcm_playback->QueuedBytes(found->second.player) >
                    queued_before) {
                    break;
                }
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
        if (context.encoded_music && context.encoded_music->TakeDecodeFailure(found->second.music)) {
            found->second.phase = Phase::error;
            found->second.error_event_pending = true;
        }
        if (found->second.error_event_pending) {
            found->second.error_event_pending = false;
            if (const auto error = post("Landroid/media/MediaPlayer;",
                                        found->second.jni_weak, 100, 1, -38);
                error.has_value()) return error;
            continue;
        }
        if (found->second.phase == Phase::preparing) {
            const auto status = context.encoded_music == nullptr
                                    ? audio::EncodedMusicMixer::PrepareStatus::failed
                                    : context.encoded_music->PollPrepare(
                                          found->second.music);
            if (status == audio::EncodedMusicMixer::PrepareStatus::pending) {
                continue;
            }
            found->second.phase = status == audio::EncodedMusicMixer::PrepareStatus::ready
                                      ? Phase::prepared : Phase::error;
            if (found->second.phase == Phase::error) {
                if (const auto error = post("Landroid/media/MediaPlayer;",
                                            found->second.jni_weak, 100, 1, -1);
                    error.has_value()) {
                    return error;
                }
                continue;
            }
            found->second.prepared_event_pending = true;
        }
        if (found->second.prepared_event_pending) {
            found->second.prepared_event_pending = false;
            if (const auto error = post("Landroid/media/MediaPlayer;",
                                        found->second.jni_weak, 1);
                error.has_value()) {
                return error;
            }
            // Listener may release/reset the player and invalidate this entry.
            continue;
        }
        if (context.encoded_music != nullptr &&
            context.encoded_music->Completed(found->second.music)) {
            found->second.phase = Phase::completed;
            if (const auto error = post("Landroid/media/MediaPlayer;",
                                        found->second.jni_weak, 2);
                error.has_value()) {
                return error;
            }
            continue;
        }
        if (found->second.seek_event_pending) {
            found->second.seek_event_pending = false;
            if (const auto error = post("Landroid/media/MediaPlayer;",
                                        found->second.jni_weak, 4);
                error.has_value()) {
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
            if (const auto error =
                    post("Landroid/media/SoundPool$SoundPoolImpl;", weak, 1,
                         load.sound, load.status);
                error.has_value()) {
                return error;
            }
        }
    }
    return PumpJavaThreads(vm, context);
}

std::optional<std::string> PumpVideoViews(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::function<void(std::vector<std::uint8_t> rgba8)>& publish) {
    std::scoped_lock video_lock(context.video_views_mutex);
    // Missing/unopenable streams complete asynchronously just like a real
    // MediaPlayer event. Clear the snapshot before invoking guest callbacks:
    // a callback may stop or restart the same view.
    std::vector<std::uint64_t> pending(
        context.pending_video_completion.begin(),
        context.pending_video_completion.end());
    context.pending_video_completion.clear();
    for (const auto handle : pending) {
        const auto error = android_intrinsics::InvokeVideoCompletionListener(
            vm, context, handle);
        if (error.has_value()) return error;
    }

    // Handle list first: onCompletion may mutate video_views (stopPlayback,
    // replay), which must not invalidate the iteration.
    std::vector<std::uint64_t> handles;
    handles.reserve(context.video_views.size());
    for (const auto& [handle, state] : context.video_views) {
        if (state.player != nullptr && state.playing) {
            handles.push_back(handle);
        }
    }
    const auto uptime = context.uptime_millis.load();
    for (const auto handle : handles) {
        const auto found = context.video_views.find(handle);
        if (found == context.video_views.end()) continue;
        auto& state = found->second;
        if (state.player == nullptr || !state.playing) continue;
        const auto position =
            android_intrinsics::VideoPositionOf(state, uptime);
        try {
            auto frame = state.player->TakeFrame(position);
            if (frame.has_value() && publish) {
                publish(video::ComposeRgbaOnCanvas(
                    *frame, context.surface_width, context.surface_height));
            }
        } catch (const video::VideoPlayerError& error) {
            return "video decode failed for " + state.guest_path + ": " +
                   error.what();
        }
        if (position >= state.duration_ms && !state.completed) {
            state.playing = false;
            state.base_position_ms = state.duration_ms;
            state.completed = true;
            const auto error =
                android_intrinsics::InvokeVideoCompletionListener(
                    vm, context, handle);
            if (error.has_value()) return error;
        }
    }
    return std::nullopt;
}

std::vector<AndroidAudioTrackDiagnosticSnapshot>
SnapshotAndroidAudioTracks(const DexVmAndroidContext& context) {
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
    std::ranges::sort(snapshots, {},
                      &AndroidAudioTrackDiagnosticSnapshot::receiver);
    return snapshots;
}

}  // namespace ogplay::runtime

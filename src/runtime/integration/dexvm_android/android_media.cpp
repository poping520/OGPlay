// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_media_AudioManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_media_AudioManager {

Decl Declare_android_media_AudioManager(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/media/AudioManager;", "Ljava/lang/Object;");
    builder.FinalMethod("getRingerMode", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(2);  // RINGER_MODE_NORMAL
    });
    builder.FinalMethod("isMusicActive", "()Z", [](dx::IntrinsicContext&) {
        // OGPlay owns the session mixer and has no external media session.
        return dx::VmValue::Int(0);
    });
    const auto max_volume = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(15); });
    const auto set_volume = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("getStreamMaxVolume", "(I)I", max_volume);
    builder.FinalMethod("setStreamVolume", "(III)V", set_volume);
    builder.FinalMethod("getStreamVolume", "(I)I", max_volume);
    builder.FinalMethod("setStreamMute", "(IZ)V", set_volume);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_media_AudioManager(const Context& context) {
    return dvm80_android_media_AudioManager::Declare_android_media_AudioManager(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_media_MediaPlayer_OnCompletionListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_media_MediaPlayer_OnCompletionListener {

Decl Declare_android_media_MediaPlayer_OnCompletionListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/media/MediaPlayer$OnCompletionListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_media_MediaPlayer_OnCompletionListener(const Context& context) {
    return dvm80_android_media_MediaPlayer_OnCompletionListener::Declare_android_media_MediaPlayer_OnCompletionListener(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_media_MediaPlayer_OnErrorListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_media_MediaPlayer_OnErrorListener {

Decl Declare_android_media_MediaPlayer_OnErrorListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/media/MediaPlayer$OnErrorListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_media_MediaPlayer_OnErrorListener(const Context& context) {
    return dvm80_android_media_MediaPlayer_OnErrorListener::Declare_android_media_MediaPlayer_OnErrorListener(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_media_MediaPlayer_OnPreparedListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_media_MediaPlayer_OnPreparedListener {

Decl Declare_android_media_MediaPlayer_OnPreparedListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/media/MediaPlayer$OnPreparedListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_media_MediaPlayer_OnPreparedListener(const Context& context) {
    return dvm80_android_media_MediaPlayer_OnPreparedListener::Declare_android_media_MediaPlayer_OnPreparedListener(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_media_MediaPlayer.cpp ----
// MediaPlayer handlers bound to the session's offline mixer ("big" bank,
// resid-keyed). Playback state is real; path-backed sources and completion
// callbacks stay recorded gaps.

#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_media_MediaPlayer {

Decl Declare_android_media_MediaPlayer(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/media/MediaPlayer;", "Ljava/lang/Object;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.FinalMethod("setDataSource", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            // Path-backed playback is not wired to the mixer yet: record the
            // gap loudly; start() on this instance will have no audio.
            GuestLog(call, core::LogLevel::warn,
                     "MediaPlayer.setDataSource is not wired to the mixer: " +
                         call.vm.StringUtf8(call.arguments[0].ref));
            return dx::VmValue::Void();
        });
    builder.FinalMethod("isLooping", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->media_looping.find(call.receiver.Value());
            return dx::VmValue::Int(
                found != context->media_looping.end() && found->second ? 1
                                                                       : 0);
        });
    builder.StaticMethod("create",
        "(Landroid/content/Context;I)Landroid/media/MediaPlayer;",
        [context](dx::IntrinsicContext& call) {
            const auto resource = call.arguments[1].AsInt();
            const auto instance = call.vm.NewIntrinsicInstance(
                "Landroid/media/MediaPlayer;");
            if (!context->session->SoundPoolMixer().Load(resource)) {
                GuestLog(call, core::LogLevel::warn,
                         "MediaPlayer.create failed for resource " +
                             std::to_string(resource));
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            context->media_resources[instance.Value()] = resource;
            context->media_playing[instance.Value()] = false;
            return dx::VmValue::Ref(instance);
        });
    const auto media_resource = [context](dx::IntrinsicContext& call)
        -> std::optional<std::int32_t> {
        const auto found =
            context->media_resources.find(call.receiver.Value());
        if (found == context->media_resources.end()) return std::nullopt;
        return found->second;
    };
    builder.FinalMethod("isPlaying", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->media_playing.find(call.receiver.Value());
            return dx::VmValue::Int(
                found != context->media_playing.end() && found->second ? 1
                                                                       : 0);
        });
    builder.FinalMethod("start", "()V",
        [context, media_resource](dx::IntrinsicContext& call) {
            const auto resource = media_resource(call);
            if (resource.has_value()) {
                static_cast<void>(context->session->SoundPoolMixer().Play(
                    audio::JavaSoundPoolKind::big, *resource, 0, 1.0F, true));
                context->media_playing[call.receiver.Value()] = true;
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("pause", "()V",
        [context, media_resource](dx::IntrinsicContext& call) {
            const auto resource = media_resource(call);
            if (resource.has_value()) {
                context->session->SoundPoolMixer().Pause(
                    audio::JavaSoundPoolKind::big, *resource, 0);
                context->media_playing[call.receiver.Value()] = false;
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("stop", "()V",
        [context, media_resource](dx::IntrinsicContext& call) {
            const auto resource = media_resource(call);
            if (resource.has_value()) {
                context->session->SoundPoolMixer().Stop(
                    audio::JavaSoundPoolKind::big, *resource, 0);
                context->media_playing[call.receiver.Value()] = false;
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("release", "()V",
        [context, media_resource](dx::IntrinsicContext& call) {
            const auto resource = media_resource(call);
            if (resource.has_value()) {
                context->session->SoundPoolMixer().Stop(
                    audio::JavaSoundPoolKind::big, *resource, 0);
                context->session->SoundPoolMixer().Unload(*resource);
            }
            context->media_resources.erase(call.receiver.Value());
            context->media_playing.erase(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("prepare", "()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.FinalMethod("seekTo", "(I)V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.FinalMethod("setLooping", "(Z)V",
        [context](dx::IntrinsicContext& call) {
            context->media_looping[call.receiver.Value()] =
                call.arguments[0].AsInt() != 0;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setVolume", "(FF)V",
        [context, media_resource](dx::IntrinsicContext& call) {
            const auto resource = media_resource(call);
            if (resource.has_value()) {
                context->session->SoundPoolMixer().SetVolume(
                    audio::JavaSoundPoolKind::big, *resource, 0,
                    call.arguments[0].AsFloat());
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setOnCompletionListener",
        "(Landroid/media/MediaPlayer$OnCompletionListener;)V",
        [](dx::IntrinsicContext&) {
            // Completion callbacks require the media clock; recorded gap.
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_media_MediaPlayer(const Context& context) {
    return dvm80_android_media_MediaPlayer::Declare_android_media_MediaPlayer(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_media_SoundPool.cpp ----
// SoundPool handlers bound to the session's offline mixer (resid is the
// key). Playback state is real; unimplemented callbacks stay recorded gaps.

#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_media_SoundPool {

Decl Declare_android_media_SoundPool(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/media/SoundPool;", "Ljava/lang/Object;");
    builder.Constructor("(III)V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.FinalMethod("load", "(Landroid/content/Context;II)I",
        [context](dx::IntrinsicContext& call) {
            const auto resource = call.arguments[1].AsInt();
            auto& mixer = context->session->SoundPoolMixer();
            if (!mixer.Load(resource)) {
                GuestLog(call, core::LogLevel::warn,
                         "SoundPool.load failed for resource " +
                             std::to_string(resource));
                return dx::VmValue::Int(0);
            }
            return dx::VmValue::Int(resource);  // sound id == resource id
        });
    builder.FinalMethod("play", "(IFFIIF)I",
        [context](dx::IntrinsicContext& call) {
            const auto sound = call.arguments[0].AsInt();
            const auto volume = call.arguments[1].AsFloat();
            const auto loop = call.arguments[3].AsInt();
            auto& mixer = context->session->SoundPoolMixer();
            const auto stream = context->next_sound_stream++;
            if (!mixer.Play(audio::JavaSoundPoolKind::pool, sound, stream,
                            volume, loop != 0)) {
                return dx::VmValue::Int(0);
            }
            context->sound_streams[stream] = sound;
            return dx::VmValue::Int(stream);
        });
    const auto stream_call =
        [context](dx::IntrinsicContext& call,
                  const std::function<void(audio::JavaSoundPoolMixer&,
                                           std::int32_t, std::int32_t)>&
                      action) {
            const auto stream = call.arguments[0].AsInt();
            const auto found = context->sound_streams.find(stream);
            if (found != context->sound_streams.end()) {
                action(context->session->SoundPoolMixer(), found->second,
                       stream);
            }
            return dx::VmValue::Void();
        };
    builder.FinalMethod("pause", "(I)V",
        [stream_call](dx::IntrinsicContext& call) {
            return stream_call(call, [](auto& mixer, const auto resource,
                                        const auto stream) {
                mixer.Pause(audio::JavaSoundPoolKind::pool, resource, stream);
            });
        });
    builder.FinalMethod("resume", "(I)V",
        [stream_call](dx::IntrinsicContext& call) {
            return stream_call(call, [](auto& mixer, const auto resource,
                                        const auto stream) {
                mixer.Resume(audio::JavaSoundPoolKind::pool, resource, stream);
            });
        });
    builder.FinalMethod("stop", "(I)V",
        [stream_call](dx::IntrinsicContext& call) {
            return stream_call(call, [](auto& mixer, const auto resource,
                                        const auto stream) {
                mixer.Stop(audio::JavaSoundPoolKind::pool, resource, stream);
            });
        });
    builder.FinalMethod("unload", "(I)Z",
        [context](dx::IntrinsicContext& call) {
            context->session->SoundPoolMixer().Unload(
                call.arguments[0].AsInt());
            return dx::VmValue::Int(1);
        });
    builder.FinalMethod("release", "()V",
        [context](dx::IntrinsicContext&) {
            context->session->SoundPoolMixer().StopAllSounds();
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setVolume", "(IFF)V",
        [context](dx::IntrinsicContext& call) {
            const auto stream = call.arguments[0].AsInt();
            const auto found = context->sound_streams.find(stream);
            if (found != context->sound_streams.end()) {
                context->session->SoundPoolMixer().SetVolume(
                    audio::JavaSoundPoolKind::pool, found->second, stream,
                    call.arguments[1].AsFloat());
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setRate", "(IF)V",
        [context](dx::IntrinsicContext& call) {
            const auto stream = call.arguments[0].AsInt();
            const auto found = context->sound_streams.find(stream);
            if (found != context->sound_streams.end()) {
                context->session->SoundPoolMixer().SetPitch(
                    audio::JavaSoundPoolKind::pool, found->second, stream,
                    call.arguments[1].AsFloat());
            }
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_media_SoundPool(const Context& context) {
    return dvm80_android_media_SoundPool::Declare_android_media_SoundPool(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_VideoView.cpp ----
// VideoView handlers: real decoded playback through the injected
// VideoPlayer factory (ADR-0021). Playback state math and the onCompletion
// invocation are shared with the guest video pump via shared.h.

#include <algorithm>

#include "ogplay/runtime/vfs/vfs.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_VideoView {

Decl Declare_android_widget_VideoView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/VideoView;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.FinalMethod("setVideoPath", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            const auto handle = call.receiver.Value();
            context->video_views.erase(handle);
            context->pending_video_completion.erase(handle);
            const auto path_ref = call.arguments[0].ref;
            if (!path_ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "setVideoPath path is null"};
            }
            const auto guest_path = call.vm.StringUtf8(path_ref);
            if (!context->video_player_factory) {
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
            std::optional<std::filesystem::path> host_path;
            try {
                host_path = context->vfs->HostPathFor(guest_path);
            } catch (const VfsError& error) {
                GuestLog(call, core::LogLevel::warn,
                         "VideoView.setVideoPath: " + guest_path +
                             " is not resolvable: " + error.what());
                return dx::VmValue::Void();
            }
            if (!host_path.has_value()) {
                GuestLog(call, core::LogLevel::warn,
                         "VideoView.setVideoPath: " + guest_path +
                             " is not a host-backed file; playback will "
                             "complete immediately (recorded gap)");
                return dx::VmValue::Void();
            }
            try {
                DexVmAndroidContext::VideoViewState state;
                state.player = context->video_player_factory(*host_path);
                state.guest_path = guest_path;
                state.duration_ms = state.player->Metadata().duration_ms;
                context->video_views[handle] = std::move(state);
            } catch (const video::VideoPlayerError& error) {
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
            context->video_views.erase(call.receiver.Value());
            context->pending_video_completion.erase(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getDuration", "()I",
        [context](dx::IntrinsicContext& call) {
            const auto* state =
                VideoStateOf(context, call.receiver.Value());
            return dx::VmValue::Int(state == nullptr
                                        ? 0
                                        : static_cast<std::int32_t>(
                                              state->duration_ms));
        });
    builder.FinalMethod("getCurrentPosition", "()I",
        [context](dx::IntrinsicContext& call) {
            const auto* state = VideoStateOf(context, call.receiver.Value());
            if (state == nullptr) return dx::VmValue::Int(0);
            return dx::VmValue::Int(static_cast<std::int32_t>(
                VideoPositionOf(*state, context->uptime_millis.load())));
        });
    builder.FinalMethod("canSeekForward", "()Z",
        [context](dx::IntrinsicContext& call) {
            // AOSP reports the prepared stream capability. Every host
            // VideoPlayer implements bounded SeekTo, while an unopened or
            // released view has not reached the prepared state.
            const auto* state = VideoStateOf(context, call.receiver.Value());
            return dx::VmValue::Int(
                state != nullptr && state->player != nullptr ? 1 : 0);
        });
    builder.FinalMethod("canSeekBackward", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto* state = VideoStateOf(context, call.receiver.Value());
            return dx::VmValue::Int(
                state != nullptr && state->player != nullptr ? 1 : 0);
        });
    builder.FinalMethod("canPause", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto* state = VideoStateOf(context, call.receiver.Value());
            return dx::VmValue::Int(
                state != nullptr && state->player != nullptr ? 1 : 0);
        });
    builder.FinalMethod("setOnCompletionListener", "(Landroid/media/MediaPlayer$OnCompletionListener;)V",
        [context](dx::IntrinsicContext& call) {
            context->video_completion[call.receiver.Value()] =
                call.arguments[0].ref;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setOnErrorListener",
        "(Landroid/media/MediaPlayer$OnErrorListener;)V",
        [context](dx::IntrinsicContext& call) {
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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_VideoView(const Context& context) {
    return dvm80_android_widget_VideoView::Declare_android_widget_VideoView(context);
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

#include "ogplay/video/rgba_canvas.h"

#include "shared.h"

namespace ogplay::runtime {

namespace {

void SaturatingAdd(std::int16_t& destination, const std::int16_t value) {
    const auto sum = static_cast<std::int32_t>(destination) + value;
    destination = static_cast<std::int16_t>(
        std::clamp<std::int32_t>(sum, INT16_MIN, INT16_MAX));
}

// Mixes one view's audio into the stereo buffer. Nearest-neighbour
// resampling with an integer phase accumulator: every output frame maps to
// the current source frame; the phase advances by the source rate and
// consumes source frames whenever it crosses the output rate, so batches of
// any size stay drift-free.
bool MixOneVideoView(DexVmAndroidContext::VideoViewState& state,
                     const std::span<std::int16_t> output,
                     const std::uint32_t output_rate) {
    const auto& metadata = state.player->Metadata();
    if (!metadata.HasAudio()) return false;
    const auto channels =
        static_cast<std::size_t>(metadata.audio_channels);
    const auto source_rate = metadata.audio_sample_rate;
    const auto output_frames = output.size() / 2U;

    // Source frames touched by this batch (index of the last used frame,
    // relative to the carry-inclusive stream position).
    const auto last_used = static_cast<std::size_t>(
        (state.pcm_phase +
         static_cast<std::uint64_t>(output_frames - 1U) * source_rate) /
        output_rate);
    const bool has_carry = !state.pcm_carry.empty();
    const auto fetch_frames = last_used + 1U - (has_carry ? 1U : 0U);
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
        if (index >= available) break;  // end of audio: silence remains
        const auto* samples = source.data() + index * channels;
        const auto left = samples[0];
        const auto right = channels >= 2U ? samples[1] : samples[0];
        SaturatingAdd(output[frame * 2U], left);
        SaturatingAdd(output[frame * 2U + 1U], right);
        phase += source_rate;
        while (phase >= output_rate) {
            phase -= output_rate;
            ++index;
        }
    }
    state.pcm_phase = phase;
    // The frame the next batch starts on is still buffered locally; carry
    // it so the player cursor never rewinds.
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
    for (const auto& [handle, state] : context.video_views) {
        if (state.player != nullptr && state.playing) return true;
    }
    return false;
}

std::size_t MixVideoPcmIntoStereo(
    DexVmAndroidContext& context,
    const std::span<std::int16_t> interleaved_stereo,
    const std::uint32_t output_rate) {
    if (output_rate == 0U || interleaved_stereo.empty() ||
        interleaved_stereo.size() % 2U != 0U) {
        throw std::invalid_argument(
            "video PCM mix needs a non-empty stereo buffer and a positive "
            "rate");
    }
    std::size_t contributed = 0;
    for (auto& [handle, state] : context.video_views) {
        if (state.player == nullptr || !state.playing) continue;
        if (MixOneVideoView(state, interleaved_stereo, output_rate)) {
            ++contributed;
        }
    }
    return contributed;
}

std::optional<std::string> PumpVideoViews(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::function<void(std::vector<std::uint8_t> rgba8)>& publish) {
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

}  // namespace ogplay::runtime

// DVM-80: API-family translation unit. Physical consolidation only.

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

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

void CheckVideoBudget(const Context& context, dx::IntrinsicContext& call) {
    if (!context->video_views.contains(call.receiver.Value()) && context->video_views.size() >= 8U) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("android.video.instance_budget", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "video instance budget exceeded"};
    }
}

void OpenVideoSource(const Context& context, dx::IntrinsicContext& call,
                     std::shared_ptr<const video::VideoDataSource> source,
                     std::string name, std::int32_t generation) {
    std::scoped_lock lock(context->video_views_mutex);
    CheckVideoBudget(context, call);
    if (!context->video_source_player_factory) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("android.video.decoder", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "video decoder is unavailable"};
    }
    try {
        if (!source || source->Size() == 0U) throw video::VideoPlayerError("video source is empty");
        DexVmAndroidContext::VideoViewState state;
        state.player = context->video_source_player_factory(std::move(source));
        if (!state.player) throw video::VideoPlayerError("video decoder returned no player");
        video::ValidateVideoMetadata(state.player->Metadata());
        state.guest_path = std::move(name);
        state.generation = generation;
        const auto node = FindViewUiNode(*context, call.receiver.Value());
        state.was_attached = node && context->ui_tree.IsAttached(*node);
        state.duration_ms = state.player->Metadata().duration_ms;
        state.pending_event = 1;
        context->video_views.insert_or_assign(call.receiver.Value(), std::move(state));
        if (const auto node = FindViewUiNode(*context, call.receiver.Value())) {
            auto* view = context->ui_tree.Get(*node);
            const auto& metadata = context->video_views.at(call.receiver.Value()).player->Metadata();
            view->intrinsic = {static_cast<std::int32_t>(metadata.width), static_cast<std::int32_t>(metadata.height)};
            context->ui_tree.MarkLayoutDirty(*node);
        }
        GuestLog(call, core::LogLevel::info, "VideoView: local video prepared; callback pending");
    } catch (const video::VideoPlayerError& error) {
        throw dx::VmJavaThrow{"Ljava/io/IOException;", error.what()};
    }
}
} // namespace

Decl Declare_android_widget_VideoView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/VideoView;", "Landroid/view/SurfaceView;");
    constexpr auto flags = dx::kAccPrivate | dx::kAccNative;
    builder.DirectMethod("nativeOpenPath", "(Ljava/lang/String;I)V", [context](dx::IntrinsicContext& call) {
        if (!context->vfs) throw dx::VmJavaThrow{"Ljava/io/IOException;", "video VFS unavailable"};
        if (!call.arguments[0].ref.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "video path is null"};
        const auto path = call.vm.StringUtf8(call.arguments[0].ref);
        try {
            const auto fd = context->vfs->Open(path, {.read = true});
            std::shared_ptr<const VfsReadLease> lease;
            try { lease = context->vfs->CaptureReadLease(fd, 0); }
            catch (...) { context->vfs->Close(fd); throw; }
            context->vfs->Close(fd);
            OpenVideoSource(context, call, std::make_shared<VfsVideoSource>(std::move(lease)), path, call.arguments[1].AsInt());
        } catch (const VfsError& error) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;", error.what()};
        }
        return dx::VmValue::Void();
    }, flags);
    builder.DirectMethod("nativeOpenFd", "(Ljava/io/FileDescriptor;JJI)V", [context](dx::IntrinsicContext& call) {
        const auto* fd = call.vm.IO().FindDescriptor(call.arguments[0].ref);
        const auto offset = call.arguments[1].AsLong(), length = call.arguments[2].AsLong();
        if (!fd || fd->closed || fd->kind != dx::IoRuntime::DescriptorKind::apk_entry ||
            offset < 0 || length <= 0 || static_cast<std::uint64_t>(offset) < fd->base_offset)
            throw dx::VmJavaThrow{"Ljava/io/IOException;", "invalid raw-resource descriptor range"};
        audio::EncodedAudioSource source;
        source.kind = audio::EncodedAudioSource::Kind::apk_entry;
        source.name = fd->source;
        source.offset = static_cast<std::uint64_t>(offset) - fd->base_offset;
        source.length = static_cast<std::uint64_t>(length);
        const auto data = LoadEncodedAudioSource(*context, source);
        if (!data) throw dx::VmJavaThrow{"Ljava/io/IOException;", "raw-resource video range unavailable"};
        OpenVideoSource(context, call, std::make_shared<EncodedVideoSource>(data), source.name, call.arguments[3].AsInt());
        return dx::VmValue::Void();
    }, flags);
    builder.DirectMethod("nativeOpenError", "(ILjava/lang/String;)V", [context](dx::IntrinsicContext& call) {
        std::scoped_lock lock(context->video_views_mutex);
        CheckVideoBudget(context, call);
        DexVmAndroidContext::VideoViewState state;
        state.generation = call.arguments[0].AsInt();
        state.pending_event = 100;
        const auto node = FindViewUiNode(*context, call.receiver.Value());
        state.was_attached = node && context->ui_tree.IsAttached(*node);
        state.guest_path = call.vm.StringUtf8(call.arguments[1].ref).substr(0, 2048);
        GuestLog(call, core::LogLevel::warn, "VideoView: " + state.guest_path);
        context->video_views.insert_or_assign(call.receiver.Value(), std::move(state));
        return dx::VmValue::Void();
    }, flags);
    builder.DirectMethod("nativeUnsupported", "(Ljava/lang/String;)V", [](dx::IntrinsicContext& call) -> dx::VmValue {
        const auto operation = call.vm.StringUtf8(call.arguments[0].ref);
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("android.video." + operation, 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "unsupported video operation: " + operation};
    }, flags);
    builder.DirectMethod("nativeUnhandledError", "()V", [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("android.video.unhandled_error_ui", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "video error was not handled; Android error dialog is unavailable"};
    }, flags);
    builder.DirectMethod("nativeRelease", "(I)V", [context](dx::IntrinsicContext& call) {
        std::scoped_lock lock(context->video_views_mutex);
        const auto found = context->video_views.find(call.receiver.Value());
        if (found != context->video_views.end() && found->second.generation == call.arguments[0].AsInt()) {
            context->video_views.erase(found);
            if (const auto node = FindViewUiNode(*context, call.receiver.Value())) context->ui_tree.MarkDrawDirty(*node);
        }
        return dx::VmValue::Void();
    }, flags);
    const auto with_state = [context](auto operation) {
        return [context, operation](dx::IntrinsicContext& call) {
            std::scoped_lock lock(context->video_views_mutex);
            auto* state = VideoStateOf(context, call.receiver.Value());
            if (!state || !state->player || state->generation != call.arguments[0].AsInt())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "video player is stale or unavailable"};
            return operation(*state, call, *context);
        };
    };
    builder.DirectMethod("nativeStart", "(I)V", with_state([](auto& state, auto&, auto& context) {
        if (state.playing) return dx::VmValue::Void();
        if (state.completed) {
            state.player->SeekTo(0); state.base_position_ms = 0;
            state.completed = false; state.pcm_phase = 0; state.pcm_carry.clear();
        }
        state.playing = true; state.start_uptime_ms = context.uptime_millis.load();
        return dx::VmValue::Void();
    }), flags);
    builder.DirectMethod("nativePause", "(I)V", with_state([](auto& state, auto&, auto& context) {
        state.base_position_ms = VideoPositionOf(state, context.uptime_millis.load());
        state.playing = false; return dx::VmValue::Void();
    }), flags);
    builder.DirectMethod("nativeStop", "(I)V", with_state([](auto& state, auto&, auto&) {
        state.player->SeekTo(0); state.base_position_ms = 0; state.playing = false;
        state.pcm_phase = 0; state.pcm_carry.clear(); state.latest_frame.reset();
        state.frame_requested = false;
        return dx::VmValue::Void();
    }), flags);
    builder.DirectMethod("nativeSeek", "(II)V", with_state([](auto& state, auto& call, auto& context) {
        const auto position = std::clamp<std::int64_t>(call.arguments[1].AsInt(), 0, state.duration_ms);
        state.player->SeekTo(position); state.base_position_ms = position;
        state.start_uptime_ms = context.uptime_millis.load(); state.completed = false;
        state.pcm_phase = 0; state.pcm_carry.clear(); state.latest_frame.reset();
        state.frame_requested = true;
        return dx::VmValue::Void();
    }), flags);
    builder.DirectMethod("nativeVolume", "(IFF)V", with_state([](auto& state, auto& call, auto&) {
        const auto left = call.arguments[1].AsFloat(), right = call.arguments[2].AsFloat();
        if (!std::isfinite(left) || !std::isfinite(right))
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "video volume must be finite"};
        state.left_volume = std::clamp(left, 0.0F, 1.0F); state.right_volume = std::clamp(right, 0.0F, 1.0F);
        return dx::VmValue::Void();
    }), flags);
    builder.DirectMethod("nativeDuration", "(I)I", with_state([](auto& state, auto&, auto&) { return dx::VmValue::Int(static_cast<std::int32_t>(state.duration_ms)); }), flags);
    builder.DirectMethod("nativePosition", "(I)I", with_state([](auto& state, auto&, auto& context) { return dx::VmValue::Int(static_cast<std::int32_t>(VideoPositionOf(state, context.uptime_millis.load()))); }), flags);
    builder.DirectMethod("nativeWidth", "(I)I", with_state([](auto& state, auto&, auto&) { return dx::VmValue::Int(static_cast<std::int32_t>(state.player->Metadata().width)); }), flags);
    builder.DirectMethod("nativeHeight", "(I)I", with_state([](auto& state, auto&, auto&) { return dx::VmValue::Int(static_cast<std::int32_t>(state.player->Metadata().height)); }), flags);
    builder.DirectMethod("nativeIsPlaying", "(I)Z", with_state([](auto& state, auto&, auto&) { return dx::VmValue::Int(state.playing ? 1 : 0); }), flags);
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
        accumulator[frame * 2U] += static_cast<std::int64_t>(std::lround(left * state.left_volume));
        accumulator[frame * 2U + 1U] += static_cast<std::int64_t>(std::lround(right * state.right_volume));
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
        try {
            if (MixOneVideoView(state, interleaved_stereo, output_rate)) ++contributed;
        } catch (const video::VideoPlayerError&) {
            // Audio workers publish only a fact. The guest main pump owns Java callbacks.
            state.frame_requested = false;
            state.playing = false;
            state.pending_event = 100;
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

void ComposeVideoViews(DexVmAndroidContext& context, std::vector<std::uint8_t>& canvas,
                       const std::uint32_t width, const std::uint32_t height,
                       const bool on_top) {
    if (width == 0 || height == 0 ||
        canvas.size() != static_cast<std::uint64_t>(width) * height * 4U) return;
    std::scoped_lock lock(context.video_views_mutex);
    if (context.video_views.empty()) return;
    const auto intersect = [](ui::Rect a, const ui::Rect b) {
        return ui::Rect{std::max(a.left, b.left), std::max(a.top, b.top),
                        std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
    };
    const auto visit = [&](const auto& self, const ui::UiNodeId id,
                           ui::Rect clip, float alpha) -> void {
        const auto* node = context.ui_tree.Get(id);
        if (!node || node->visibility != ui::Visibility::Visible) return;
        alpha *= std::clamp(node->alpha, 0.0F, 1.0F);
        const auto object = context.ui_node_to_object.find(id);
        if (object != context.ui_node_to_object.end() && node->surface_on_top == on_top) {
            const auto found = context.video_views.find(object->second.Value());
            if (found != context.video_views.end() && found->second.latest_frame) {
                const auto& frame = *found->second.latest_frame;
                const auto bounds = node->screen_frame;
                const auto rect_width = static_cast<std::int64_t>(bounds.right) - bounds.left;
                const auto rect_height = static_cast<std::int64_t>(bounds.bottom) - bounds.top;
                if (rect_width > 0 && rect_height > 0 && frame.width > 0 && frame.height > 0 &&
                    frame.rgba8.size() == static_cast<std::uint64_t>(frame.width) * frame.height * 4U) {
                    // Scale only visible pixels; oversized guest layout bounds never allocate a canvas.
                    const double scale = std::min(static_cast<double>(rect_width) / frame.width,
                                                  static_cast<double>(rect_height) / frame.height);
                    const auto image_width = std::max<std::int64_t>(1, static_cast<std::int64_t>(frame.width * scale));
                    const auto image_height = std::max<std::int64_t>(1, static_cast<std::int64_t>(frame.height * scale));
                    const auto image_left = bounds.left + (rect_width - image_width) / 2;
                    const auto image_top = bounds.top + (rect_height - image_height) / 2;
                    const auto visible = intersect(clip, bounds);
                    for (auto y = visible.top; y < visible.bottom; ++y) {
                        for (auto x = visible.left; x < visible.right; ++x) {
                            const auto target = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4U;
                            const bool in_image = x >= image_left && x < image_left + image_width &&
                                                  y >= image_top && y < image_top + image_height;
                            std::size_t source{};
                            if (in_image) {
                                const auto sx = static_cast<std::uint64_t>(x - image_left) * frame.width / static_cast<std::uint64_t>(image_width);
                                const auto sy = static_cast<std::uint64_t>(y - image_top) * frame.height / static_cast<std::uint64_t>(image_height);
                                source = static_cast<std::size_t>((sy * frame.width + sx) * 4U);
                            }
                            for (std::size_t channel = 0; channel < 3U; ++channel) {
                                const auto value = in_image ? frame.rgba8[source + channel] : 0;
                                canvas[target + channel] = static_cast<std::uint8_t>(
                                    std::lround(value * alpha + canvas[target + channel] * (1.0F - alpha)));
                            }
                            canvas[target + 3U] = 255U;
                        }
                    }
                }
            }
        }
        if (node->clip_children) clip = intersect(clip, node->screen_frame);
        if (node->clip_to_padding) {
            auto padded = node->screen_frame;
            padded.left += node->padding.left; padded.top += node->padding.top;
            padded.right -= node->padding.right; padded.bottom -= node->padding.bottom;
            clip = intersect(clip, padded);
        }
        for (const auto child : node->children) self(self, child, clip, alpha);
    };
    visit(visit, context.ui_tree.Root(),
          ui::Rect{0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height)}, 1.0F);
}

std::optional<std::string> PumpVideoViews(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::function<void(std::vector<std::uint8_t> rgba8)>& publish) {
    std::vector<std::uint64_t> handles;
    {
        std::scoped_lock lock(context.video_views_mutex);
        for (const auto& [handle, _] : context.video_views) handles.push_back(handle);
    }
    for (const auto handle : handles) {
        std::int32_t generation{};
        std::int32_t event{};
        bool detached{};
        {
            std::scoped_lock lock(context.video_views_mutex);
            const auto found = context.video_views.find(handle);
            if (found == context.video_views.end()) continue;
            auto& state = found->second;
            const auto node = FindViewUiNode(context, handle);
            const bool attached = node && context.ui_tree.IsAttached(*node);
            detached = state.was_attached && !attached;
            if (!attached && !detached) continue;
            state.was_attached = attached;
            generation = state.generation;
            event = std::exchange(state.pending_event, 0);
        }
        // No video/audio lock is held across Java listener code. Release/reopen
        // in a callback invalidates this generation before any decoder work.
        if (detached) {
            const auto type = vm.Linker().ResolveDescriptor("Landroid/widget/VideoView;");
            const auto slot = vm.Linker().FindVtableIndex(type, "suspend", "()V");
            if (!slot) return "VideoView suspend method is unavailable";
            const dexvm::VmObjectRef view{static_cast<std::uint32_t>(handle)};
            const auto root = vm.ProtectReferences(std::array{view});
            const auto outcome = vm.Call(vm.Linker().Class(type).vtable[*slot],
                                        std::array{dexvm::VmValue::Ref(view)});
            if (outcome.exception.IsValid()) return outcome.exception_message;
            continue;
        }
        if (event != 0) {
            if (auto error = android_intrinsics::InvokeVideoEvent(vm, context, handle, generation, event)) return error;
        }
        std::optional<video::VideoFrame> frame;
        event = 0;
        {
            std::scoped_lock lock(context.video_views_mutex);
            const auto found = context.video_views.find(handle);
            if (found == context.video_views.end() || found->second.generation != generation) continue;
            auto& state = found->second;
            if (!state.player || (!state.playing && !state.frame_requested)) continue;
            const auto position = android_intrinsics::VideoPositionOf(state, context.uptime_millis.load());
            try {
                frame = state.player->TakeFrame(position);
                if (frame) { state.latest_frame = *frame; state.frame_requested = false; }
                if (state.playing && position >= state.duration_ms && !state.completed) {
                    state.playing = false;
                    state.base_position_ms = state.duration_ms;
                    state.completed = true;
                    event = 2;
                }
            } catch (const video::VideoPlayerError& error) {
                if (auto* logger = vm.Log()) logger->Write(core::LogLevel::warn,
                    "runtime.video.decode", error.what());
                state.frame_requested = false;
                state.playing = false;
                state.latest_frame.reset();
                event = 100;
            }
        }
        if (frame && publish) publish(video::ComposeRgbaOnCanvas(*frame, context.surface_width, context.surface_height));
        if (event != 0) {
            if (auto error = android_intrinsics::InvokeVideoEvent(vm, context, handle, generation, event)) return error;
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

namespace ogplay::runtime {
std::optional<std::vector<AndroidAudioTrackDiagnosticSnapshot>> TrySnapshotAndroidAudioTracks(dexvm::Interpreter& vm, const DexVmAndroidContext& context) {
    auto& lock = vm.ExecutionLock();
    if (!lock.TryAcquire()) return std::nullopt;
    struct Release { dexvm::VmExecutionLock& lock; ~Release() { lock.Release(); } } release{lock};
    if (!context.pcm_playback) return std::nullopt;
    std::vector<AndroidAudioTrackDiagnosticSnapshot> result;
    for (const auto& [receiver, state] : context.audio_tracks) {
        if (result.size() == 129) break;
        const auto player = context.pcm_playback->TrySnapshot(state.player);
        if (!player) return std::nullopt;
        result.push_back({receiver, state.player, state.written_frames, player->queued_bytes, player->consumed_source_frames,
            player->underrun_output_frames, player->underrun_count, state.periodic_callbacks_generated,
            state.periodic_callbacks_delivered, state.PendingPeriodicCallbacks()});
    }
    return result;
}
std::optional<AndroidUiSnapshot> TrySnapshotAndroidUi(dexvm::Interpreter& vm, const DexVmAndroidContext& context) {
    auto& lock = vm.ExecutionLock();
    if (!lock.TryAcquire()) return std::nullopt;
    struct Release { dexvm::VmExecutionLock& lock; ~Release() { lock.Release(); } } release{lock};
    AndroidUiSnapshot result; result.generation = context.ui_tree.Generation(); result.nodes = context.ui_tree.Size();
    if (const auto focus = context.ui_tree.Focused()) result.focus = focus->Value();
    for (const auto& [id, object] : context.ui_node_to_object) {
        static_cast<void>(object);
        if (const auto node = context.ui_tree.Get(id)) { result.layout_dirty += node->layout_dirty; result.draw_dirty += node->draw_dirty; }
    }
    if (!context.ui_node_to_object.contains(context.ui_tree.Root())) {
        if (const auto* root = context.ui_tree.Get(context.ui_tree.Root())) {
            result.layout_dirty += root->layout_dirty; result.draw_dirty += root->draw_dirty;
        }
    }
    return result;
}
std::optional<std::vector<AndroidVideoSnapshot>> TrySnapshotAndroidVideo(const DexVmAndroidContext& context) {
    std::unique_lock lock(context.video_views_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return std::nullopt;
    std::vector<AndroidVideoSnapshot> result;
    for (const auto& [receiver, state] : context.video_views) {
        if (result.size() == 128) break;
        result.push_back({receiver, state.duration_ms, state.base_position_ms, state.playing, state.completed, state.player != nullptr});
    }
    return result;
}
}

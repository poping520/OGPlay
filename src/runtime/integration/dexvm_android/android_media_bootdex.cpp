#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/audio/encoded_music.h"
#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/audio/open_sles_pcm_mixer.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {

constexpr std::int32_t kStateInitialized = 1;
constexpr std::int32_t kStateNoStaticData = 2;
constexpr std::int32_t kModeStatic = 0;
constexpr std::int32_t kModeStream = 1;
constexpr std::int32_t kSuccess = 0;
constexpr std::int32_t kError = -1;
constexpr std::int32_t kErrorBadValue = -2;
constexpr std::int32_t kErrorInvalidOperation = -3;
constexpr auto kPrivNatFin =
    dx::kAccPrivate | dx::kAccNative | dx::kAccFinal;
constexpr auto kPrivStatNatFin =
    dx::kAccPrivate | dx::kAccStatic | dx::kAccNative | dx::kAccFinal;
constexpr auto kPubNatFin =
    dx::kAccPublic | dx::kAccNative | dx::kAccFinal;
constexpr auto kPubNat = dx::kAccPublic | dx::kAccNative;
constexpr auto kPrivNat = dx::kAccPrivate | dx::kAccNative;

[[nodiscard]] DexVmAndroidContext::AudioTrackState* FindTrack(
    const Context& context, const dx::VmObjectRef receiver) {
    const auto found = context->audio_tracks.find(receiver.Value());
    return found == context->audio_tracks.end() ? nullptr : &found->second;
}

[[nodiscard]] std::int32_t BytesPerSample(const std::int32_t encoding) {
    if (encoding == 3) return 1;
    if (encoding == 1 || encoding == 2) return 2;
    return 0;
}

[[nodiscard]] std::int32_t ChannelCount(const std::int32_t channels_or_config) {
    if (channels_or_config == 4) return 1;
    if (channels_or_config == 12 || channels_or_config == 3) return 2;
    if (channels_or_config == 1 || channels_or_config == 2) return channels_or_config;
    return 0;
}

[[nodiscard]] std::int32_t MinimumBuffer(const std::int32_t sample_rate,
                                         const std::int32_t channels,
                                         const std::int32_t encoding) {
    const auto bytes = BytesPerSample(encoding);
    if (channels < 1 || channels > 2 || bytes == 0 || sample_rate < 4000 ||
        sample_rate > 48000) {
        return kErrorBadValue;
    }
    return (sample_rate / 10) * channels * bytes;
}

[[nodiscard]] std::int32_t WriteBytes(
    const Context& context, dx::IntrinsicContext& call,
    const std::span<const std::byte> bytes) {
    auto* state = FindTrack(context, call.receiver);
    if (state == nullptr || context->pcm_playback == nullptr) {
        return kErrorInvalidOperation;
    }
    const auto frame_bytes = static_cast<std::size_t>(
        state->channel_count * state->bytes_per_sample);
    if (bytes.size() % frame_bytes != 0U) return kErrorBadValue;
    if (bytes.empty()) return 0;
    const auto player = state->player;
    const auto mode = state->mode;
    const auto buffer_size = static_cast<std::size_t>(state->buffer_size);
    if (mode == kModeStatic) {
        const auto take = std::min(bytes.size(), buffer_size);
        if (take % frame_bytes != 0U) return kErrorBadValue;
        if (!context->pcm_playback->Enqueue(player, bytes.first(take))) {
            return 0;
        }
        if (state->state == kStateNoStaticData) {
            state->state = kStateInitialized;
        }
        return static_cast<std::int32_t>(take);
    }
    std::size_t written{};
    auto remaining = bytes;
    while (!remaining.empty()) {
        const auto chunk = std::min(remaining.size(), buffer_size);
        auto& execution_lock = call.vm.ExecutionLock();
        const auto depth = execution_lock.ReleaseForBlocking();
        audio::OpenSlesEnqueueResult enqueue{};
        try {
            enqueue = context->pcm_playback->EnqueueBlocking(
                player, remaining.first(chunk), buffer_size);
        } catch (...) {
            execution_lock.ReacquireAfterBlocking(depth);
            throw;
        }
        execution_lock.ReacquireAfterBlocking(depth);
        state = FindTrack(context, call.receiver);
        if (enqueue != audio::OpenSlesEnqueueResult::enqueued ||
            state == nullptr || state->player != player) {
            if (written > 0U) return static_cast<std::int32_t>(written);
            return kErrorInvalidOperation;
        }
        written += chunk;
        remaining = remaining.subspan(chunk);
    }
    return static_cast<std::int32_t>(written);
}

void Unsupported(dx::IntrinsicContext& call, const char* name) {
    if (auto* ledger = call.vm.Ledger()) {
        ledger->RecordUnimplemented(std::string("dexvm.media.") + name, 0);
    }
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                          std::string(name) + " is not supported"};
}

void SetIntInstanceField(dx::IntrinsicContext& call, const char* name,
                         const std::int32_t value) {
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(call.receiver), name, "I");
    if (!field.has_value()) return;
    call.vm.Model().InstanceSlots(call.receiver)[
        call.vm.Linker().Field(*field).slot] = {
        static_cast<std::uint32_t>(value), dx::SlotTag::cat1};
}

void SetNativeContext(dx::IntrinsicContext& call, const std::int32_t token) {
    SetIntInstanceField(call, "mNativeContext", token);
}

[[nodiscard]] audio::EncodedAudioSource DescriptorSource(
    const Context& context, dx::IntrinsicContext& call,
    const dx::VmObjectRef fd, const std::int64_t offset,
    const std::int64_t length) {
    if (!fd.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "fd must not be null"};
    }
    if (offset < 0 || length < 0) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "offset and length must be non-negative"};
    }
    dx::IoRuntime::DescriptorState* descriptor{};
    try {
        descriptor = &call.vm.IO().Descriptor(fd);
    } catch (const dx::IoRuntimeError& error) {
        throw dx::VmJavaThrow{"Ljava/io/IOException;", error.what()};
    }
    audio::EncodedAudioSource source;
    source.kind = descriptor->kind == dx::IoRuntime::DescriptorKind::apk_entry
                      ? audio::EncodedAudioSource::Kind::apk_entry
                      : audio::EncodedAudioSource::Kind::vfs_path;
    source.name = descriptor->source;
    if (source.name.empty() && descriptor->file != nullptr) {
        source.name = descriptor->file->path;
    }
    if (descriptor->kind == dx::IoRuntime::DescriptorKind::apk_entry &&
        offset != 0) {
        if (static_cast<std::uint64_t>(offset) < descriptor->base_offset) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalArgumentException;",
                "APK descriptor offset precedes the asset payload"};
        }
        source.offset =
            static_cast<std::uint64_t>(offset) - descriptor->base_offset;
    } else {
        source.offset = static_cast<std::uint64_t>(offset);
    }
    source.length = static_cast<std::uint64_t>(length);
    static_cast<void>(CaptureEncodedAudioWindow(*context, source));
    return source;
}

[[nodiscard]] DexVmAndroidContext::MediaPlayerState& RequireMedia(
    const Context& context, dx::IntrinsicContext& call) {
    const auto found = context->media_players.find(call.receiver.Value());
    if (found == context->media_players.end() ||
        context->encoded_music == nullptr) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "MediaPlayer is not initialized"};
    }
    return found->second;
}

}  // namespace

Decl Declare_android_media_AudioTrack(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/media/AudioTrack;", "Ljava/lang/Object;");
    builder.StaticMethod(
        "native_get_min_buff_size", "(III)I",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(MinimumBuffer(
                call.arguments[0].AsInt(),
                ChannelCount(call.arguments[1].AsInt()),
                call.arguments[2].AsInt()));
        },
        kPrivStatNatFin);
    builder.StaticMethod(
        "native_get_output_sample_rate", "(I)I",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Int(static_cast<std::int32_t>(
                context->native_output_sample_rate));
        },
        kPrivStatNatFin);
    builder.DirectMethod(
        "native_setup", "(Ljava/lang/Object;IIIIII[I)I",
        [context](dx::IntrinsicContext& call) {
            const auto sample_rate = call.arguments[2].AsInt();
            const auto audio_stream = call.arguments[1].AsInt();
            const auto channels = ChannelCount(call.arguments[3].AsInt());
            const auto encoding = call.arguments[4].AsInt();
            const auto buffer_size = call.arguments[5].AsInt();
            const auto mode = call.arguments[6].AsInt();
            const auto bytes = BytesPerSample(encoding);
            if (context->pcm_playback == nullptr || audio_stream < 0 || audio_stream >= 10 || channels < 1 ||
                channels > 2 || bytes == 0 || sample_rate < 4000 ||
                sample_rate > 48000 || buffer_size <= 0 ||
                buffer_size % (channels * bytes) != 0 ||
                (mode != kModeStatic && mode != kModeStream) ||
                (mode == kModeStream &&
                 buffer_size < MinimumBuffer(sample_rate, channels,
                                             encoding))) {
                return dx::VmValue::Int(kError);
            }
            if (context->audio_tracks.contains(call.receiver.Value())) {
                return dx::VmValue::Int(kError);
            }
            const auto player = context->pcm_playback->CreatePlayer(
                {static_cast<std::uint32_t>(sample_rate),
                 static_cast<std::uint8_t>(channels),
                 static_cast<std::uint8_t>(bytes * 8)},
                mode == kModeStatic ? 1U : 255U);
            context->pcm_playback->SetPlayerKind(
                player, mode == kModeStatic
                            ? audio::OpenSlesPlayerKind::audio_track_static
                            : audio::OpenSlesPlayerKind::audio_track_stream);
            context->pcm_playback->SetAudioStream(player, audio_stream);
            context->audio_tracks[call.receiver.Value()] = {
                player,
                sample_rate,
                channels,
                bytes,
                buffer_size,
                mode,
                mode == kModeStatic ? kStateNoStaticData : kStateInitialized,
                0,
                0,
                false,
                0U,
                call.arguments[0].ref};
            const auto session = call.arguments[7].ref;
            if (session.IsValid() &&
                call.vm.Model().ArrayLength(session) > 0 &&
                call.vm.Model().PrimitiveArrayKind(session) ==
                    JniPrimitiveKind::integer) {
                call.vm.Model().SetPrimitiveElement(session, 0, 1);
            }
            SetIntInstanceField(call, "mNativeTrackInJavaObj",
                                static_cast<std::int32_t>(player));
            return dx::VmValue::Int(kSuccess);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_start", "()V",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            if (state == nullptr || context->pcm_playback == nullptr) {
                return dx::VmValue::Void();
            }
            context->pcm_playback->SetPlayState(
                state->player, audio::OpenSlesPlayState::playing);
            return dx::VmValue::Void();
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_pause", "()V",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            if (state != nullptr && context->pcm_playback != nullptr) {
                context->pcm_playback->SetPlayState(
                    state->player, audio::OpenSlesPlayState::paused);
            }
            return dx::VmValue::Void();
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_stop", "()V",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            if (state != nullptr && context->pcm_playback != nullptr) {
                context->pcm_playback->SetPlayState(
                    state->player, audio::OpenSlesPlayState::stopped);
                state->last_notified_head =
                    context->pcm_playback->PositionFrames(state->player);
            }
            return dx::VmValue::Void();
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_flush", "()V",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            if (state != nullptr && context->pcm_playback != nullptr &&
                state->mode == kModeStream &&
                context->pcm_playback->PlayState(state->player) !=
                    audio::OpenSlesPlayState::playing) {
                context->pcm_playback->ClearQueueKeepHead(state->player);
                state->last_notified_head =
                    context->pcm_playback->PositionFrames(state->player);
            }
            return dx::VmValue::Void();
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_release", "()V",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->audio_tracks.find(call.receiver.Value());
            if (found != context->audio_tracks.end()) {
                if (context->pcm_playback != nullptr) {
                    context->pcm_playback->DestroyPlayer(found->second.player);
                }
                context->audio_tracks.erase(found);
            }
            SetIntInstanceField(call, "mNativeTrackInJavaObj", 0);
            return dx::VmValue::Void();
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_finalize", "()V",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->audio_tracks.find(call.receiver.Value());
            if (found != context->audio_tracks.end()) {
                if (context->pcm_playback != nullptr) {
                    context->pcm_playback->DestroyPlayer(found->second.player);
                }
                context->audio_tracks.erase(found);
            }
            return dx::VmValue::Void();
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_write_byte", "([BIII)I",
        [context](dx::IntrinsicContext& call) {
            const auto array = call.arguments[0].ref;
            const auto offset = call.arguments[1].AsInt();
            const auto count = call.arguments[2].AsInt();
            if (!array.IsValid() || offset < 0 || count < 0 ||
                static_cast<std::int64_t>(offset) + count >
                    call.vm.Model().ArrayLength(array) ||
                call.vm.Model().PrimitiveArrayKind(array) !=
                    JniPrimitiveKind::byte) {
                return dx::VmValue::Int(kErrorBadValue);
            }
            return dx::VmValue::Int(WriteBytes(
                context, call,
                call.vm.Model().ReadByteRegion(array, offset, count)));
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_write_short", "([SIII)I",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            const auto array = call.arguments[0].ref;
            const auto offset = call.arguments[1].AsInt();
            const auto count = call.arguments[2].AsInt();
            if (state == nullptr || state->bytes_per_sample != 2 ||
                !array.IsValid() || offset < 0 || count < 0 ||
                static_cast<std::int64_t>(offset) + count >
                    call.vm.Model().ArrayLength(array) ||
                call.vm.Model().PrimitiveArrayKind(array) !=
                    JniPrimitiveKind::short_integer) {
                return dx::VmValue::Int(state == nullptr
                                            ? kErrorInvalidOperation
                                            : kErrorBadValue);
            }
            std::vector<std::byte> bytes(static_cast<std::size_t>(count) * 2U);
            for (std::int32_t index = 0; index < count; ++index) {
                const auto value = static_cast<std::uint16_t>(
                    call.vm.Model().GetPrimitiveElement(array, offset + index));
                bytes[static_cast<std::size_t>(index) * 2U] =
                    static_cast<std::byte>(value & 0xffU);
                bytes[static_cast<std::size_t>(index) * 2U + 1U] =
                    static_cast<std::byte>(value >> 8U);
            }
            const auto written = WriteBytes(context, call, bytes);
            return dx::VmValue::Int(written < 0 ? written : written / 2);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_setVolume", "(FF)V",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            if (state != nullptr && context->pcm_playback != nullptr) {
                context->pcm_playback->SetStereoVolume(
                    state->player,
                    std::clamp(call.arguments[0].AsFloat(), 0.0F, 1.0F),
                    std::clamp(call.arguments[1].AsFloat(), 0.0F, 1.0F));
            }
            return dx::VmValue::Void();
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_get_position", "()I",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            return dx::VmValue::Int(
                state == nullptr || context->pcm_playback == nullptr
                    ? 0
                    : static_cast<std::int32_t>(
                          context->pcm_playback->PositionFrames(state->player)));
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_set_pos_update_period", "(I)I",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            const auto period = call.arguments[0].AsInt();
            if (state == nullptr) return dx::VmValue::Int(kErrorInvalidOperation);
            if (period < 0) return dx::VmValue::Int(kErrorBadValue);
            state->notification_period = period;
            if (context->pcm_playback != nullptr) {
                state->last_notified_head =
                    context->pcm_playback->PositionFrames(state->player);
            }
            return dx::VmValue::Int(kSuccess);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_get_pos_update_period", "()I",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            return dx::VmValue::Int(state == nullptr ? 0
                                                     : state->notification_period);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_set_marker_pos", "(I)I",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            const auto marker = call.arguments[0].AsInt();
            if (state == nullptr) return dx::VmValue::Int(kErrorInvalidOperation);
            if (marker < 0) return dx::VmValue::Int(kErrorBadValue);
            state->marker_position = marker;
            state->marker_fired = false;
            return dx::VmValue::Int(kSuccess);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_get_marker_pos", "()I",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            return dx::VmValue::Int(state == nullptr ? 0
                                                     : state->marker_position);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_get_native_frame_count", "()I",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            if (state == nullptr || state->channel_count == 0 ||
                state->bytes_per_sample == 0) {
                return dx::VmValue::Int(0);
            }
            return dx::VmValue::Int(state->buffer_size /
                                    (state->channel_count *
                                     state->bytes_per_sample));
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_get_latency", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); }, kPrivNatFin);
    builder.DirectMethod(
        "native_reload_static", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(kSuccess); },
        kPrivNatFin);
    builder.DirectMethod(
        "native_set_loop", "(III)I",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            if (state == nullptr || context->pcm_playback == nullptr) {
                return dx::VmValue::Int(kErrorInvalidOperation);
            }
            context->pcm_playback->SetLoop(
                state->player,
                static_cast<std::uint32_t>(std::max(call.arguments[0].AsInt(), 0)),
                static_cast<std::uint32_t>(std::max(call.arguments[1].AsInt(), 0)),
                call.arguments[2].AsInt());
            return dx::VmValue::Int(kSuccess);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_set_playback_rate", "(I)I",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            if (state == nullptr || context->pcm_playback == nullptr ||
                state->sample_rate <= 0) {
                return dx::VmValue::Int(kErrorInvalidOperation);
            }
            const auto rate = call.arguments[0].AsInt();
            if (rate <= 0) return dx::VmValue::Int(kErrorBadValue);
            context->pcm_playback->SetPlaybackRate(
                state->player,
                static_cast<float>(rate) /
                    static_cast<float>(state->sample_rate));
            return dx::VmValue::Int(rate);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_get_playback_rate", "()I",
        [context](dx::IntrinsicContext& call) {
            auto* state = FindTrack(context, call.receiver);
            return dx::VmValue::Int(state == nullptr ? 0 : state->sample_rate);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_set_position", "(I)I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(kError); },
        kPrivNatFin);
    builder.DirectMethod(
        "native_get_timestamp", "([J)I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(kError); },
        kPrivNatFin);
    builder.DirectMethod(
        "native_attachAuxEffect", "(I)I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(kError); },
        kPrivNatFin);
    builder.DirectMethod(
        "native_setAuxEffectSendLevel", "(F)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, kPrivNatFin);
    return std::move(builder).Build();
}

Decl Declare_android_media_SoundPoolImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/media/SoundPool$SoundPoolImpl;", "Ljava/lang/Object;");
    builder.DirectMethod(
        "native_setup", "(Ljava/lang/Object;III)I",
        [context](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback == nullptr) {
                return dx::VmValue::Int(kError);
            }
            const auto pool = context->encoded_audio_playback->CreatePool(
                call.arguments[1].AsInt(), call.arguments[2].AsInt());
            DexVmAndroidContext::SoundPoolState state;
            state.pool = pool;
            state.jni_weak = call.arguments[0].ref;
            context->sound_pools[call.receiver.Value()] = std::move(state);
            SetNativeContext(call, static_cast<std::int32_t>(pool));
            return dx::VmValue::Int(kSuccess);
        },
        kPrivNatFin);
    const auto pool_of = [context](dx::IntrinsicContext& call) -> std::uint32_t {
        const auto found = context->sound_pools.find(call.receiver.Value());
        return found == context->sound_pools.end() ? 0U : found->second.pool;
    };
    const auto enqueue_load =
        [context](dx::IntrinsicContext& call, const std::int32_t sound) {
            const auto found = context->sound_pools.find(call.receiver.Value());
            if (found == context->sound_pools.end() || sound == 0) return;
            found->second.pending_loads.push_back({sound, 0});
        };
    builder.DirectMethod(
        "_load", "(Ljava/io/FileDescriptor;JJI)I",
        [context, pool_of, enqueue_load](dx::IntrinsicContext& call) {
            const auto pool = pool_of(call);
            if (pool == 0U || context->encoded_audio_playback == nullptr) {
                return dx::VmValue::Int(0);
            }
            auto source = DescriptorSource(context, call, call.arguments[0].ref,
                                           call.arguments[1].AsLong(),
                                           call.arguments[2].AsLong());
            const auto sound =
                context->encoded_audio_playback->LoadSample(pool, source);
            if (sound == 0 && source.lease != 0U) {
                context->encoded_audio_leases.erase(source.lease);
            }
            enqueue_load(call, sound);
            return dx::VmValue::Int(sound);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "_load", "(Ljava/lang/String;I)I",
        [context, pool_of, enqueue_load](dx::IntrinsicContext& call) {
            const auto pool = pool_of(call);
            if (pool == 0U || context->encoded_audio_playback == nullptr ||
                !call.arguments[0].ref.IsValid()) {
                return dx::VmValue::Int(0);
            }
            audio::EncodedAudioSource source;
            source.kind = audio::EncodedAudioSource::Kind::vfs_path;
            source.name = call.vm.StringUtf8(call.arguments[0].ref);
            source.length = std::numeric_limits<std::uint64_t>::max();
            static_cast<void>(CaptureEncodedAudioWindow(*context, source));
            const auto sound =
                context->encoded_audio_playback->LoadSample(pool, source);
            if (sound == 0 && source.lease != 0U) {
                context->encoded_audio_leases.erase(source.lease);
            }
            enqueue_load(call, sound);
            return dx::VmValue::Int(sound);
        },
        kPrivNatFin);
    builder.FinalMethod(
        "play", "(IFFIIF)I",
        [context, pool_of](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback == nullptr) {
                return dx::VmValue::Int(0);
            }
            return dx::VmValue::Int(context->encoded_audio_playback->PlaySample(
                pool_of(call), call.arguments[0].AsInt(),
                call.arguments[1].AsFloat(), call.arguments[2].AsFloat(),
                call.arguments[3].AsInt(), call.arguments[4].AsInt(),
                call.arguments[5].AsFloat()));
        },
        kPubNatFin);
    builder.FinalMethod(
        "pause", "(I)V",
        [context](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback != nullptr) {
                context->encoded_audio_playback->PauseStream(
                    call.arguments[0].AsInt());
            }
            return dx::VmValue::Void();
        },
        kPubNatFin);
    builder.FinalMethod(
        "resume", "(I)V",
        [context](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback != nullptr) {
                context->encoded_audio_playback->ResumeStream(
                    call.arguments[0].AsInt());
            }
            return dx::VmValue::Void();
        },
        kPubNatFin);
    builder.FinalMethod(
        "stop", "(I)V",
        [context](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback != nullptr) {
                context->encoded_audio_playback->StopStream(
                    call.arguments[0].AsInt());
            }
            return dx::VmValue::Void();
        },
        kPubNatFin);
    builder.FinalMethod(
        "unload", "(I)Z",
        [context, pool_of](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback == nullptr) {
                return dx::VmValue::Int(0);
            }
            const auto pool = pool_of(call);
            const auto source = context->encoded_audio_playback->SampleSource(
                pool, call.arguments[0].AsInt());
            const auto unloaded = context->encoded_audio_playback->UnloadSample(
                pool, call.arguments[0].AsInt());
            if (unloaded && source.has_value() && source->lease != 0U) {
                context->encoded_audio_leases.erase(source->lease);
            }
            return dx::VmValue::Int(unloaded ? 1 : 0);
        },
        kPubNatFin);
    builder.FinalMethod(
        "release", "()V",
        [context, pool_of](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback != nullptr) {
                const auto pool = pool_of(call);
                for (const auto& source :
                     context->encoded_audio_playback->PoolSources(pool)) {
                    if (source.lease != 0U) {
                        context->encoded_audio_leases.erase(source.lease);
                    }
                }
                context->encoded_audio_playback->DestroyPool(pool);
            }
            context->sound_pools.erase(call.receiver.Value());
            SetNativeContext(call, 0);
            return dx::VmValue::Void();
        },
        kPubNatFin);
    builder.FinalMethod(
        "setVolume", "(IFF)V",
        [context](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback != nullptr) {
                context->encoded_audio_playback->SetStreamVolume(
                    call.arguments[0].AsInt(), call.arguments[1].AsFloat(),
                    call.arguments[2].AsFloat());
            }
            return dx::VmValue::Void();
        },
        kPubNatFin);
    builder.FinalMethod(
        "setLoop", "(II)V",
        [context](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback != nullptr) {
                context->encoded_audio_playback->SetStreamLoop(
                    call.arguments[0].AsInt(), call.arguments[1].AsInt());
            }
            return dx::VmValue::Void();
        },
        kPubNatFin);
    builder.FinalMethod(
        "setPriority", "(II)V",
        [context](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback != nullptr) {
                context->encoded_audio_playback->SetStreamPriority(
                    call.arguments[0].AsInt(), call.arguments[1].AsInt());
            }
            return dx::VmValue::Void();
        },
        kPubNatFin);
    builder.FinalMethod(
        "setRate", "(IF)V",
        [context](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback != nullptr) {
                context->encoded_audio_playback->SetStreamRate(
                    call.arguments[0].AsInt(), call.arguments[1].AsFloat());
            }
            return dx::VmValue::Void();
        },
        kPubNatFin);
    builder.FinalMethod(
        "autoPause", "()V",
        [context, pool_of](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback != nullptr) {
                context->encoded_audio_playback->AutoPausePool(pool_of(call));
            }
            return dx::VmValue::Void();
        },
        kPubNatFin);
    builder.FinalMethod(
        "autoResume", "()V",
        [context, pool_of](dx::IntrinsicContext& call) {
            if (context->encoded_audio_playback != nullptr) {
                context->encoded_audio_playback->AutoResumePool(pool_of(call));
            }
            return dx::VmValue::Void();
        },
        kPubNatFin);
    return std::move(builder).Build();
}

Decl Declare_android_media_MediaPlayer(const Context& context) {
    using Phase = DexVmAndroidContext::MediaPlayerState::Phase;
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/media/MediaPlayer;", "Ljava/lang/Object;");
    builder.StaticMethod(
        "native_init", "()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); },
        kPrivStatNatFin);
    builder.DirectMethod(
        "native_setup", "(Ljava/lang/Object;)V",
        [context](dx::IntrinsicContext& call) {
            if (context->encoded_music == nullptr) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "encoded music mixer is unavailable"};
            }
            DexVmAndroidContext::MediaPlayerState state;
            try {
                state.music = context->encoded_music->Create();
            } catch (const std::length_error& error) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", error.what()};
            }
            state.jni_weak = call.arguments[0].ref;
            context->media_players[call.receiver.Value()] = std::move(state);
            SetNativeContext(call, static_cast<std::int32_t>(
                                       context->media_players[call.receiver.Value()].music));
            return dx::VmValue::Void();
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_finalize", "()V",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->media_players.find(call.receiver.Value());
            if (found != context->media_players.end()) {
                if (context->encoded_music != nullptr) {
                    context->encoded_music->Destroy(found->second.music);
                }
                if (found->second.source.lease != 0U) {
                    context->encoded_audio_leases.erase(
                        found->second.source.lease);
                }
                context->media_players.erase(found);
            }
            SetNativeContext(call, 0);
            return dx::VmValue::Void();
        },
        kPrivNatFin);
    const auto set_fd_source =
        [context](dx::IntrinsicContext& call) {
            auto& state = RequireMedia(context, call);
            if (state.phase != Phase::idle) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "MediaPlayer data source is already set"};
            }
            auto source = DescriptorSource(context, call, call.arguments[0].ref,
                                           call.arguments[1].AsLong(),
                                           call.arguments[2].AsLong());
            auto bytes = LoadEncodedAudioWindow(*context, source);
            if (!context->encoded_music->SetEncoded(state.music,
                                                    std::move(bytes))) {
                if (source.lease) context->encoded_audio_leases.erase(source.lease);
                throw dx::VmJavaThrow{
                    "Ljava/io/IOException;",
                    "MediaPlayer source is empty or too large"};
            }
            state.source = std::move(source);
            state.phase = Phase::initialized;
            return dx::VmValue::Void();
        };
    builder.DirectMethod(
        "_setDataSource", "(Ljava/io/FileDescriptor;JJ)V", set_fd_source,
        kPrivNat);
    builder.VirtualMethod(
        "setDataSource", "(Ljava/io/FileDescriptor;JJ)V", set_fd_source);
    builder.DirectMethod(
        "_setDataSource",
        "(Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            auto& state = RequireMedia(context, call);
            if (state.phase != Phase::idle) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "MediaPlayer data source is already set"};
            }
            if (!call.arguments[0].ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "path must not be null"};
            }
            const auto path = call.vm.StringUtf8(call.arguments[0].ref);
            if (path.find("://") != std::string::npos) {
                Unsupported(call, "MediaPlayer.networkSource");
            }
            audio::EncodedAudioSource source;
            source.kind = audio::EncodedAudioSource::Kind::vfs_path;
            source.name = path;
            source.length = std::numeric_limits<std::uint64_t>::max();
            static_cast<void>(CaptureEncodedAudioWindow(*context, source));
            auto bytes = LoadEncodedAudioWindow(*context, source);
            if (!context->encoded_music->SetEncoded(state.music,
                                                    std::move(bytes))) {
                if (source.lease) context->encoded_audio_leases.erase(source.lease);
                throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                      "MediaPlayer path could not be read"};
            }
            state.source = std::move(source);
            state.phase = Phase::initialized;
            return dx::VmValue::Void();
        },
        kPrivNat);
    builder.VirtualMethod(
        "prepare", "()V",
        [context](dx::IntrinsicContext& call) {
            auto& state = RequireMedia(context, call);
            if (state.phase == Phase::idle && context->encoded_music->HasEncoded(state.music)) {
                state.phase = Phase::initialized;
            }
            if (state.phase != Phase::initialized && state.phase != Phase::stopped) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "MediaPlayer prepare in invalid state"};
            }
            if (!context->encoded_music->Prepare(state.music)) {
                state.phase = Phase::error;
                throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                      "MediaPlayer prepare failed"};
            }
            state.phase = Phase::prepared;
            return dx::VmValue::Void();
        },
        kPubNat);
    builder.VirtualMethod(
        "prepareAsync", "()V",
        [context](dx::IntrinsicContext& call) {
            auto& state = RequireMedia(context, call);
            if (state.phase == Phase::idle && context->encoded_music->HasEncoded(state.music)) {
                state.phase = Phase::initialized;
            }
            if (state.phase != Phase::initialized && state.phase != Phase::stopped) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "MediaPlayer prepareAsync in invalid state"};
            }
            if (context->encoded_music == nullptr ||
                !context->encoded_music->BeginPrepare(state.music)) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "MediaPlayer prepareAsync could not start"};
            }
            state.phase = Phase::preparing;
            return dx::VmValue::Void();
        },
        kPubNat);
    builder.DirectMethod(
        "_start", "()V",
        [context](dx::IntrinsicContext& call) {
            auto& state = RequireMedia(context, call);
            if (state.phase != Phase::prepared && state.phase != Phase::started &&
                state.phase != Phase::paused && state.phase != Phase::completed) {
                state.phase = Phase::error;
                state.error_event_pending = true;
                context->encoded_music->Pause(state.music);
                return dx::VmValue::Void();
            }
            context->encoded_music->Start(state.music);
            state.phase = Phase::started;
            return dx::VmValue::Void();
        },
        kPrivNat);
    builder.DirectMethod(
        "_pause", "()V",
        [context](dx::IntrinsicContext& call) {
            auto& state = RequireMedia(context, call);
            if (state.phase != Phase::started && state.phase != Phase::paused &&
                state.phase != Phase::completed) {
                state.phase = Phase::error;
                state.error_event_pending = true;
                context->encoded_music->Pause(state.music);
                return dx::VmValue::Void();
            }
            context->encoded_music->Pause(state.music);
            state.phase = Phase::paused;
            return dx::VmValue::Void();
        },
        kPrivNat);
    builder.DirectMethod(
        "_stop", "()V",
        [context](dx::IntrinsicContext& call) {
            auto& state = RequireMedia(context, call);
            if (state.phase != Phase::prepared && state.phase != Phase::started &&
                state.phase != Phase::paused && state.phase != Phase::completed &&
                state.phase != Phase::stopped) {
                state.phase = Phase::error;
                state.error_event_pending = true;
                context->encoded_music->Pause(state.music);
                return dx::VmValue::Void();
            }
            context->encoded_music->Stop(state.music);
            state.phase = Phase::stopped;
            return dx::VmValue::Void();
        },
        kPrivNat);
    builder.DirectMethod(
        "_release", "()V",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->media_players.find(call.receiver.Value());
            if (found != context->media_players.end()) {
                context->encoded_music->Destroy(found->second.music);
                if (found->second.source.lease != 0U) {
                    context->encoded_audio_leases.erase(
                        found->second.source.lease);
                }
                context->media_players.erase(found);
            }
            SetNativeContext(call, 0);
            return dx::VmValue::Void();
        },
        kPrivNat);
    builder.DirectMethod(
        "_reset", "()V",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->media_players.find(call.receiver.Value());
            if (found == context->media_players.end() ||
                context->encoded_music == nullptr) {
                return dx::VmValue::Void();
            }
            context->encoded_music->Reset(found->second.music);
            if (found->second.source.lease != 0U) {
                context->encoded_audio_leases.erase(found->second.source.lease);
            }
            found->second.source = {};
            found->second.prepared_event_pending = false;
            found->second.seek_event_pending = false;
            found->second.error_event_pending = false;
            found->second.phase = Phase::idle;
            return dx::VmValue::Void();
        },
        kPrivNat);
    builder.VirtualMethod(
        "seekTo", "(I)V",
        [context](dx::IntrinsicContext& call) {
            auto& state = RequireMedia(context, call);
            if (state.phase != Phase::prepared && state.phase != Phase::started &&
                state.phase != Phase::paused && state.phase != Phase::completed) {
                state.phase = Phase::error;
                state.error_event_pending = true;
                context->encoded_music->Pause(state.music);
                return dx::VmValue::Void();
            }
            context->encoded_music->SeekMs(state.music,
                                           call.arguments[0].AsInt());
            state.seek_event_pending = true;
            return dx::VmValue::Void();
        },
        kPubNat);
    builder.VirtualMethod(
        "isPlaying", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->media_players.find(call.receiver.Value());
            return dx::VmValue::Int(
                found != context->media_players.end() &&
                        context->encoded_music != nullptr &&
                        context->encoded_music->IsPlaying(found->second.music)
                    ? 1
                    : 0);
        },
        kPubNat);
    builder.VirtualMethod(
        "isLooping", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->media_players.find(call.receiver.Value());
            return dx::VmValue::Int(
                found != context->media_players.end() &&
                        context->encoded_music != nullptr &&
                        context->encoded_music->IsLooping(found->second.music)
                    ? 1
                    : 0);
        },
        kPubNat);
    builder.VirtualMethod(
        "setLooping", "(Z)V",
        [context](dx::IntrinsicContext& call) {
            context->encoded_music->SetLooping(
                RequireMedia(context, call).music,
                call.arguments[0].AsInt() != 0);
            return dx::VmValue::Void();
        },
        kPubNat);
    builder.VirtualMethod(
        "setVolume", "(FF)V",
        [context](dx::IntrinsicContext& call) {
            context->encoded_music->SetVolume(
                RequireMedia(context, call).music, call.arguments[0].AsFloat(),
                call.arguments[1].AsFloat());
            return dx::VmValue::Void();
        },
        kPubNat);
    builder.VirtualMethod(
        "getDuration", "()I",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(context->encoded_music->DurationMs(
                RequireMedia(context, call).music));
        },
        kPubNat);
    builder.VirtualMethod(
        "getCurrentPosition", "()I",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(context->encoded_music->PositionMs(
                RequireMedia(context, call).music));
        },
        kPubNat);
    builder.VirtualMethod(
        "getVideoWidth", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); }, kPubNat);
    builder.VirtualMethod(
        "getVideoHeight", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); }, kPubNat);
    builder.VirtualMethod(
        "getAudioSessionId", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); }, kPubNat);
    builder.VirtualMethod(
        "setAudioSessionId", "(I)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, kPubNat);
    builder.VirtualMethod(
        "setAudioStreamType", "(I)V",
        [context](dx::IntrinsicContext& call) {
            auto& state = RequireMedia(context, call);
            const auto stream = call.arguments[0].AsInt();
            if (stream < 0 || stream >= 10)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid audio stream"};
            if (state.phase != Phase::idle && state.phase != Phase::initialized &&
                state.phase != Phase::stopped)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "stream type after prepare"};
            context->encoded_music->SetAudioStream(state.music, stream);
            return dx::VmValue::Void();
        }, kPubNat);
    builder.VirtualMethod(
        "attachAuxEffect", "(I)V",
        [](dx::IntrinsicContext& call) {
            Unsupported(call, "MediaPlayer.attachAuxEffect");
            return dx::VmValue::Void();
        },
        kPubNat);
    builder.VirtualMethod(
        "setAuxEffectSendLevel", "(F)V",
        [](dx::IntrinsicContext& call) {
            Unsupported(call, "MediaPlayer.setAuxEffectSendLevel");
            return dx::VmValue::Void();
        },
        kPubNat);
    builder.VirtualMethod(
        "setNextMediaPlayer", "(Landroid/media/MediaPlayer;)V",
        [](dx::IntrinsicContext& call) {
            Unsupported(call, "MediaPlayer.setNextMediaPlayer");
            return dx::VmValue::Void();
        },
        kPubNat);
    builder.DirectMethod(
        "_setVideoSurface", "(Landroid/view/Surface;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, kPrivNat);
    builder.DirectMethod(
        "_suspend", "()Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); }, kPrivNat);
    builder.DirectMethod(
        "_resume", "()Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); }, kPrivNat);
    builder.DirectMethod(
        "native_getMetadata", "(ZZLandroid/os/Parcel;)Z",
        [](dx::IntrinsicContext& call) {
            Unsupported(call, "MediaPlayer.native_getMetadata");
            return dx::VmValue::Int(0);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_invoke", "(Landroid/os/Parcel;Landroid/os/Parcel;)I",
        [](dx::IntrinsicContext& call) {
            Unsupported(call, "MediaPlayer.native_invoke");
            return dx::VmValue::Int(kError);
        },
        kPrivNatFin);
    builder.StaticMethod(
        "native_pullBatteryData", "(Landroid/os/Parcel;)I",
        [](dx::IntrinsicContext& call) {
            Unsupported(call, "MediaPlayer.native_pullBatteryData");
            return dx::VmValue::Int(kError);
        },
        dx::kAccPublic | dx::kAccStatic | dx::kAccNative);
    builder.DirectMethod(
        "native_setMetadataFilter", "(Landroid/os/Parcel;)I",
        [](dx::IntrinsicContext& call) {
            Unsupported(call, "MediaPlayer.native_setMetadataFilter");
            return dx::VmValue::Int(kError);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "native_setRetransmitEndpoint", "(Ljava/lang/String;I)I",
        [](dx::IntrinsicContext& call) {
            Unsupported(call, "MediaPlayer.native_setRetransmitEndpoint");
            return dx::VmValue::Int(kError);
        },
        kPrivNatFin);
    builder.DirectMethod(
        "updateProxyConfig", "(Landroid/net/ProxyProperties;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, kPrivNat);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

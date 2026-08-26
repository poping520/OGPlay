// DVM-80: API-family translation unit. Physical consolidation only.

#include <algorithm>
#include <cmath>
#include <limits>

#include "ogplay/audio/open_sles_pcm_mixer.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

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

// ---- DVM-84: API 19 AudioFormat / AudioTrack on the shared PCM backend ----

namespace ogplay::runtime::android_intrinsics::dvm84_android_media_AudioTrack {
namespace {

constexpr std::int32_t kStateUninitialized = 0;
constexpr std::int32_t kStateInitialized = 1;
constexpr std::int32_t kStateNoStaticData = 2;
constexpr std::int32_t kModeStatic = 0;
constexpr std::int32_t kModeStream = 1;
constexpr std::int32_t kErrorBadValue = -2;
constexpr std::int32_t kErrorInvalidOperation = -3;
constexpr std::int32_t kNativeOutputSampleRate = 48000;

struct Format final {
    std::int32_t channels{};
    std::int32_t bytes_per_sample{};
};

[[nodiscard]] std::optional<Format> DecodeFormat(
    const std::int32_t channel_config, const std::int32_t encoding) {
    const auto channels =
        channel_config == 1 || channel_config == 2 || channel_config == 4
            ? 1
            : (channel_config == 3 || channel_config == 12 ? 2 : 0);
    const auto bytes = encoding == 1 || encoding == 2
                           ? 2
                           : (encoding == 3 ? 1 : 0);
    if (channels == 0 || bytes == 0) return std::nullopt;
    return Format{channels, bytes};
}

[[nodiscard]] std::int32_t MinimumBuffer(const std::int32_t sample_rate,
                                         const std::int32_t channel_config,
                                         const std::int32_t encoding) {
    const auto format = DecodeFormat(channel_config, encoding);
    if (!format.has_value() || sample_rate < 4000 || sample_rate > 48000) {
        return kErrorBadValue;
    }
    return (sample_rate / 10) * format->channels * format->bytes_per_sample;
}

[[nodiscard]] DexVmAndroidContext::AudioTrackState* Find(
    const Context& context, const dx::VmObjectRef receiver) {
    const auto found = context->audio_tracks.find(receiver.Value());
    return found == context->audio_tracks.end() ? nullptr : &found->second;
}

[[nodiscard]] DexVmAndroidContext::AudioTrackState& Require(
    const Context& context, dx::IntrinsicContext& call) {
    auto* state = Find(context, call.receiver);
    if (state == nullptr || state->state == kStateUninitialized ||
        context->pcm_playback == nullptr ||
        !context->pcm_playback->HasPlayer(state->player)) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "AudioTrack is not initialized"};
    }
    return *state;
}

[[nodiscard]] std::int32_t WriteBytes(
    const Context& context, dx::IntrinsicContext& call,
    const std::span<const std::byte> bytes) {
    auto* state = Find(context, call.receiver);
    if (state == nullptr || context->pcm_playback == nullptr) {
        return kErrorInvalidOperation;
    }
    if (bytes.empty() || bytes.size() %
            static_cast<std::size_t>(state->channel_count *
                                     state->bytes_per_sample) != 0U ||
        bytes.size() > static_cast<std::size_t>(state->buffer_size)) {
        return kErrorBadValue;
    }
    if (state->mode == kModeStatic) context->pcm_playback->Clear(state->player);
    if (!context->pcm_playback->Enqueue(state->player, bytes)) return 0;
    if (state->state == kStateNoStaticData) state->state = kStateInitialized;
    return static_cast<std::int32_t>(bytes.size());
}

}  // namespace

Decl Declare_android_media_AudioFormat(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/media/AudioFormat;", "Ljava/lang/Object;");
    builder.ConstantInt("ENCODING_DEFAULT", "I", 1)
        .ConstantInt("ENCODING_PCM_16BIT", "I", 2)
        .ConstantInt("ENCODING_PCM_8BIT", "I", 3)
        .ConstantInt("CHANNEL_CONFIGURATION_DEFAULT", "I", 1)
        .ConstantInt("CHANNEL_CONFIGURATION_MONO", "I", 2)
        .ConstantInt("CHANNEL_CONFIGURATION_STEREO", "I", 3)
        .ConstantInt("CHANNEL_OUT_MONO", "I", 4)
        .ConstantInt("CHANNEL_OUT_STEREO", "I", 12);
    return std::move(builder).Build();
}

Decl Declare_android_media_AudioTrack(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/media/AudioTrack;", "Ljava/lang/Object;");
    builder.ConstantInt("MODE_STATIC", "I", kModeStatic)
        .ConstantInt("MODE_STREAM", "I", kModeStream)
        .ConstantInt("STATE_UNINITIALIZED", "I", kStateUninitialized)
        .ConstantInt("STATE_INITIALIZED", "I", kStateInitialized)
        .ConstantInt("STATE_NO_STATIC_DATA", "I", kStateNoStaticData)
        .ConstantInt("PLAYSTATE_STOPPED", "I", 1)
        .ConstantInt("PLAYSTATE_PAUSED", "I", 2)
        .ConstantInt("PLAYSTATE_PLAYING", "I", 3)
        .ConstantInt("SUCCESS", "I", 0)
        .ConstantInt("ERROR", "I", -1)
        .ConstantInt("ERROR_BAD_VALUE", "I", kErrorBadValue)
        .ConstantInt("ERROR_INVALID_OPERATION", "I", kErrorInvalidOperation);
    builder.StaticMethod("getMinVolume", "()F",
        [](dx::IntrinsicContext&) { return dx::VmValue::Float(0.0F); });
    builder.StaticMethod("getMaxVolume", "()F",
        [](dx::IntrinsicContext&) { return dx::VmValue::Float(1.0F); });
    builder.StaticMethod("getMinBufferSize", "(III)I",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(MinimumBuffer(
                call.arguments[0].AsInt(), call.arguments[1].AsInt(),
                call.arguments[2].AsInt()));
        });
    builder.StaticMethod("getNativeOutputSampleRate", "(I)I",
        [](dx::IntrinsicContext&) {
            // OGPlay's shared desktop output is configured at 48 kHz. Android's
            // API reports the native mixer rate for every legacy stream type.
            return dx::VmValue::Int(kNativeOutputSampleRate);
        });
    builder.Constructor("(IIIIII)V", [context](dx::IntrinsicContext& call) {
        const auto sample_rate = call.arguments[1].AsInt();
        const auto format = DecodeFormat(call.arguments[2].AsInt(),
                                         call.arguments[3].AsInt());
        const auto buffer_size = call.arguments[4].AsInt();
        const auto mode = call.arguments[5].AsInt();
        const auto frame_size = format.has_value()
                                    ? format->channels * format->bytes_per_sample
                                    : 0;
        if (call.arguments[0].AsInt() != 3 || !format.has_value() ||
            sample_rate < 4000 || sample_rate > 48000 || buffer_size <= 0 ||
            frame_size == 0 || buffer_size % frame_size != 0 ||
            (mode != kModeStatic && mode != kModeStream) ||
            context->pcm_playback == nullptr ||
            (mode == kModeStream && buffer_size < MinimumBuffer(
                 sample_rate, call.arguments[2].AsInt(),
                 call.arguments[3].AsInt()))) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "unsupported AudioTrack configuration"};
        }
        if (context->audio_tracks.contains(call.receiver.Value())) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "AudioTrack is already initialized"};
        }
        const auto player = context->pcm_playback->CreatePlayer(
            {static_cast<std::uint32_t>(sample_rate),
             static_cast<std::uint8_t>(format->channels),
             static_cast<std::uint8_t>(format->bytes_per_sample * 8)},
            mode == kModeStatic ? 1U : 255U);
        context->audio_tracks[call.receiver.Value()] = {
            player, sample_rate, format->channels, format->bytes_per_sample,
            buffer_size, mode,
            mode == kModeStatic ? kStateNoStaticData : kStateInitialized,
            0, dx::VmObjectRef{0}};
        return dx::VmValue::Void();
    });
    builder.FinalMethod("getState", "()I", [context](dx::IntrinsicContext& call) {
        const auto* state = Find(context, call.receiver);
        return dx::VmValue::Int(state == nullptr ? kStateUninitialized
                                                 : state->state);
    });
    builder.FinalMethod("getPlayState", "()I", [context](dx::IntrinsicContext& call) {
        const auto* state = Find(context, call.receiver);
        if (state == nullptr || context->pcm_playback == nullptr) {
            return dx::VmValue::Int(1);
        }
        const auto play = context->pcm_playback->PlayState(state->player);
        return dx::VmValue::Int(play == audio::OpenSlesPlayState::playing
                                    ? 3
                                    : (play == audio::OpenSlesPlayState::paused ? 2 : 1));
    });
    builder.FinalMethod("play", "()V", [context](dx::IntrinsicContext& call) {
        auto& state = Require(context, call);
        if (state.state != kStateInitialized) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "static AudioTrack has no data"};
        }
        context->pcm_playback->SetPlayState(
            state.player, audio::OpenSlesPlayState::playing);
        return dx::VmValue::Void();
    });
    const auto set_play_state = [context](const audio::OpenSlesPlayState play) {
        return [context, play](dx::IntrinsicContext& call) {
            auto& state = Require(context, call);
            context->pcm_playback->SetPlayState(state.player, play);
            return dx::VmValue::Void();
        };
    };
    builder.FinalMethod("pause", "()V", set_play_state(audio::OpenSlesPlayState::paused));
    builder.FinalMethod("stop", "()V", set_play_state(audio::OpenSlesPlayState::stopped));
    builder.FinalMethod("flush", "()V", [context](dx::IntrinsicContext& call) {
        auto& state = Require(context, call);
        if (state.mode == kModeStream) context->pcm_playback->Clear(state.player);
        return dx::VmValue::Void();
    });
    builder.FinalMethod("release", "()V", [context](dx::IntrinsicContext& call) {
        const auto found = context->audio_tracks.find(call.receiver.Value());
        if (found != context->audio_tracks.end()) {
            if (context->pcm_playback != nullptr) {
                context->pcm_playback->DestroyPlayer(found->second.player);
            }
            context->audio_tracks.erase(found);
        }
        return dx::VmValue::Void();
    });
    builder.FinalMethod("write", "([BII)I", [context](dx::IntrinsicContext& call) {
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto count = call.arguments[2].AsInt();
        if (!array.IsValid() || offset < 0 || count < 0 ||
            static_cast<std::int64_t>(offset) + count >
                call.vm.Model().ArrayLength(array) ||
            call.vm.Model().PrimitiveArrayKind(array) != JniPrimitiveKind::byte) {
            return dx::VmValue::Int(kErrorBadValue);
        }
        return dx::VmValue::Int(WriteBytes(
            context, call, call.vm.Model().ReadByteRegion(array, offset, count)));
    });
    builder.FinalMethod("write", "([SII)I", [context](dx::IntrinsicContext& call) {
        auto* state = Find(context, call.receiver);
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto count = call.arguments[2].AsInt();
        if (state == nullptr || state->bytes_per_sample != 2 || !array.IsValid() ||
            offset < 0 || count < 0 ||
            static_cast<std::int64_t>(offset) + count >
                call.vm.Model().ArrayLength(array) ||
            call.vm.Model().PrimitiveArrayKind(array) !=
                JniPrimitiveKind::short_integer) {
            return dx::VmValue::Int(state == nullptr ? kErrorInvalidOperation
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
    });
    builder.FinalMethod("setStereoVolume", "(FF)I", [context](dx::IntrinsicContext& call) {
        auto* state = Find(context, call.receiver);
        if (state == nullptr || context->pcm_playback == nullptr) {
            return dx::VmValue::Int(kErrorInvalidOperation);
        }
        const auto left = std::clamp(call.arguments[0].AsFloat(), 0.0F, 1.0F);
        const auto right = std::clamp(call.arguments[1].AsFloat(), 0.0F, 1.0F);
        context->pcm_playback->SetStereoVolume(state->player, left, right);
        return dx::VmValue::Int(0);
    });
    builder.FinalMethod("setVolume", "(F)I", [context](dx::IntrinsicContext& call) {
        auto* state = Find(context, call.receiver);
        if (state == nullptr || context->pcm_playback == nullptr) {
            return dx::VmValue::Int(kErrorInvalidOperation);
        }
        const auto volume = std::clamp(call.arguments[0].AsFloat(), 0.0F, 1.0F);
        context->pcm_playback->SetStereoVolume(state->player, volume, volume);
        return dx::VmValue::Int(0);
    });
    builder.FinalMethod("setPositionNotificationPeriod", "(I)I",
        [context](dx::IntrinsicContext& call) {
            auto* state = Find(context, call.receiver);
            const auto period = call.arguments[0].AsInt();
            if (state == nullptr) return dx::VmValue::Int(kErrorInvalidOperation);
            if (period < 0) return dx::VmValue::Int(kErrorBadValue);
            state->notification_period = period;
            return dx::VmValue::Int(0);
        });
    builder.FinalMethod(
        "setPlaybackPositionUpdateListener",
        "(Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;)V",
        [context](dx::IntrinsicContext& call) {
            auto* state = Find(context, call.receiver);
            if (state == nullptr) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "AudioTrack is not initialized"};
            }
            state->position_listener = call.arguments[0].ref;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getPlaybackHeadPosition", "()I",
        [context](dx::IntrinsicContext& call) {
            auto* state = Find(context, call.receiver);
            return dx::VmValue::Int(
                state == nullptr || context->pcm_playback == nullptr
                    ? 0
                    : static_cast<std::int32_t>(
                          context->pcm_playback->PositionFrames(state->player)));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics::dvm84_android_media_AudioTrack

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_media_AudioFormat(const Context& context) {
    return dvm84_android_media_AudioTrack::Declare_android_media_AudioFormat(context);
}
Decl Declare_android_media_AudioTrack(const Context& context) {
    return dvm84_android_media_AudioTrack::Declare_android_media_AudioTrack(context);
}
Decl Declare_android_media_AudioTrack_OnPlaybackPositionUpdateListener(
    const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Interface(
        "Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;");
    builder.UnimplementedVirtual(
        "onMarkerReached", "(Landroid/media/AudioTrack;)V", 0x0401U);
    builder.UnimplementedVirtual(
        "onPeriodicNotification", "(Landroid/media/AudioTrack;)V", 0x0401U);
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {
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
                found->second.position_listener.IsValid()) {
                visit(found->second.position_listener);
            }
        },
        [context](const dexvm::VmObjectRef object) {
            const auto found = context->audio_tracks.find(object.Value());
            if (found == context->audio_tracks.end()) return;
            if (context->pcm_playback != nullptr) {
                context->pcm_playback->DestroyPlayer(found->second.player);
            }
            context->audio_tracks.erase(found);
        },
        {}});
}
}  // namespace ogplay::runtime

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
// resid-keyed). Playback state is real; path-backed sources and listener
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
    const auto descriptor_source =
        [context](dx::IntrinsicContext& call, const dx::VmObjectRef fd,
                  const std::int64_t offset,
                  const std::int64_t length) -> audio::EncodedAudioSource {
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
        source.kind = descriptor->kind ==
                              dx::IoRuntime::DescriptorKind::apk_entry
                          ? audio::EncodedAudioSource::Kind::apk_entry
                          : audio::EncodedAudioSource::Kind::vfs_path;
        source.name = descriptor->source;
        if (descriptor->kind == dx::IoRuntime::DescriptorKind::apk_entry &&
            offset != 0) {
            if (static_cast<std::uint64_t>(offset) <
                descriptor->base_offset) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "APK descriptor offset precedes the asset payload"};
            }
            source.offset = static_cast<std::uint64_t>(offset) -
                            descriptor->base_offset;
        } else {
            source.offset = static_cast<std::uint64_t>(offset);
        }
        source.length = static_cast<std::uint64_t>(length);
        static_cast<void>(context);
        return source;
    };
    builder.FinalMethod("setDataSource", "(Ljava/io/FileDescriptor;)V",
        [context, descriptor_source](dx::IntrinsicContext& call) {
            auto source = descriptor_source(
                call, call.arguments[0].ref, 0,
                std::numeric_limits<std::int64_t>::max());
            source.length = UINT64_MAX;
            context->media_resources[call.receiver.Value()] =
                std::move(source);
            context->media_playing[call.receiver.Value()] = false;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setDataSource", "(Ljava/io/FileDescriptor;JJ)V",
        [context, descriptor_source](dx::IntrinsicContext& call) {
            context->media_resources[call.receiver.Value()] =
                descriptor_source(call, call.arguments[0].ref,
                                  call.arguments[1].AsLong(),
                                  call.arguments[2].AsLong());
            context->media_playing[call.receiver.Value()] = false;
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
            if (context->encoded_audio_playback == nullptr ||
                !context->encoded_audio_playback->Load(resource)) {
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
        -> std::optional<audio::EncodedAudioSource> {
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
                const auto looping = context->media_looping.contains(
                    call.receiver.Value()) &&
                    context->media_looping.at(call.receiver.Value());
                static_cast<void>(context->encoded_audio_playback->Play(
                    audio::JavaSoundPoolKind::big, *resource, 0, 1.0F,
                    looping));
                context->media_playing[call.receiver.Value()] = true;
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("pause", "()V",
        [context, media_resource](dx::IntrinsicContext& call) {
            const auto resource = media_resource(call);
            if (resource.has_value()) {
                context->encoded_audio_playback->Pause(
                    audio::JavaSoundPoolKind::big, *resource, 0);
                context->media_playing[call.receiver.Value()] = false;
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("stop", "()V",
        [context, media_resource](dx::IntrinsicContext& call) {
            const auto resource = media_resource(call);
            if (resource.has_value()) {
                context->encoded_audio_playback->Stop(
                    audio::JavaSoundPoolKind::big, *resource, 0);
                context->media_playing[call.receiver.Value()] = false;
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("release", "()V",
        [context, media_resource](dx::IntrinsicContext& call) {
            const auto resource = media_resource(call);
            if (resource.has_value()) {
                context->encoded_audio_playback->Stop(
                    audio::JavaSoundPoolKind::big, *resource, 0);
                context->encoded_audio_playback->Unload(*resource);
            }
            context->media_resources.erase(call.receiver.Value());
            context->media_playing.erase(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("prepare", "()V",
        [context, media_resource](dx::IntrinsicContext& call) {
            const auto source = media_resource(call);
            if (!source.has_value()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "MediaPlayer has no data source"};
            }
            if (context->encoded_audio_playback == nullptr ||
                !context->encoded_audio_playback->Load(*source)) {
                const auto reason = context->encoded_audio_playback == nullptr
                    ? std::optional<std::string>{}
                    : context->encoded_audio_playback->LoadFailure(*source);
                throw dx::VmJavaThrow{
                    "Ljava/io/IOException;",
                    reason.value_or("encoded audio backend is unavailable")};
            }
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
                context->encoded_audio_playback->SetVolume(
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
    builder.FinalMethod("setOnErrorListener",
        "(Landroid/media/MediaPlayer$OnErrorListener;)V",
        [](dx::IntrinsicContext&) {
            // The offline mixer has no failure surface to report; the
            // listener is accepted like the completion listener and the
            // callback stays a recorded gap.
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setOnPreparedListener",
        "(Landroid/media/MediaPlayer$OnPreparedListener;)V",
        [](dx::IntrinsicContext&) {
            // Sources prepare synchronously in this backend; the callback
            // stays a recorded gap.
            return dx::VmValue::Void();
        });
    builder.FinalMethod("reset", "()V",
        [context, media_resource](dx::IntrinsicContext& call) {
            // AOSP reset() returns the player to idle; the resid binding
            // from create() outlives it so a later start() can replay.
            const auto resource = media_resource(call);
            if (resource.has_value()) {
                context->encoded_audio_playback->Stop(
                    audio::JavaSoundPoolKind::big, *resource, 0);
                context->media_playing[call.receiver.Value()] = false;
            }
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

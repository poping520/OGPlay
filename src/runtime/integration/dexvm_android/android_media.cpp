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

namespace ogplay::runtime {
namespace {
using Audio = SimulatedDeviceState::Audio;
constexpr std::size_t kVoiceCall = 0;
constexpr std::size_t kRing = 2;
constexpr std::size_t kMusic = 3;
constexpr std::size_t kBluetoothSco = 6;
constexpr unsigned kMuteAffected = (1U << 1) | (1U << kRing) | (1U << kMusic) | (1U << 5);
constexpr unsigned kRingerAffected = (1U << 1) | (1U << kRing) | (1U << 5) | (1U << 7) | (1U << 8);
constexpr int kRingerSilent = 0;
constexpr int kRingerNormal = 2;
constexpr int kModeNormal = 0;
constexpr int kModeInCall = 2;
constexpr int kModeInCommunication = 3;
constexpr int kForceNone = 0;
constexpr int kForceSpeaker = 1;
constexpr int kForceBtSco = 3;
constexpr int kForceNoBtA2dp = 10;
constexpr int kAllowRingerModes = 1 << 1;
constexpr int kBluetoothAbsVolume = 1 << 6;
constexpr int kDefaultStream = std::numeric_limits<int>::min();
constexpr int kRccUnregistered = -1;

bool ValidStream(int stream) {
    return stream >= 0 && stream < static_cast<int>(Audio::max_volume.size());
}

int RescaleIndex(int index, std::size_t from, std::size_t to) {
    return (index * Audio::max_volume[to] + Audio::max_volume[from] / 2) / Audio::max_volume[from];
}

int StreamVolumeIndex(const Audio& audio, std::size_t stream) {
    const auto alias = Audio::volume_alias[stream];
    return (RescaleIndex(audio.volume_index[alias], alias, stream) + 5) / 10;
}

bool IsStreamMuted(const Audio& audio, std::size_t stream) {
    return !audio.client_mutes[stream].empty() ||
           (audio.ringer_mode != kRingerNormal && (kRingerAffected & (1U << stream)) != 0);
}

void SetClientMute(Audio& audio, std::size_t stream, dexvm::VmObjectRef token, bool mute) {
    auto& clients = audio.client_mutes[stream];
    if (mute) {
        ++clients[token];
    } else if (const auto client = clients.find(token); client != clients.end() && --client->second == 0) {
        clients.erase(client);
    }
}

void SetRingerMode(Audio& audio, int mode) {
    if (audio.ringer_mode != kRingerNormal && mode == kRingerNormal && audio.volume_index[kRing] == 0)
        audio.volume_index[kRing] = 10;
    audio.ringer_mode = mode;
}

bool IsMusicActive(const DexVmAndroidContext& context) {
    if (context.encoded_music && context.encoded_music->AnyPlaying()) return true;
    if (context.encoded_audio_playback && context.encoded_audio_playback->ActiveVoiceCount() > 0U) return true;
    return context.pcm_playback && std::ranges::any_of(context.audio_tracks, [&](const auto& entry) {
               return context.pcm_playback->PlayState(entry.second.player) == audio::OpenSlesPlayState::playing;
           });
}

int ActiveVolumeStream(const DexVmAndroidContext& context, int suggested) {
    const auto& audio = context.device_state.audio;
    if (audio.volume_control_stream != -1) return audio.volume_control_stream;
    if (audio.mode == kModeInCall || audio.mode == kModeInCommunication)
        return static_cast<int>(audio.force_use_for_comm == kForceBtSco ? kBluetoothSco : kVoiceCall);
    if (IsMusicActive(context)) return static_cast<int>(kMusic);
    return suggested == kDefaultStream ? static_cast<int>(kRing) : suggested;
}

void AdjustStreamVolume(DexVmAndroidContext& context, int stream, int direction, int flags) {
    if (direction < -1 || direction > 1)
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Invalid volume direction"};
    if (!ValidStream(stream)) throw dexvm::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Invalid audio stream"};
    if (flags & kBluetoothAbsVolume) return;  // The current output is not A2DP.
    auto& audio = context.device_state.audio;
    const auto stream_index = static_cast<std::size_t>(stream);
    const auto alias = Audio::volume_alias[stream_index];
    auto& index = audio.volume_index[alias];
    const auto step = RescaleIndex(10, stream_index, alias);
    bool adjust_index = true;
    if (alias == kRing || (flags & kAllowRingerModes)) {
        if (audio.ringer_mode == kRingerSilent) {
            if (direction == 1) SetRingerMode(audio, kRingerNormal);
            adjust_index = false;
        } else if (direction == -1 && index < step && audio.previous_ringer_adjustment != -1) {
            SetRingerMode(audio, kRingerSilent);
        }
        audio.previous_ringer_adjustment = direction;
    }
    if (adjust_index) index = std::clamp(index + direction * step, 0, 10 * Audio::max_volume[alias]);
    SyncAudioStreamGains(context);
}
}  // namespace
}  // namespace ogplay::runtime

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

void ReportAndroidAudioDecodeFailure(dx::Interpreter& vm, std::string_view operation,
                                     const audio::AudioDecodeFailure& failure) {
    if (auto* ledger = vm.Ledger()) {
        ledger->RecordUnimplemented(failure.codec.empty() ? "android.media.audio_container.unknown"
                                                          : "android.media.audio_codec." + failure.codec,
                                    0);
    }
    if (auto* logger = vm.Log()) {
        logger->Write(core::LogLevel::warn, "runtime.android.audio.decode", failure.message, {},
                      {{"operation", std::string(operation)}});
    }
}

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
            return dx::VmValue::Int(IsMusicActive(*context));
        },
        dx::kAccPublic | dx::kAccStatic | dx::kAccNative);
    constexpr auto flags = dx::kAccPublic | dx::kAccStatic | dx::kAccNative;
    using V = dx::VmValue;
    constexpr int kSpeaker = 0x2;
    constexpr int kBuiltInMic = std::bit_cast<int>(0x80000004U);
    builder.StaticMethod(
        "getDeviceConnectionState", "(ILjava/lang/String;)I",
        [](dx::IntrinsicContext& call) {
            const auto device = call.arguments[0].AsInt();
            return V::Int(device == kSpeaker || device == kBuiltInMic);
        },
        flags);
    builder.StaticMethod(
        "getDevicesForStream", "(I)I",
        [](dx::IntrinsicContext& call) {
            const auto stream = call.arguments[0].AsInt();
            return V::Int(stream >= 0 && stream < 10 ? kSpeaker : 0);
        },
        flags);
    builder.StaticMethod(
        "getPrimaryOutputSamplingRate", "()I",
        [context](dx::IntrinsicContext&) { return V::Int(static_cast<int>(context->native_output_sample_rate)); },
        flags);
    builder.StaticMethod(
        "getPrimaryOutputFrameCount", "()I",
        [context](dx::IntrinsicContext&) { return V::Int(static_cast<int>(context->native_output_buffer_frames)); },
        flags);
    builder.StaticMethod(
        "getStreamVolumeIndex", "(II)I",
        [context](dx::IntrinsicContext& call) {
            const auto stream = call.arguments[0].AsInt();
            return V::Int(ValidStream(stream)
                              ? StreamVolumeIndex(context->device_state.audio, static_cast<std::size_t>(stream))
                              : -1);
        },
        flags);
    builder.StaticMethod("getMasterVolume", "()F", [](dx::IntrinsicContext&) { return V::Float(1.0F); }, flags);
    builder.StaticMethod(
        "getParameters", "(Ljava/lang/String;)Ljava/lang/String;",
        [](dx::IntrinsicContext& call) { return MakeString(call, ""); }, flags);
    builder.StaticMethod(
        "setParameters", "(Ljava/lang/String;)I",
        [](dx::IntrinsicContext& call) {
            const auto parameters = call.arguments[0].ref;
            if (auto* logger = call.vm.Log(); logger && parameters.IsValid()) {
                const auto pairs = call.vm.StringUtf8(parameters);
                for (const auto pair : std::string_view(pairs) | std::views::split(';')) {
                    const std::string_view parameter(pair.begin(), pair.end());
                    const auto key = parameter.substr(0, parameter.find('='));
                    if (!key.empty())
                        logger->Write(core::LogLevel::warn, "runtime.android.audio",
                                      "Ignoring audio parameter " + std::string(key), {}, {},
                                      {.mode = core::RateLimitMode::first_and_count});
                }
            }
            return V::Int(0);
        },
        flags);
    builder.StaticMethod(
        "getOutputLatency", "(I)I",
        [context](dx::IntrinsicContext&) {
            const auto rate = context->native_output_sample_rate;
            const auto buffer = context->native_output_buffer_frames;
            const auto queued = context->native_output_target_queued_frames;
            if (!rate || !buffer || !queued) return V::Int(-1);
            const auto latency_ms = (static_cast<std::uint64_t>(buffer) + queued) * 1000 / rate;
            return V::Int(latency_ms <= INT32_MAX ? static_cast<int>(latency_ms) : -1);
        },
        flags);
    for (const auto& [name, desc] : std::array{
             std::pair{"isMicrophoneMuted", "()Z"},
             std::pair{"isSourceActive", "(I)Z"},
             std::pair{"isStreamActiveRemotely", "(II)Z"},
             std::pair{"getMasterMute", "()Z"},
             std::pair{"getForceUse", "(I)I"},
             std::pair{"checkAudioFlinger", "()I"},
         }) {
        builder.StaticMethod(name, desc, [](dx::IntrinsicContext&) { return V::Int(0); }, flags);
    }
    // The fixed routes and mix policy cannot be reconfigured through AudioSystem.
    for (const auto& [name, desc] : std::array{
             std::pair{"muteMicrophone", "(Z)I"},
             std::pair{"setDeviceConnectionState", "(IILjava/lang/String;)I"},
             std::pair{"setPhoneState", "(I)I"},
             std::pair{"setForceUse", "(II)I"},
             std::pair{"initStreamVolume", "(III)I"},
             std::pair{"setStreamVolumeIndex", "(III)I"},
             std::pair{"setMasterVolume", "(F)I"},
             std::pair{"setMasterMute", "(Z)I"},
             std::pair{"setLowRamDevice", "(Z)I"},
         }) {
        builder.StaticMethod(
            name, desc,
            [name](dx::IntrinsicContext& call) {
                if (auto* ledger = call.vm.Ledger())
                    ledger->RecordUnimplemented(std::string("dexvm.audio_system.") + name, 0);
                const std::string_view method{name};
                if (method == "setLowRamDevice") return V::Int(-38);  // INVALID_OPERATION
                return V::Int(1);                                     // AudioSystem.AUDIO_STATUS_ERROR
            },
            flags);
    }
    return std::move(builder).Build();
}

namespace {
std::size_t ServiceStream(dx::IntrinsicContext& call) {
    const auto stream = call.arguments[0].AsInt();
    if (!ValidStream(stream)) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Invalid audio stream"};
    return static_cast<std::size_t>(stream);
}

void DispatchAudioFocus(dx::Interpreter& vm, const SimulatedDeviceState::Audio::Focus& entry, int change) {
    if (!entry.dispatcher.IsValid()) return;
    const auto roots = vm.ProtectReferences(std::array{entry.dispatcher, entry.token});
    static_cast<void>(CallAndroidMethod(vm, entry.dispatcher, "dispatchAudioFocusChange", "(ILjava/lang/String;)V",
                                        {dx::VmValue::Int(change), dx::VmValue::Ref(vm.NewStringUtf8(entry.client))}));
}

void RemoveAudioFocus(dx::IntrinsicContext& call, const Context& context, std::size_t index, bool notify) {
    auto& stack = context->device_state.audio.focus;
    const auto id = call.vm.StringUtf8(dx::IntrinsicCall(call).NonNullRef(index, "clientId"));
    const auto entry = std::ranges::find(stack, id, &SimulatedDeviceState::Audio::Focus::client);
    if (entry == stack.end()) return;
    const auto was_top = std::next(entry) == stack.end();
    stack.erase(entry);
    if (notify && was_top && !stack.empty()) {
        stack.back().loss = 0;
        DispatchAudioFocus(call.vm, stack.back(), 1);
    }
}

void RecordUnsupportedAudioMethod(dx::IntrinsicContext& call, std::string_view signature) {
    if (auto* ledger = call.vm.Ledger())
        ledger->RecordUnimplemented(std::string("android.media.IAudioService.") + std::string(signature), 0);
}

void LogAudioRejection(dx::IntrinsicContext& call, std::string_view method, std::string_view reason) {
    if (auto* logger = call.vm.Log())
        logger->Write(core::LogLevel::warn, "runtime.android.audio", std::string(reason), {},
                      {{"method", std::string(method)}}, {.mode = core::RateLimitMode::first_and_count});
}

bool CanModifyAudioSettings(dx::IntrinsicContext& call, const Context& context, std::string_view method) {
    if (context->granted_permissions.contains("android.permission.MODIFY_AUDIO_SETTINGS")) return true;
    LogAudioRejection(call, method, "Missing MODIFY_AUDIO_SETTINGS");
    return false;
}

void SetCommForceUse(dx::IntrinsicContext& call, const Context& context, int value, std::string_view method) {
    if (!CanModifyAudioSettings(call, context, method)) return;
    auto& forced = context->device_state.audio.force_use_for_comm;
    if (call.arguments[0].AsInt() != 0) {
        forced = value;
    } else if (forced == value) {
        forced = kForceNone;
    }
}

dx::VmObjectRef MediaButtonTarget(dx::IntrinsicContext& call) {
    const auto intent = call.arguments[0].ref;
    if (!intent.IsValid()) return dx::VmObjectRef{};
    return CallAndroidMethod(call.vm, intent, "getTarget", "()Landroid/content/IIntentSender;").ref;
}

void DispatchMediaKey(dx::IntrinsicContext& call, const Context& context, std::string_view signature) {
    using V = dx::VmValue;
    const auto event = call.arguments[0].ref;
    if (!event.IsValid()) return;
    try {
        // Key codes accepted by API 19 MediaFocusControl.
        constexpr std::array media_keys{79, 85, 86, 87, 88, 89, 90, 91, 126, 127, 128, 129, 130, 222};
        const auto key = CallAndroidMethod(call.vm, event, "getKeyCode", "()I").AsInt();
        if (std::ranges::find(media_keys, key) == media_keys.end()) return;
        const auto& stack = context->device_state.audio.media_button_receivers;
        if (stack.empty() || !context->pending_intents.contains(stack.back().target.Value())) {
            RecordUnsupportedAudioMethod(call, signature);
            return;
        }
        const auto target = stack.back().target;
        const auto target_root = call.vm.ProtectReferences(std::array{target});
        const auto intent = NewPlatformObject(call.vm, "Landroid/content/Intent;", "(Ljava/lang/String;)V",
                                              {MakeString(call, "android.intent.action.MEDIA_BUTTON")});
        const auto intent_root = call.vm.ProtectReferences(std::array{intent});
        static_cast<void>(CallAndroidMethod(call.vm, intent, "putExtra",
                                            "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
                                            {MakeString(call, "android.intent.extra.KEY_EVENT"), V::Ref(event)}));
        // PendingIntent.send delegates here after resolving the MIME type. The
        // media button fill-in has neither a URI nor a type.
        const auto null = V::Ref(dx::VmObjectRef{});
        const auto result = CallAndroidMethod(
            call.vm, target, "send",
            "(ILandroid/content/Intent;Ljava/lang/String;Landroid/content/IIntentReceiver;Ljava/lang/String;)I",
            {V::Int(0), V::Ref(intent), null, null, null});
        if (result.AsInt() < 0) LogAudioRejection(call, signature, "Media button PendingIntent canceled");
    } catch (const dx::VmJavaThrow&) {
        LogAudioRejection(call, signature, "Media button delivery failed");
    }
}
}  // namespace

Decl Declare_local_MediaRouterService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalMediaRouterService;",
                                                    "Landroid/media/IMediaRouterService$Stub;");
    const auto client = [](dx::IntrinsicContext& call) {
        const auto object = call.arguments[0].ref;
        if (!object.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "client must not be null"};
        return object;
    };
    builder.VirtualMethod(
        "registerClientAsUser", "(Landroid/media/IMediaRouterClient;Ljava/lang/String;I)V",
        [context, client](dx::IntrinsicContext& call) {
            const auto token = client(call);
            const auto package = call.arguments[1].ref;
            if (!package.IsValid() || call.vm.StringUtf8(package) != context->package_name)
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "packageName must match the calling uid"};
            const auto user = call.arguments[2].AsInt();
            if (user != 0 && user != -2 && user != -3)
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "Cross-user media route access"};
            context->device_state.audio.media_router_clients.insert(token);
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("unregisterClient", "(Landroid/media/IMediaRouterClient;)V",
                          [context, client](dx::IntrinsicContext& call) {
                              context->device_state.audio.media_router_clients.erase(client(call));
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("getState", "(Landroid/media/IMediaRouterClient;)Landroid/media/MediaRouterClientState;",
                          [context, client](dx::IntrinsicContext& call) {
                              if (!context->device_state.audio.media_router_clients.contains(client(call)))
                                  return dx::VmValue::Ref(dx::VmObjectRef{});
                              // The default audio/video route belongs to ROM MediaRouter.
                              // The process-local service has no global route providers.
                              return dx::VmValue::Ref(
                                  NewPlatformObject(call.vm, "Landroid/media/MediaRouterClientState;", "()V", {}));
                          });
    const auto update = [client](dx::IntrinsicContext& call) {
        static_cast<void>(client(call));
        return dx::VmValue::Void();
    };
    builder.VirtualMethod("setDiscoveryRequest", "(Landroid/media/IMediaRouterClient;IZ)V", update);
    builder.VirtualMethod("setSelectedRoute", "(Landroid/media/IMediaRouterClient;Ljava/lang/String;Z)V", update);
    for (const auto* name : {"requestSetVolume", "requestUpdateVolume"})
        builder.VirtualMethod(
            name, "(Landroid/media/IMediaRouterClient;Ljava/lang/String;I)V", [client](dx::IntrinsicContext& call) {
                static_cast<void>(client(call));
                if (!call.arguments[1].ref.IsValid())
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "routeId must not be null"};
                return dx::VmValue::Void();
            });
    return std::move(builder).Build();
}

Decl Declare_local_AudioService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalAudioService;",
                                                    "Landroid/media/IAudioService$Stub;");
    builder.VirtualMethod("getRingerMode", "()I", [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(context->device_state.audio.ringer_mode);
    });
    builder.VirtualMethod("setRingerMode", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto mode = call.arguments[0].AsInt();
        if (mode < kRingerSilent || mode > kRingerNormal)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Invalid ringer mode"};
        SetRingerMode(context->device_state.audio, mode == kRingerNormal ? kRingerNormal : kRingerSilent);
        SyncAudioStreamGains(*context);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("getStreamMaxVolume", "(I)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(Audio::max_volume[ServiceStream(call)]);
    });
    builder.VirtualMethod("getStreamVolume", "(I)I", [context](dx::IntrinsicContext& call) {
        const auto stream = ServiceStream(call);
        const auto& audio = context->device_state.audio;
        return dx::VmValue::Int(IsStreamMuted(audio, stream) ? 0 : StreamVolumeIndex(audio, stream));
    });
    builder.VirtualMethod("getLastAudibleStreamVolume", "(I)I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(StreamVolumeIndex(context->device_state.audio, ServiceStream(call)));
    });
    builder.VirtualMethod("isStreamMute", "(I)Z", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(IsStreamMuted(context->device_state.audio, ServiceStream(call)));
    });
    builder.VirtualMethod("setStreamVolume", "(IIILjava/lang/String;)V", [context](dx::IntrinsicContext& call) {
        const auto stream = ServiceStream(call);
        const auto flags = call.arguments[2].AsInt();
        if (flags & kBluetoothAbsVolume) return dx::VmValue::Void();
        auto& audio = context->device_state.audio;
        const auto alias = Audio::volume_alias[stream];
        const auto index =
            RescaleIndex(10 * std::clamp(call.arguments[1].AsInt(), 0, Audio::max_volume[stream]), stream, alias);
        audio.volume_index[alias] = index;
        if (alias == kRing || (flags & kAllowRingerModes))
            SetRingerMode(audio, index == 0 ? kRingerSilent : kRingerNormal);
        SyncAudioStreamGains(*context);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setStreamMute", "(IZLandroid/os/IBinder;)V", [context](dx::IntrinsicContext& call) {
        const auto stream = call.arguments[0].AsInt();
        if (ValidStream(stream) && (kMuteAffected & (1U << stream))) {
            SetClientMute(context->device_state.audio, static_cast<std::size_t>(stream), call.arguments[2].ref,
                          call.arguments[1].AsInt() != 0);
            SyncAudioStreamGains(*context);
        }
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setStreamSolo", "(IZLandroid/os/IBinder;)V", [context](dx::IntrinsicContext& call) {
        const auto solo = static_cast<std::size_t>(call.arguments[0].AsInt());
        for (std::size_t stream = 0; stream < Audio::max_volume.size(); ++stream)
            if (stream != solo && (kMuteAffected & (1U << stream)))
                SetClientMute(context->device_state.audio, stream, call.arguments[2].ref,
                              call.arguments[1].AsInt() != 0);
        SyncAudioStreamGains(*context);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("adjustStreamVolume", "(IIILjava/lang/String;)V", [context](dx::IntrinsicContext& call) {
        AdjustStreamVolume(*context, call.arguments[0].AsInt(), call.arguments[1].AsInt(), call.arguments[2].AsInt());
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("adjustSuggestedStreamVolume", "(IIILjava/lang/String;)V",
                          [context](dx::IntrinsicContext& call) {
                              AdjustStreamVolume(*context, ActiveVolumeStream(*context, call.arguments[1].AsInt()),
                                                 call.arguments[0].AsInt(), call.arguments[2].AsInt());
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("adjustVolume", "(IILjava/lang/String;)V", [context](dx::IntrinsicContext& call) {
        AdjustStreamVolume(*context, ActiveVolumeStream(*context, kDefaultStream), call.arguments[0].AsInt(),
                           call.arguments[1].AsInt());
        return dx::VmValue::Void();
    });
    builder.VirtualMethod(
        "adjustLocalOrRemoteStreamVolume", "(IILjava/lang/String;)V", [context](dx::IntrinsicContext& call) {
            const auto direction = call.arguments[1].AsInt();
            if (direction < -1 || direction > 1) {
                LogAudioRejection(call, "adjustLocalOrRemoteStreamVolume", "Invalid volume direction");
            } else if (IsMusicActive(*context)) {
                AdjustStreamVolume(*context, static_cast<int>(kMusic), direction, 0);
            }
            return dx::VmValue::Void();
        });
    builder.VirtualMethod(
        "forceVolumeControlStream", "(ILandroid/os/IBinder;)V", [context](dx::IntrinsicContext& call) {
            auto& audio = context->device_state.audio;
            audio.volume_control_stream = call.arguments[0].AsInt();
            audio.volume_control_token = audio.volume_control_stream == -1 ? dx::VmObjectRef{} : call.arguments[1].ref;
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("getCurrentAudioFocus", "()I", [context](dx::IntrinsicContext&) {
        const auto& stack = context->device_state.audio.focus;
        return dx::VmValue::Int(stack.empty() ? 0 : stack.back().gain);
    });
    builder.VirtualMethod(
        "requestAudioFocus",
        "(IILandroid/os/IBinder;Landroid/media/IAudioFocusDispatcher;Ljava/lang/String;Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            const auto gain = call.arguments[1].AsInt();
            if (gain < 1 || gain > 4 || !call.arguments[2].ref.IsValid()) return dx::VmValue::Int(0);
            const auto client = call.vm.StringUtf8(dx::IntrinsicCall(call).NonNullRef(4, "clientId"));
            auto& stack = context->device_state.audio.focus;
            if (!stack.empty() && stack.back().client == client && stack.back().gain == gain)
                return dx::VmValue::Int(1);
            std::erase_if(stack, [&](const auto& entry) { return entry.client == client; });
            std::vector<Audio::Focus> changed;
            std::vector<dx::VmObjectRef> roots;
            const auto loss = gain == 1 ? -1 : gain == 3 ? -3 : -2;
            for (auto& entry : stack) {
                if (entry.loss && entry.loss >= loss) continue;
                entry.loss = loss;
                changed.push_back(entry);
                roots.push_back(entry.dispatcher);
            }
            stack.push_back({client, call.arguments[2].ref, call.arguments[3].ref, gain, 0});
            const auto retained = call.vm.ProtectReferences(roots);
            for (const auto& entry : changed) {
                const auto current = std::ranges::find(stack, entry.client, &Audio::Focus::client);
                if (current != stack.end() && current->loss == entry.loss)
                    DispatchAudioFocus(call.vm, entry, entry.loss);
            }
            return dx::VmValue::Int(1);
        });
    builder.VirtualMethod("abandonAudioFocus", "(Landroid/media/IAudioFocusDispatcher;Ljava/lang/String;)I",
                          [context](dx::IntrinsicContext& call) {
                              RemoveAudioFocus(call, context, 1, true);
                              return dx::VmValue::Int(1);
                          });
    builder.VirtualMethod("unregisterAudioFocusClient", "(Ljava/lang/String;)V", [context](dx::IntrinsicContext& call) {
        RemoveAudioFocus(call, context, 0, false);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod(
        "startWatchingRoutes", "(Landroid/media/IAudioRoutesObserver;)Landroid/media/AudioRoutesInfo;",
        [context](dx::IntrinsicContext& call) {
            context->device_state.audio.route_observers.insert(dx::IntrinsicCall(call).NonNullRef(0, "observer"));
            return dx::VmValue::Ref(NewPlatformObject(call.vm, "Landroid/media/AudioRoutesInfo;", "()V", {}));
        });
    for (const auto& [name, descriptor] : std::array{
             std::pair{"setRingtonePlayer", "(Landroid/media/IRingtonePlayer;)V"},
         }) {
        DenyServiceMethod(builder, name, descriptor, "REMOTE_AUDIO_PLAYBACK");
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"registerMediaButtonEventReceiverForCalls", "(Landroid/content/ComponentName;)V"},
             std::pair{"unregisterMediaButtonEventReceiverForCalls", "()V"},
         }) {
        builder.VirtualMethod(name, descriptor, [name](dx::IntrinsicContext& call) {
            LogAudioRejection(call, name, "Missing MODIFY_PHONE_STATE");
            return dx::VmValue::Void();
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getMasterMaxVolume", "()I"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Int(100); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getMasterStreamType", "()I"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Int(2); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getMasterVolume", "()I"},
             std::pair{"getLastAudibleMasterVolume", "()I"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Int(100); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getRemoteStreamMaxVolume", "()I"},
             std::pair{"getRemoteStreamVolume", "()I"},
             std::pair{"isCameraSoundForced", "()Z"},
             std::pair{"isMasterMute", "()Z"},
             std::pair{"shouldVibrate", "(I)Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getRingtonePlayer", "()Landroid/media/IRingtonePlayer;"},
         }) {
        builder.VirtualMethod(name, descriptor,
                              [](dx::IntrinsicContext&) { return dx::VmValue::Ref(dx::VmObjectRef{}); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"registerRemoteController",
                       "(Landroid/media/IRemoteControlDisplay;IILandroid/content/ComponentName;)Z"},
             std::pair{"loadSoundEffects", "()Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"unloadSoundEffects", "()V"},
             std::pair{"setMasterMute", "(ZILandroid/os/IBinder;)V"},
             std::pair{"setMasterVolume", "(IILjava/lang/String;)V"},
             std::pair{"adjustMasterVolume", "(IILjava/lang/String;)V"},
             std::pair{"setRemoteStreamVolume", "(I)V"},
             std::pair{"startBluetoothSco", "(Landroid/os/IBinder;I)V"},
             std::pair{"stopBluetoothSco", "(Landroid/os/IBinder;)V"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"setBluetoothA2dpDeviceConnectionState", "(Landroid/bluetooth/BluetoothDevice;I)I"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"avrcpSupportsAbsoluteVolume", "(Ljava/lang/String;Z)V"},
             std::pair{"setWiredDeviceConnectionState", "(IILjava/lang/String;)V"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"verifyX509CertChain", "(I[BLjava/lang/String;Ljava/lang/String;)I"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Int(5); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"dispatchMediaKeyEvent", "(Landroid/view/KeyEvent;)V"},
             std::pair{"dispatchMediaKeyEventUnderWakelock", "(Landroid/view/KeyEvent;)V"},
         }) {
        builder.VirtualMethod(name, descriptor,
                              [context, signature = std::string(name) + descriptor](dx::IntrinsicContext& call) {
                                  DispatchMediaKey(call, context, signature);
                                  return dx::VmValue::Void();
                              });
    }

    for (const auto& [name, descriptor] : std::array{
             std::pair{"playSoundEffect", "(I)V"},
             std::pair{"playSoundEffectVolume", "(IF)V"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext& call) {
            if (const auto effect = call.arguments[0].AsInt(); effect >= 0 && effect < 10)
                RecordUiDegraded(call.vm, "System UI sound effect");
            return dx::VmValue::Void();
        });
    }

    builder.VirtualMethod("isLocalOrRemoteMusicActive", "()Z",
                          [context](dx::IntrinsicContext&) { return dx::VmValue::Int(IsMusicActive(*context)); });
    builder.VirtualMethod("getMode", "()I", [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(context->device_state.audio.mode);
    });
    builder.VirtualMethod("setMode", "(ILandroid/os/IBinder;)V", [context](dx::IntrinsicContext& call) {
        if (!CanModifyAudioSettings(call, context, "setMode")) return dx::VmValue::Void();
        constexpr int kModeCurrent = -1;
        const auto requested = call.arguments[0].AsInt();
        const auto token = call.arguments[1].ref;
        if (requested < kModeCurrent || requested > kModeInCommunication || !token.IsValid())
            return dx::VmValue::Void();
        auto& audio = context->device_state.audio;
        if (requested != kModeCurrent) audio.mode = requested;
        audio.mode_token = audio.mode == kModeNormal ? dx::VmObjectRef{} : token;
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("isSpeakerphoneOn", "()Z", [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(context->device_state.audio.force_use_for_comm == kForceSpeaker);
    });
    builder.VirtualMethod("setSpeakerphoneOn", "(Z)V", [context](dx::IntrinsicContext& call) {
        SetCommForceUse(call, context, kForceSpeaker, "setSpeakerphoneOn");
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("isBluetoothScoOn", "()Z", [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(context->device_state.audio.force_use_for_comm == kForceBtSco);
    });
    builder.VirtualMethod("setBluetoothScoOn", "(Z)V", [context](dx::IntrinsicContext& call) {
        SetCommForceUse(call, context, kForceBtSco, "setBluetoothScoOn");
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("isBluetoothA2dpOn", "()Z", [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(context->device_state.audio.force_use_for_media != kForceNoBtA2dp);
    });
    builder.VirtualMethod("setBluetoothA2dpOn", "(Z)V", [context](dx::IntrinsicContext& call) {
        context->device_state.audio.force_use_for_media = call.arguments[0].AsInt() ? kForceNone : kForceNoBtA2dp;
        return dx::VmValue::Void();
    });
    // Without a vibrator API 19 reports VIBRATE_SETTING_OFF and ignores updates.
    builder.VirtualMethod("getVibrateSetting", "(I)I", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.VirtualMethod("setVibrateSetting", "(II)V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.VirtualMethod("registerMediaButtonIntent",
                          "(Landroid/app/PendingIntent;Landroid/content/ComponentName;Landroid/os/IBinder;)V",
                          [context](dx::IntrinsicContext& call) {
                              const auto target = MediaButtonTarget(call);
                              if (!target.IsValid()) return dx::VmValue::Void();
                              auto& audio = context->device_state.audio;
                              auto& stack = audio.media_button_receivers;
                              if (!stack.empty() && stack.back().target == target) return dx::VmValue::Void();
                              const auto existing = std::ranges::find(
                                  stack, target, &SimulatedDeviceState::Audio::MediaButtonReceiver::target);
                              if (existing == stack.end()) {
                                  stack.push_back({call.arguments[0].ref, target, call.arguments[1].ref,
                                                   call.arguments[2].ref, dx::VmObjectRef{}, audio.next_rcc_id++});
                              } else {
                                  std::rotate(existing, existing + 1, stack.end());
                              }
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("unregisterMediaButtonIntent", "(Landroid/app/PendingIntent;)V",
                          [context](dx::IntrinsicContext& call) {
                              const auto target = MediaButtonTarget(call);
                              auto& stack = context->device_state.audio.media_button_receivers;
                              std::erase_if(stack, [target](const auto& entry) { return entry.target == target; });
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("registerRemoteControlClient",
                          "(Landroid/app/PendingIntent;Landroid/media/IRemoteControlClient;Ljava/lang/String;)I",
                          [context](dx::IntrinsicContext& call) {
                              const auto target = MediaButtonTarget(call);
                              auto& stack = context->device_state.audio.media_button_receivers;
                              const auto entry = std::ranges::find(stack, target, &Audio::MediaButtonReceiver::target);
                              if (entry == stack.end()) return dx::VmValue::Int(kRccUnregistered);
                              entry->remote_control_client = call.arguments[1].ref;
                              return dx::VmValue::Int(entry->remote_control_client.IsValid() ? entry->rcc_id
                                                                                             : kRccUnregistered);
                          });
    builder.VirtualMethod("unregisterRemoteControlClient",
                          "(Landroid/app/PendingIntent;Landroid/media/IRemoteControlClient;)V",
                          [context](dx::IntrinsicContext& call) {
                              const auto target = MediaButtonTarget(call);
                              auto& stack = context->device_state.audio.media_button_receivers;
                              const auto entry = std::ranges::find(stack, target, &Audio::MediaButtonReceiver::target);
                              if (entry != stack.end() && entry->remote_control_client == call.arguments[1].ref)
                                  entry->remote_control_client = dx::VmObjectRef{};
                              return dx::VmValue::Void();
                          });
    RejectUnsupportedServiceMethod(builder, "android.media.IAudioService", "registerRemoteControlDisplay",
                                   "(Landroid/media/IRemoteControlDisplay;II)Z");
    for (const auto& [name, descriptor] : std::array{
             std::pair{"registerRemoteVolumeObserverForRcc", "(ILandroid/media/IRemoteVolumeObserver;)V"},
             std::pair{"setPlaybackInfoForRcc", "(III)V"},
             std::pair{"setPlaybackStateForRcc", "(IIJF)V"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"reloadAudioSettings", "()V"},
             std::pair{"remoteControlDisplayUsesBitmapSize", "(Landroid/media/IRemoteControlDisplay;II)V"},
             std::pair{"remoteControlDisplayWantsPlaybackPositionSync", "(Landroid/media/IRemoteControlDisplay;Z)V"},
             std::pair{"setRemoteControlClientPlaybackPosition", "(IJ)V"},
             std::pair{"unregisterRemoteControlDisplay", "(Landroid/media/IRemoteControlDisplay;)V"},
             std::pair{"updateRemoteControlClientMetadata", "(IILandroid/media/Rating;)V"},
         }) {
        builder.VirtualMethod(name, descriptor,
                              [signature = std::string(name) + descriptor](dx::IntrinsicContext& call) {
                                  RecordUnsupportedAudioMethod(call, signature);
                                  return dx::VmValue::Void();
                              });
    }
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

namespace {
void ApplyAudioStreamPolicy(DexVmAndroidContext& context, std::int32_t stream) {
    const auto gain = context.stream_gain[static_cast<std::size_t>(stream)];
    if (context.pcm_playback) context.pcm_playback->SetStreamGain(stream, gain);
    if (context.encoded_audio_playback) context.encoded_audio_playback->SetStreamGain(stream, gain);
    if (context.encoded_music) context.encoded_music->SetStreamGain(stream, gain);
}
}  // namespace

void SyncAudioStreamGains(DexVmAndroidContext& context) {
    const auto& audio = context.device_state.audio;
    std::scoped_lock lock(context.audio_policy_mutex);
    for (std::size_t stream = 0; stream < context.stream_gain.size(); ++stream) {
        context.stream_gain[stream] =
            IsStreamMuted(audio, stream)
                ? 0.0F
                : static_cast<float>(StreamVolumeIndex(audio, stream)) / Audio::max_volume[stream];
        ApplyAudioStreamPolicy(context, static_cast<std::int32_t>(stream));
    }
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
        gains = context.stream_gain;
    }
    std::scoped_lock lock(context.video_views_mutex);
    std::size_t contributed = 0;
    for (auto& [handle, state] : context.video_views) {
        if (state.player == nullptr || !state.playing) continue;
        try {
            const auto stream = static_cast<std::size_t>(state.audio_stream);
            if (MixOneVideoView(state, interleaved_stereo, output_rate, gains[stream < gains.size() ? stream : 3U]))
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
            found->second.QueueError();
        }
        if (const auto event = std::exchange(found->second.error_event_pending, std::nullopt)) {
            if (const auto error =
                    post("Landroid/media/MediaPlayer;", found->second.jni_weak, 100, event->what, event->extra);
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
            bool video_failure = false;
            if (status == audio::EncodedMusicMixer::PrepareStatus::failed || !found->second.display_window.IsNull()) {
                try {
                    if (PrepareAndroidMediaVideo(context, handle)) continue;
                } catch (const video::VideoCodecUnsupportedError& error) {
                    video_failure = true;
                    if (auto* ledger = vm.Ledger())
                        ledger->RecordUnimplemented("android.video.codec." + error.codec(), 0);
                    if (auto* logger = vm.Log())
                        logger->Write(core::LogLevel::warn, "runtime.video.prepare", error.what());
                } catch (const dexvm::VmJavaThrow& error) {
                    video_failure = true;
                    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.media.video_prepare", 0);
                    if (auto* logger = vm.Log())
                        logger->Write(core::LogLevel::warn, "runtime.video.prepare", error.message);
                }
            }
            if (found->second.phase == Phase::error) {
                if (!video_failure && context.encoded_music) {
                    if (const auto failure = context.encoded_music->PrepareFailure(found->second.music))
                        android_intrinsics::ReportAndroidAudioDecodeFailure(vm, "MediaPlayer.prepareAsync", *failure);
                }
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
            const auto now = context.UptimeMillis();
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

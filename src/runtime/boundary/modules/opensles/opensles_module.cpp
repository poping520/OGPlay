#include "runtime/boundary/modules/opensles/opensles_module.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/memory/address_space.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/core/boundary_binding.h"
#include "runtime/boundary/modules/opensles/opensles_abi.h"

namespace ogplay::runtime {
namespace {

constexpr std::uint32_t kSuccess = 0U;
constexpr std::uint32_t kPreconditionsViolated = 1U;
constexpr std::uint32_t kParameterInvalid = 2U;
constexpr std::uint32_t kMemoryFailure = 3U;
constexpr std::uint32_t kBufferInsufficient = 7U;
constexpr std::uint32_t kContentUnsupported = 9U;
constexpr std::uint32_t kFeatureUnsupported = 12U;
constexpr std::uint32_t kObjectUnrealized = 1U;
constexpr std::uint32_t kObjectRealized = 2U;
constexpr std::uint32_t kPlayStopped = 1U;
constexpr std::uint32_t kPlayPaused = 2U;
constexpr std::uint32_t kPlayPlaying = 3U;
constexpr std::uint32_t kTimeUnknown = 0xffffffffU;
constexpr std::uint32_t kObjectIdEngine = 0x1001U;
constexpr std::uint32_t kObjectIdAudioPlayer = 0x1004U;
constexpr std::uint32_t kObjectIdOutputMix = 0x1009U;
constexpr std::uint32_t kLocatorOutputMix = 4U;
constexpr std::uint32_t kLocatorBufferQueue = 6U;
constexpr std::uint32_t kLocatorAndroidSimpleBufferQueue = 0x800007bdU;
constexpr std::uint32_t kDataFormatPcm = 2U;
constexpr std::uint32_t kByteOrderLittleEndian = 2U;
constexpr std::size_t kObjectStride = 0x40U;
constexpr std::size_t kMaximumObjects = 4096U;

enum class ObjectKind : std::uint8_t { engine, output_mix, audio_player };

[[nodiscard]] std::optional<audio::OpenSlesPlayState> DecodePlayState(
    const std::uint32_t state) {
    switch (state) {
        case kPlayStopped: return audio::OpenSlesPlayState::stopped;
        case kPlayPaused: return audio::OpenSlesPlayState::paused;
        case kPlayPlaying: return audio::OpenSlesPlayState::playing;
        default: return std::nullopt;
    }
}

[[nodiscard]] std::uint32_t EncodePlayState(
    const audio::OpenSlesPlayState state) {
    switch (state) {
        case audio::OpenSlesPlayState::stopped: return kPlayStopped;
        case audio::OpenSlesPlayState::paused: return kPlayPaused;
        case audio::OpenSlesPlayState::playing: return kPlayPlaying;
    }
    return kPlayStopped;
}

}  // namespace

class OpenSlesModule::Impl final {
public:
    Impl(BoundaryCallServices& calls, audio::OpenSlesPcmMixer& mixer)
        : calls_(calls), mixer_(mixer) {}

    void MapGuestObjectArena() {
        calls_.address_space.Map(
            {kOpenSlesObjectArenaBegin, kOpenSlesObjectArenaBytes},
            memory::PageProtection::read | memory::PageProtection::write);
    }
    std::vector<audio::OpenSlesConsumedBuffer> Mix(
        const std::span<std::int16_t> output, const std::uint32_t output_rate) {
        return mixer_.MixAdditiveStereoPcm16(output, output_rate);
    }

    std::uint32_t CreateEngine(const A32CallFrame& call) {
        if (call.Argument(0) == 0U) {
            return kParameterInvalid;
        }
        Write32(call.Argument(0), 0U, call.ThreadId());
        if (call.Argument(1) > 16U ||
            (call.Argument(1) != 0U && call.Argument(2) == 0U)) {
            return kParameterInvalid;
        }
        for (std::uint32_t index = 0; index < call.Argument(1); ++index) {
            const auto feature = Read32(call.Argument(2) + index * 8U,
                                        call.ThreadId());
            if (feature != 1U && feature != 2U) return kParameterInvalid;
        }
        const auto interfaces = RequestedInterfaces(
            call.Argument(3), call.Argument(4), call.Argument(5),
            {"SL_IID_OBJECT", "SL_IID_ENGINE"}, call.ThreadId());
        if (!interfaces.has_value()) return kFeatureUnsupported;
        std::scoped_lock lock(mutex_);
        auto& engine = Allocate(ObjectKind::engine, *interfaces, call.ThreadId());
        Write32(call.Argument(0), engine.base.Value(), call.ThreadId());
        return kSuccess;
    }
    std::uint32_t QueryNumEngineInterfaces(const A32CallFrame& call) {
        if (call.Argument(0) == 0U) return kParameterInvalid;
        Write32(call.Argument(0), 2U, call.ThreadId());
        return kSuccess;
    }
    std::uint32_t QueryEngineInterface(const A32CallFrame& call) {
        static constexpr std::array names{"SL_IID_OBJECT", "SL_IID_ENGINE"};
        if (call.Argument(0) >= names.size() || call.Argument(1) == 0U) {
            return kParameterInvalid;
        }
        Write32(call.Argument(1), IidAddress(names[call.Argument(0)]).Value(),
                call.ThreadId());
        return kSuccess;
    }

    std::uint32_t ObjectRealize(const A32CallFrame& call) {
        if (call.Argument(1) > 1U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        Require(call.Argument(0)).state = kObjectRealized;
        return kSuccess;
    }
    std::uint32_t ObjectResume(const A32CallFrame& call) {
        if (call.Argument(1) > 1U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        return Require(call.Argument(0)).state == kObjectRealized
                   ? kSuccess
                   : kPreconditionsViolated;
    }
    std::uint32_t ObjectGetState(const A32CallFrame& call) {
        if (call.Argument(1) == 0U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        Write32(call.Argument(1), Require(call.Argument(0)).state,
                call.ThreadId());
        return kSuccess;
    }
    std::uint32_t ObjectGetInterface(const A32CallFrame& call) {
        if (call.Argument(1) == 0U || call.Argument(2) == 0U) {
            return kParameterInvalid;
        }
        std::scoped_lock lock(mutex_);
        auto& object = Require(call.Argument(0));
        if (object.state != kObjectRealized) return kPreconditionsViolated;
        const auto iid = IidName(memory::GuestAddress{call.Argument(1)});
        if (!iid.has_value() || !object.interfaces.contains(*iid)) {
            return kFeatureUnsupported;
        }
        Write32(call.Argument(2), InterfaceHandle(object, *iid).Value(),
                call.ThreadId());
        return kSuccess;
    }
    std::uint32_t ObjectRegisterCallback(const A32CallFrame& call) {
        std::scoped_lock lock(mutex_);
        auto& object = Require(call.Argument(0));
        object.object_callback = call.Argument(1);
        object.object_context = call.Argument(2);
        return kSuccess;
    }
    std::uint32_t ObjectAbortAsync(const A32CallFrame& call) {
        std::scoped_lock lock(mutex_);
        static_cast<void>(Require(call.Argument(0)));
        return kSuccess;
    }
    std::uint32_t ObjectDestroy(const A32CallFrame& call) {
        std::scoped_lock lock(mutex_);
        const auto found = objects_.find(ObjectKey(call.Argument(0)));
        if (found == objects_.end()) {
            throw std::runtime_error("stale OpenSL object handle");
        }
        if (found->second.player.has_value()) {
            mixer_.DestroyPlayer(*found->second.player);
        }
        std::array<std::byte, kObjectStride> zero{};
        calls_.address_space.Write(found->second.base, zero, call.ThreadId());
        objects_.erase(found);
        return kSuccess;
    }
    std::uint32_t ObjectSetPriority(const A32CallFrame& call) {
        if (call.Argument(2) > 1U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        auto& object = Require(call.Argument(0));
        object.priority = std::bit_cast<std::int32_t>(call.Argument(1));
        object.preemptable = call.Argument(2) != 0U;
        return kSuccess;
    }
    std::uint32_t ObjectGetPriority(const A32CallFrame& call) {
        if (call.Argument(1) == 0U || call.Argument(2) == 0U) {
            return kParameterInvalid;
        }
        std::scoped_lock lock(mutex_);
        const auto& object = Require(call.Argument(0));
        Write32(call.Argument(1), std::bit_cast<std::uint32_t>(object.priority),
                call.ThreadId());
        Write32(call.Argument(2), object.preemptable ? 1U : 0U,
                call.ThreadId());
        return kSuccess;
    }
    std::uint32_t ObjectSetLossOfControl(const A32CallFrame& call) {
        std::scoped_lock lock(mutex_);
        static_cast<void>(Require(call.Argument(0)));
        return call.Argument(1) == 0U && call.Argument(3) <= 1U
                   ? kSuccess
                   : kFeatureUnsupported;
    }

    std::uint32_t EngineCreateOutputMix(const A32CallFrame& call) {
        if (call.Argument(1) == 0U) return kParameterInvalid;
        Write32(call.Argument(1), 0U, call.ThreadId());
        const auto interfaces = RequestedInterfaces(
            call.Argument(2), call.Argument(3), call.Argument(4),
            {"SL_IID_OBJECT"}, call.ThreadId());
        if (!interfaces.has_value()) return kFeatureUnsupported;
        std::scoped_lock lock(mutex_);
        const auto& engine = RequireKind(call.Argument(0), ObjectKind::engine);
        if (engine.state != kObjectRealized) return kPreconditionsViolated;
        auto& mix = Allocate(ObjectKind::output_mix, *interfaces, call.ThreadId());
        Write32(call.Argument(1), mix.base.Value(), call.ThreadId());
        return kSuccess;
    }
    std::uint32_t EngineCreateAudioPlayer(const A32CallFrame& call) {
        if (call.Argument(1) == 0U || call.Argument(2) == 0U ||
            call.Argument(3) == 0U) {
            return kParameterInvalid;
        }
        Write32(call.Argument(1), 0U, call.ThreadId());
        const auto source_locator = Read32(call.Argument(2), call.ThreadId());
        const auto source_format = Read32(call.Argument(2) + 4U, call.ThreadId());
        const auto sink_locator = Read32(call.Argument(3), call.ThreadId());
        if (source_locator == 0U || source_format == 0U || sink_locator == 0U) {
            return kParameterInvalid;
        }
        const auto locator_type = Read32(source_locator, call.ThreadId());
        if (locator_type != kLocatorBufferQueue &&
            locator_type != kLocatorAndroidSimpleBufferQueue) {
            return kContentUnsupported;
        }
        const auto capacity = Read32(source_locator + 4U, call.ThreadId());
        if (capacity == 0U || capacity > 255U) return kParameterInvalid;
        if (Read32(source_format, call.ThreadId()) != kDataFormatPcm) {
            return kContentUnsupported;
        }
        const auto channels = Read32(source_format + 4U, call.ThreadId());
        const auto milli_hz = Read32(source_format + 8U, call.ThreadId());
        const auto bits = Read32(source_format + 12U, call.ThreadId());
        const auto container = Read32(source_format + 16U, call.ThreadId());
        const auto endian = Read32(source_format + 24U, call.ThreadId());
        if ((channels != 1U && channels != 2U) ||
            (bits != 8U && bits != 16U) || container != bits ||
            milli_hz == 0U || milli_hz % 1000U != 0U ||
            (bits == 16U && endian != kByteOrderLittleEndian)) {
            return kContentUnsupported;
        }
        if (Read32(sink_locator, call.ThreadId()) != kLocatorOutputMix) {
            return kContentUnsupported;
        }
        const auto mix_handle = Read32(sink_locator + 4U, call.ThreadId());
        const auto interfaces = RequestedInterfaces(
            call.Argument(4), call.Argument(5), call.Argument(6),
            {"SL_IID_OBJECT", "SL_IID_PLAY"}, call.ThreadId());
        if (!interfaces.has_value()) return kFeatureUnsupported;
        auto exposed = *interfaces;
        exposed.insert(locator_type == kLocatorBufferQueue
                           ? "SL_IID_BUFFERQUEUE"
                           : "SL_IID_ANDROIDSIMPLEBUFFERQUEUE");
        std::scoped_lock lock(mutex_);
        const auto& engine = RequireKind(call.Argument(0), ObjectKind::engine);
        const auto& mix = RequireKind(mix_handle, ObjectKind::output_mix);
        if (engine.state != kObjectRealized || mix.state != kObjectRealized) {
            return kPreconditionsViolated;
        }
        const audio::OpenSlesPcmFormat format{
            milli_hz / 1000U, static_cast<std::uint8_t>(channels),
            static_cast<std::uint8_t>(bits)};
        const auto player_id = mixer_.CreatePlayer(
            format, static_cast<std::uint8_t>(capacity));
        try {
            auto& player = Allocate(ObjectKind::audio_player, exposed,
                                    call.ThreadId());
            player.player = player_id;
            player.sample_rate = format.sample_rate;
            player.queue_capacity = capacity;
            Write32(call.Argument(1), player.base.Value(), call.ThreadId());
        } catch (...) {
            mixer_.DestroyPlayer(player_id);
            throw;
        }
        return kSuccess;
    }
    std::uint32_t EngineQueryNumInterfaces(const A32CallFrame& call) {
        if (call.Argument(2) == 0U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        static_cast<void>(RequireKind(call.Argument(0), ObjectKind::engine));
        const auto names = ObjectInterfaces(call.Argument(1));
        if (names.empty()) return kParameterInvalid;
        Write32(call.Argument(2), static_cast<std::uint32_t>(names.size()),
                call.ThreadId());
        return kSuccess;
    }
    std::uint32_t EngineQueryInterface(const A32CallFrame& call) {
        if (call.Argument(3) == 0U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        static_cast<void>(RequireKind(call.Argument(0), ObjectKind::engine));
        const auto names = ObjectInterfaces(call.Argument(1));
        if (call.Argument(2) >= names.size()) return kParameterInvalid;
        Write32(call.Argument(3), IidAddress(names[call.Argument(2)]).Value(),
                call.ThreadId());
        return kSuccess;
    }
    std::uint32_t EngineQueryNumExtensions(const A32CallFrame& call) {
        if (call.Argument(1) == 0U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        static_cast<void>(RequireKind(call.Argument(0), ObjectKind::engine));
        Write32(call.Argument(1), 0U, call.ThreadId());
        return kSuccess;
    }
    std::uint32_t EngineIsExtensionSupported(const A32CallFrame& call) {
        if (call.Argument(1) == 0U || call.Argument(2) == 0U) {
            return kParameterInvalid;
        }
        std::scoped_lock lock(mutex_);
        static_cast<void>(RequireKind(call.Argument(0), ObjectKind::engine));
        Write32(call.Argument(2), 0U, call.ThreadId());
        return kSuccess;
    }

    std::uint32_t OutputMixGetDevices(const A32CallFrame& call) {
        if (call.Argument(1) == 0U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        static_cast<void>(RequireKind(call.Argument(0), ObjectKind::output_mix));
        if (call.Argument(2) != 0U) {
            if (std::bit_cast<std::int32_t>(
                    Read32(call.Argument(1), call.ThreadId())) < 1) {
                Write32(call.Argument(1), 1U, call.ThreadId());
                return kBufferInsufficient;
            }
            Write32(call.Argument(2), 0xfffffffeU, call.ThreadId());
        }
        Write32(call.Argument(1), 1U, call.ThreadId());
        return kSuccess;
    }
    std::uint32_t OutputMixRegisterCallback(const A32CallFrame& call) {
        std::scoped_lock lock(mutex_);
        auto& object = RequireKind(call.Argument(0), ObjectKind::output_mix);
        object.mix_callback = call.Argument(1);
        object.mix_context = call.Argument(2);
        return kSuccess;
    }
    std::uint32_t OutputMixReRoute(const A32CallFrame& call) {
        if (call.Argument(1) != 1U || call.Argument(2) == 0U) {
            return kParameterInvalid;
        }
        std::scoped_lock lock(mutex_);
        static_cast<void>(RequireKind(call.Argument(0), ObjectKind::output_mix));
        return Read32(call.Argument(2), call.ThreadId()) == 0xfffffffeU
                   ? kSuccess
                   : kParameterInvalid;
    }

    std::uint32_t PlaySetState(const A32CallFrame& call) {
        const auto state = DecodePlayState(call.Argument(1));
        if (!state.has_value()) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        auto& object = RequirePlayer(call.Argument(0));
        if (object.state != kObjectRealized) return kPreconditionsViolated;
        mixer_.SetPlayState(*object.player, *state);
        return kSuccess;
    }
    std::uint32_t PlayGetState(const A32CallFrame& call) {
        if (call.Argument(1) == 0U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        const auto& object = RequirePlayer(call.Argument(0));
        Write32(call.Argument(1), EncodePlayState(mixer_.PlayState(*object.player)),
                call.ThreadId());
        return kSuccess;
    }
    std::uint32_t PlayGetDuration(const A32CallFrame& call) {
        if (call.Argument(1) == 0U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        static_cast<void>(RequirePlayer(call.Argument(0)));
        Write32(call.Argument(1), kTimeUnknown, call.ThreadId());
        return kSuccess;
    }
    std::uint32_t PlayGetPosition(const A32CallFrame& call) {
        if (call.Argument(1) == 0U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        const auto& object = RequirePlayer(call.Argument(0));
        Write32(call.Argument(1), mixer_.PositionMillis(*object.player),
                call.ThreadId());
        return kSuccess;
    }
    std::uint32_t PlayRegisterCallback(const A32CallFrame& call) {
        std::scoped_lock lock(mutex_);
        auto& object = RequirePlayer(call.Argument(0));
        object.play_callback = call.Argument(1);
        object.play_context = call.Argument(2);
        return kSuccess;
    }
    std::uint32_t PlaySetMask(const A32CallFrame& call) {
        constexpr std::uint32_t kKnownMask = 0x1fU;
        if ((call.Argument(1) & ~kKnownMask) != 0U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        RequirePlayer(call.Argument(0)).play_mask = call.Argument(1);
        return kSuccess;
    }
    std::uint32_t PlayGetMask(const A32CallFrame& call) {
        return WritePlayer32(call, [](const Object& object) {
            return object.play_mask;
        });
    }
    std::uint32_t PlaySetMarker(const A32CallFrame& call) {
        std::scoped_lock lock(mutex_);
        RequirePlayer(call.Argument(0)).marker = call.Argument(1);
        return kSuccess;
    }
    std::uint32_t PlayClearMarker(const A32CallFrame& call) {
        std::scoped_lock lock(mutex_);
        RequirePlayer(call.Argument(0)).marker.reset();
        return kSuccess;
    }
    std::uint32_t PlayGetMarker(const A32CallFrame& call) {
        return WritePlayer32(call, [](const Object& object) {
            return object.marker.value_or(kTimeUnknown);
        });
    }
    std::uint32_t PlaySetUpdatePeriod(const A32CallFrame& call) {
        if (call.Argument(1) == 0U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        RequirePlayer(call.Argument(0)).update_period = call.Argument(1);
        return kSuccess;
    }
    std::uint32_t PlayGetUpdatePeriod(const A32CallFrame& call) {
        return WritePlayer32(call, [](const Object& object) {
            return object.update_period;
        });
    }

    std::uint32_t BufferQueueEnqueue(const A32CallFrame& call) {
        if (call.Argument(1) == 0U || call.Argument(2) == 0U) {
            return kParameterInvalid;
        }
        std::scoped_lock lock(mutex_);
        auto& object = RequirePlayer(call.Argument(0));
        if (object.state != kObjectRealized) return kPreconditionsViolated;
        if (mixer_.QueueState(*object.player).count >= object.queue_capacity) {
            return kBufferInsufficient;
        }
        std::vector<std::byte> bytes(call.Argument(2));
        calls_.address_space.Read(memory::GuestAddress{call.Argument(1)}, bytes,
                                  call.ThreadId());
        try {
            return mixer_.Enqueue(*object.player, bytes) ? kSuccess
                                                        : kBufferInsufficient;
        } catch (const std::invalid_argument&) {
            return kParameterInvalid;
        } catch (const std::length_error&) {
            return kMemoryFailure;
        }
    }
    std::uint32_t BufferQueueClear(const A32CallFrame& call) {
        std::scoped_lock lock(mutex_);
        auto& object = RequirePlayer(call.Argument(0));
        mixer_.Clear(*object.player);
        return kSuccess;
    }
    std::uint32_t BufferQueueGetState(const A32CallFrame& call) {
        if (call.Argument(1) == 0U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        const auto& object = RequirePlayer(call.Argument(0));
        const auto state = mixer_.QueueState(*object.player);
        Write32(call.Argument(1), state.count, call.ThreadId());
        Write32(call.Argument(1) + 4U, state.play_index, call.ThreadId());
        return kSuccess;
    }
    std::uint32_t BufferQueueRegisterCallback(const A32CallFrame& call) {
        std::scoped_lock lock(mutex_);
        auto& object = RequirePlayer(call.Argument(0));
        object.queue_callback = call.Argument(1);
        object.queue_context = call.Argument(2);
        return kSuccess;
    }

    std::uint32_t VolumeSetLevel(const A32CallFrame& call) {
        const auto level = static_cast<std::int16_t>(call.Argument(1));
        if (level < -9600 || level > 0) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        auto& object = RequirePlayer(call.Argument(0));
        object.volume = level;
        mixer_.SetVolume(*object.player, level);
        return kSuccess;
    }
    std::uint32_t VolumeGetLevel(const A32CallFrame& call) {
        return WritePlayer16(call, [](const Object& object) { return object.volume; });
    }
    std::uint32_t VolumeGetMaxLevel(const A32CallFrame& call) {
        return WritePlayer16(call, [](const Object&) { return std::int16_t{}; });
    }
    std::uint32_t VolumeSetMute(const A32CallFrame& call) {
        if (call.Argument(1) > 1U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        auto& object = RequirePlayer(call.Argument(0));
        object.mute = call.Argument(1) != 0U;
        mixer_.SetMute(*object.player, object.mute);
        return kSuccess;
    }
    std::uint32_t VolumeGetMute(const A32CallFrame& call) {
        return WritePlayer32(call, [](const Object& object) {
            return object.mute ? 1U : 0U;
        });
    }
    std::uint32_t VolumeEnableStereo(const A32CallFrame& call) {
        if (call.Argument(1) > 1U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        RequirePlayer(call.Argument(0)).stereo_enabled = call.Argument(1) != 0U;
        return kSuccess;
    }
    std::uint32_t VolumeIsStereoEnabled(const A32CallFrame& call) {
        return WritePlayer32(call, [](const Object& object) {
            return object.stereo_enabled ? 1U : 0U;
        });
    }
    std::uint32_t VolumeSetStereo(const A32CallFrame& call) {
        const auto position = static_cast<std::int16_t>(call.Argument(1));
        if (position < -1000 || position > 1000) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        auto& object = RequirePlayer(call.Argument(0));
        if (!object.stereo_enabled) return kPreconditionsViolated;
        object.stereo_position = position;
        mixer_.SetStereoPosition(*object.player, position);
        return kSuccess;
    }
    std::uint32_t VolumeGetStereo(const A32CallFrame& call) {
        return WritePlayer16(call, [](const Object& object) {
            return object.stereo_position;
        });
    }

    std::uint32_t Unsupported(const A32CallFrame& call) {
        std::scoped_lock lock(mutex_);
        static_cast<void>(Require(call.Argument(0)));
        return kFeatureUnsupported;
    }
    std::uint32_t EngineCreateLED(const A32CallFrame& call) { return Unsupported(call); }
    std::uint32_t EngineCreateVibra(const A32CallFrame& call) { return Unsupported(call); }
    std::uint32_t EngineCreateAudioRecorder(const A32CallFrame& call) { return Unsupported(call); }
    std::uint32_t EngineCreateMidi(const A32CallFrame& call) { return Unsupported(call); }
    std::uint32_t EngineCreateListener(const A32CallFrame& call) { return Unsupported(call); }
    std::uint32_t EngineCreate3DGroup(const A32CallFrame& call) { return Unsupported(call); }
    std::uint32_t EngineCreateMetadataExtractor(const A32CallFrame& call) { return Unsupported(call); }
    std::uint32_t EngineCreateExtensionObject(const A32CallFrame& call) { return Unsupported(call); }
    std::uint32_t EngineQueryExtension(const A32CallFrame& call) { return Unsupported(call); }

private:
    struct Object final {
        ObjectKind kind{};
        memory::GuestAddress base{};
        std::uint32_t state{kObjectUnrealized};
        std::set<std::string> interfaces;
        std::optional<audio::OpenSlesPcmMixer::PlayerId> player;
        std::uint32_t sample_rate{};
        std::uint32_t queue_capacity{};
        std::int32_t priority{};
        bool preemptable{};
        std::uint32_t object_callback{};
        std::uint32_t object_context{};
        std::uint32_t play_callback{};
        std::uint32_t play_context{};
        std::uint32_t play_mask{};
        std::optional<std::uint32_t> marker;
        std::uint32_t update_period{1000U};
        std::uint32_t queue_callback{};
        std::uint32_t queue_context{};
        std::uint32_t mix_callback{};
        std::uint32_t mix_context{};
        std::int16_t volume{};
        std::int16_t stereo_position{};
        bool mute{};
        bool stereo_enabled{};
    };

    Object& Allocate(const ObjectKind kind, std::set<std::string> interfaces,
                     const std::uint64_t thread_id) {
        if (objects_.size() >= kMaximumObjects ||
            next_offset_ > kOpenSlesObjectArenaBytes - kObjectStride) {
            throw std::length_error("OpenSL object arena exhausted");
        }
        const auto base = kOpenSlesObjectArenaBegin.Add(next_offset_);
        next_offset_ += kObjectStride;
        interfaces.insert("SL_IID_OBJECT");
        if (kind == ObjectKind::engine) interfaces.insert("SL_IID_ENGINE");
        if (kind == ObjectKind::output_mix) interfaces.insert("SL_IID_OUTPUTMIX");
        if (kind == ObjectKind::audio_player) interfaces.insert("SL_IID_PLAY");
        Object object;
        object.kind = kind;
        object.base = base;
        object.state = kObjectUnrealized;
        object.interfaces = std::move(interfaces);
        Write32(base.Value(), kOpenSlesObjectVtable.Value(), thread_id);
        if (object.interfaces.contains("SL_IID_ENGINE")) {
            Write32(base.Add(4U).Value(), kOpenSlesEngineVtable.Value(), thread_id);
        }
        if (object.interfaces.contains("SL_IID_PLAY")) {
            Write32(base.Add(4U).Value(), kOpenSlesPlayVtable.Value(), thread_id);
        }
        if (object.interfaces.contains("SL_IID_OUTPUTMIX")) {
            Write32(base.Add(4U).Value(), kOpenSlesOutputMixVtable.Value(), thread_id);
        }
        if (object.interfaces.contains("SL_IID_BUFFERQUEUE") ||
            object.interfaces.contains("SL_IID_ANDROIDSIMPLEBUFFERQUEUE")) {
            Write32(base.Add(8U).Value(), kOpenSlesBufferQueueVtable.Value(), thread_id);
        }
        if (object.interfaces.contains("SL_IID_VOLUME")) {
            Write32(base.Add(12U).Value(), kOpenSlesVolumeVtable.Value(), thread_id);
        }
        auto [found, inserted] = objects_.emplace(base.Value(), std::move(object));
        if (!inserted) throw std::logic_error("duplicate OpenSL object address");
        return found->second;
    }
    Object& Require(const std::uint32_t handle) {
        const auto found = objects_.find(ObjectKey(handle));
        if (found == objects_.end() || handle < found->second.base.Value() ||
            handle >= found->second.base.Add(kObjectStride).Value()) {
            throw std::runtime_error("stale or invalid OpenSL interface handle");
        }
        return found->second;
    }
    Object& RequireKind(const std::uint32_t handle, const ObjectKind kind) {
        auto& object = Require(handle);
        if (object.kind != kind) throw std::runtime_error("OpenSL object kind mismatch");
        return object;
    }
    Object& RequirePlayer(const std::uint32_t handle) {
        auto& object = RequireKind(handle, ObjectKind::audio_player);
        if (!object.player.has_value()) throw std::runtime_error("OpenSL player has no mixer state");
        return object;
    }
    static std::uint32_t ObjectKey(const std::uint32_t handle) {
        if (handle < kOpenSlesObjectArenaBegin.Value()) return 0U;
        const auto offset = handle - kOpenSlesObjectArenaBegin.Value();
        return kOpenSlesObjectArenaBegin.Value() +
               static_cast<std::uint32_t>((offset / kObjectStride) * kObjectStride);
    }
    memory::GuestAddress InterfaceHandle(const Object& object,
                                         const std::string_view iid) const {
        if (iid == "SL_IID_OBJECT") return object.base;
        if (iid == "SL_IID_ENGINE" || iid == "SL_IID_OUTPUTMIX" ||
            iid == "SL_IID_PLAY") return object.base.Add(4U);
        if (iid == "SL_IID_BUFFERQUEUE" ||
            iid == "SL_IID_ANDROIDSIMPLEBUFFERQUEUE") return object.base.Add(8U);
        if (iid == "SL_IID_VOLUME") return object.base.Add(12U);
        throw std::logic_error("OpenSL interface has no guest handle slot");
    }
    std::optional<std::string> IidName(const memory::GuestAddress address) const {
        const auto found = std::find_if(OpenSlesIids().begin(), OpenSlesIids().end(),
                                        [&](const auto& iid) {
                                            return iid.value_address == address;
                                        });
        if (found == OpenSlesIids().end()) return std::nullopt;
        return std::string(found->name);
    }
    memory::GuestAddress IidAddress(const std::string_view name) const {
        const auto found = std::find_if(OpenSlesIids().begin(), OpenSlesIids().end(),
                                        [&](const auto& iid) { return iid.name == name; });
        if (found == OpenSlesIids().end()) throw std::logic_error("unknown OpenSL IID name");
        return found->value_address;
    }
    std::optional<std::set<std::string>> RequestedInterfaces(
        const std::uint32_t count, const std::uint32_t ids,
        const std::uint32_t required, const std::set<std::string>& supported,
        const std::uint64_t thread_id) const {
        if (count > 64U || (count != 0U && (ids == 0U || required == 0U))) {
            return std::nullopt;
        }
        auto result = supported;
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto iid_pointer = Read32(ids + index * 4U, thread_id);
            const auto required_value = Read32(required + index * 4U, thread_id);
            if (required_value > 1U) return std::nullopt;
            const auto name = IidName(memory::GuestAddress{iid_pointer});
            const auto available = name.has_value() &&
                (supported.contains(*name) || *name == "SL_IID_VOLUME" ||
                 *name == "SL_IID_BUFFERQUEUE" ||
                 *name == "SL_IID_ANDROIDSIMPLEBUFFERQUEUE" ||
                 *name == "SL_IID_PLAY");
            if (!available) {
                if (required_value != 0U) return std::nullopt;
                continue;
            }
            result.insert(*name);
        }
        return result;
    }
    static std::vector<std::string_view> ObjectInterfaces(
        const std::uint32_t object_id) {
        switch (object_id) {
            case kObjectIdEngine: return {"SL_IID_OBJECT", "SL_IID_ENGINE"};
            case kObjectIdOutputMix:
                return {"SL_IID_OBJECT", "SL_IID_OUTPUTMIX"};
            case kObjectIdAudioPlayer:
                return {"SL_IID_OBJECT", "SL_IID_PLAY", "SL_IID_BUFFERQUEUE",
                        "SL_IID_ANDROIDSIMPLEBUFFERQUEUE", "SL_IID_VOLUME"};
            default: return {};
        }
    }
    std::uint32_t Read32(const std::uint32_t address,
                         const std::uint64_t thread_id) const {
        std::array<std::byte, 4> bytes{};
        calls_.address_space.Read(memory::GuestAddress{address}, bytes, thread_id);
        std::uint32_t result{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            result |= static_cast<std::uint32_t>(
                          std::to_integer<std::uint8_t>(bytes[index]))
                      << (index * 8U);
        }
        return result;
    }
    void Write32(const std::uint32_t address, const std::uint32_t value,
                 const std::uint64_t thread_id) const {
        std::array<std::byte, 4> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(value >> (index * 8U));
        }
        calls_.address_space.Write(memory::GuestAddress{address}, bytes, thread_id);
    }
    template <typename Read>
    std::uint32_t WritePlayer32(const A32CallFrame& call, Read read) {
        if (call.Argument(1) == 0U) return kParameterInvalid;
        std::scoped_lock lock(mutex_);
        const auto& object = RequirePlayer(call.Argument(0));
        Write32(call.Argument(1), read(object), call.ThreadId());
        return kSuccess;
    }
    template <typename Read>
    std::uint32_t WritePlayer16(const A32CallFrame& call, Read read) {
        return WritePlayer32(call, [&](const Object& object) {
            return static_cast<std::uint32_t>(
                static_cast<std::uint16_t>(read(object)));
        });
    }

    BoundaryCallServices& calls_;
    audio::OpenSlesPcmMixer& mixer_;
    std::mutex mutex_;
    std::map<std::uint32_t, Object> objects_;
    std::size_t next_offset_{};
};

OpenSlesModule::OpenSlesModule(BoundaryCallServices& calls,
                               audio::OpenSlesPcmMixer& mixer)
    : calls_(calls), impl_(std::make_unique<Impl>(calls, mixer)) {}
OpenSlesModule::~OpenSlesModule() = default;
BoundaryCallServices& OpenSlesModule::CallServices() noexcept { return calls_; }
void OpenSlesModule::MapGuestObjectArena() { impl_->MapGuestObjectArena(); }
std::vector<audio::OpenSlesConsumedBuffer>
OpenSlesModule::MixAdditiveStereoPcm16(
    const std::span<std::int16_t> output, const std::uint32_t output_rate) {
    return impl_->Mix(output, output_rate);
}

#define OGPLAY_DEFINE_OPENSLES(name, id, count, kind, method)                  \
    std::uint32_t OpenSlesModule::method(const A32CallFrame& call) {           \
        return impl_->method(call);                                            \
    }
OGPLAY_OPENSLES_BOUNDARY_EXPORTS(OGPLAY_DEFINE_OPENSLES)
#undef OGPLAY_DEFINE_OPENSLES

}  // namespace ogplay::runtime

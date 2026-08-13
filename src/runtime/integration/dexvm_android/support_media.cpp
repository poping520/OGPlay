// SoundPool/MediaPlayer handlers bound to the session's offline mixer
// (resid is the key). Playback state is real; unimplemented callbacks
// stay recorded gaps.

#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

void PopulateAudioVideo(AndroidHandlers& handlers,
                        const Context& context) {
    handlers.handler_android_sound_pool_init = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    handlers.handler_android_sound_pool_load = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
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
    handlers.handler_android_sound_pool_play = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
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
    handlers.handler_android_sound_pool_pause = dx::IntrinsicHandler([stream_call](dx::IntrinsicContext& call) {
        return stream_call(call, [](auto& mixer, const auto resource,
                                    const auto stream) {
            mixer.Pause(audio::JavaSoundPoolKind::pool, resource, stream);
        });
    });
    handlers.handler_android_sound_pool_resume = dx::IntrinsicHandler([stream_call](dx::IntrinsicContext& call) {
        return stream_call(call, [](auto& mixer, const auto resource,
                                    const auto stream) {
            mixer.Resume(audio::JavaSoundPoolKind::pool, resource, stream);
        });
    });
    handlers.handler_android_sound_pool_stop = dx::IntrinsicHandler([stream_call](dx::IntrinsicContext& call) {
        return stream_call(call, [](auto& mixer, const auto resource,
                                    const auto stream) {
            mixer.Stop(audio::JavaSoundPoolKind::pool, resource, stream);
        });
    });
    handlers.handler_android_sound_pool_set_volume = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto stream = call.arguments[0].AsInt();
        const auto found = context->sound_streams.find(stream);
        if (found != context->sound_streams.end()) {
            context->session->SoundPoolMixer().SetVolume(
                audio::JavaSoundPoolKind::pool, found->second, stream,
                call.arguments[1].AsFloat());
        }
        return dx::VmValue::Void();
    });
    handlers.handler_android_sound_pool_set_rate = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto stream = call.arguments[0].AsInt();
        const auto found = context->sound_streams.find(stream);
        if (found != context->sound_streams.end()) {
            context->session->SoundPoolMixer().SetPitch(
                audio::JavaSoundPoolKind::pool, found->second, stream,
                call.arguments[1].AsFloat());
        }
        return dx::VmValue::Void();
    });
    handlers.handler_android_sound_pool_unload = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        context->session->SoundPoolMixer().Unload(
            call.arguments[0].AsInt());
        return dx::VmValue::Int(1);
    });
    handlers.handler_android_sound_pool_release = dx::IntrinsicHandler([context](dx::IntrinsicContext&) {
        context->session->SoundPoolMixer().StopAllSounds();
        return dx::VmValue::Void();
    });

    handlers.handler_android_media_player_init = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    handlers.handler_android_media_player_set_data_source = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        // Path-backed playback is not wired to the mixer yet: record the
        // gap loudly; start() on this instance will have no audio.
        GuestLog(call, core::LogLevel::warn,
                 "MediaPlayer.setDataSource is not wired to the mixer: " +
                     call.vm.StringUtf8(call.arguments[0].ref));
        return dx::VmValue::Void();
    });
    handlers.handler_android_media_player_is_looping = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto found =
            context->media_looping.find(call.receiver.Value());
        return dx::VmValue::Int(
            found != context->media_looping.end() && found->second ? 1
                                                                   : 0);
    });
    handlers.handler_android_media_player_create = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
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
    handlers.handler_android_media_player_start = dx::IntrinsicHandler([context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            static_cast<void>(context->session->SoundPoolMixer().Play(
                audio::JavaSoundPoolKind::big, *resource, 0, 1.0F, true));
            context->media_playing[call.receiver.Value()] = true;
        }
        return dx::VmValue::Void();
    });
    handlers.handler_android_media_player_pause = dx::IntrinsicHandler([context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().Pause(
                audio::JavaSoundPoolKind::big, *resource, 0);
            context->media_playing[call.receiver.Value()] = false;
        }
        return dx::VmValue::Void();
    });
    handlers.handler_android_media_player_stop = dx::IntrinsicHandler([context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().Stop(
                audio::JavaSoundPoolKind::big, *resource, 0);
            context->media_playing[call.receiver.Value()] = false;
        }
        return dx::VmValue::Void();
    });
    handlers.handler_android_media_player_release = dx::IntrinsicHandler([context, media_resource](dx::IntrinsicContext& call) {
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
    handlers.handler_android_media_player_is_playing = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto found =
            context->media_playing.find(call.receiver.Value());
        return dx::VmValue::Int(
            found != context->media_playing.end() && found->second ? 1 : 0);
    });
    handlers.handler_android_media_player_prepare = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    handlers.handler_android_media_player_seek_to = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    handlers.handler_android_media_player_set_looping = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        context->media_looping[call.receiver.Value()] =
            call.arguments[0].AsInt() != 0;
        return dx::VmValue::Void();
    });
    handlers.handler_android_media_player_set_volume = dx::IntrinsicHandler([context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().SetVolume(
                audio::JavaSoundPoolKind::big, *resource, 0,
                call.arguments[0].AsFloat());
        }
        return dx::VmValue::Void();
    });
    handlers.handler_android_media_player_set_completion_listener = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        // Completion callbacks require the media clock; recorded gap.
        return dx::VmValue::Void();
    });
}

}  // namespace ogplay::runtime::android_intrinsics

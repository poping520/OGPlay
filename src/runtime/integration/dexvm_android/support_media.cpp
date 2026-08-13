// SoundPool/MediaPlayer handlers bound to the session's offline mixer
// (resid is the key). Playback state is real; unimplemented callbacks
// stay recorded gaps.

#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

void RegisterAudioVideo(dx::IntrinsicRegistry& registry,
                        const Context& context) {
    Bind(registry, "android.sound_pool.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    Bind(registry, "android.sound_pool.load",
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
    Bind(registry, "android.sound_pool.play",
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
    Bind(registry, "android.sound_pool.pause",
                      [stream_call](dx::IntrinsicContext& call) {
        return stream_call(call, [](auto& mixer, const auto resource,
                                    const auto stream) {
            mixer.Pause(audio::JavaSoundPoolKind::pool, resource, stream);
        });
    });
    Bind(registry, "android.sound_pool.resume",
                      [stream_call](dx::IntrinsicContext& call) {
        return stream_call(call, [](auto& mixer, const auto resource,
                                    const auto stream) {
            mixer.Resume(audio::JavaSoundPoolKind::pool, resource, stream);
        });
    });
    Bind(registry, "android.sound_pool.stop",
                      [stream_call](dx::IntrinsicContext& call) {
        return stream_call(call, [](auto& mixer, const auto resource,
                                    const auto stream) {
            mixer.Stop(audio::JavaSoundPoolKind::pool, resource, stream);
        });
    });
    Bind(registry, "android.sound_pool.set_volume",
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
    Bind(registry, "android.sound_pool.set_rate",
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
    Bind(registry, "android.sound_pool.unload",
                      [context](dx::IntrinsicContext& call) {
        context->session->SoundPoolMixer().Unload(
            call.arguments[0].AsInt());
        return dx::VmValue::Int(1);
    });
    Bind(registry, "android.sound_pool.release",
                      [context](dx::IntrinsicContext&) {
        context->session->SoundPoolMixer().StopAllSounds();
        return dx::VmValue::Void();
    });

    Bind(registry, "android.media_player.init",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    Bind(registry, "android.media_player.set_data_source",
                      [context](dx::IntrinsicContext& call) {
        // Path-backed playback is not wired to the mixer yet: record the
        // gap loudly; start() on this instance will have no audio.
        GuestLog(call, core::LogLevel::warn,
                 "MediaPlayer.setDataSource is not wired to the mixer: " +
                     call.vm.StringUtf8(call.arguments[0].ref));
        return dx::VmValue::Void();
    });
    Bind(registry, "android.media_player.is_looping",
                      [context](dx::IntrinsicContext& call) {
        const auto found =
            context->media_looping.find(call.receiver.Value());
        return dx::VmValue::Int(
            found != context->media_looping.end() && found->second ? 1
                                                                   : 0);
    });
    Bind(registry, "android.media_player.create",
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
    Bind(registry, "android.media_player.start",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            static_cast<void>(context->session->SoundPoolMixer().Play(
                audio::JavaSoundPoolKind::big, *resource, 0, 1.0F, true));
            context->media_playing[call.receiver.Value()] = true;
        }
        return dx::VmValue::Void();
    });
    Bind(registry, "android.media_player.pause",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().Pause(
                audio::JavaSoundPoolKind::big, *resource, 0);
            context->media_playing[call.receiver.Value()] = false;
        }
        return dx::VmValue::Void();
    });
    Bind(registry, "android.media_player.stop",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().Stop(
                audio::JavaSoundPoolKind::big, *resource, 0);
            context->media_playing[call.receiver.Value()] = false;
        }
        return dx::VmValue::Void();
    });
    Bind(registry, "android.media_player.release",
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
    Bind(registry, "android.media_player.is_playing",
                      [context](dx::IntrinsicContext& call) {
        const auto found =
            context->media_playing.find(call.receiver.Value());
        return dx::VmValue::Int(
            found != context->media_playing.end() && found->second ? 1 : 0);
    });
    Bind(registry, "android.media_player.prepare",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    Bind(registry, "android.media_player.seek_to",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    Bind(registry, "android.media_player.set_looping",
                      [context](dx::IntrinsicContext& call) {
        context->media_looping[call.receiver.Value()] =
            call.arguments[0].AsInt() != 0;
        return dx::VmValue::Void();
    });
    Bind(registry, "android.media_player.set_volume",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().SetVolume(
                audio::JavaSoundPoolKind::big, *resource, 0,
                call.arguments[0].AsFloat());
        }
        return dx::VmValue::Void();
    });
    Bind(registry, "android.media_player.set_completion_listener",
                      [](dx::IntrinsicContext&) {
        // Completion callbacks require the media clock; recorded gap.
        return dx::VmValue::Void();
    });
}

}  // namespace ogplay::runtime::android_intrinsics

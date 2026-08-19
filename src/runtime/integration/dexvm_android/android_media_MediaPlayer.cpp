// MediaPlayer handlers bound to the session's offline mixer ("big" bank,
// resid-keyed). Playback state is real; path-backed sources and completion
// callbacks stay recorded gaps.

#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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

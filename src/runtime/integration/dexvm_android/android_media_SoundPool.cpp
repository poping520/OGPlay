// SoundPool handlers bound to the session's offline mixer (resid is the
// key). Playback state is real; unimplemented callbacks stay recorded gaps.

#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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

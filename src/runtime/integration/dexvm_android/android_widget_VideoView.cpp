// VideoView handlers: real decoded playback through the injected
// VideoPlayer factory (ADR-0021). Playback state math and the onCompletion
// invocation are shared with the guest video pump via shared.h.

#include <algorithm>

#include "ogplay/runtime/vfs/vfs.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_VideoView(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/widget/VideoView;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", ViewInitHandler());
    builder.Virtual("setVideoPath", "(Ljava/lang/String;)V",
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
    builder.Virtual("start", "()V",
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
    builder.Virtual("pause", "()V",
        [context](dx::IntrinsicContext& call) {
            auto* state = VideoStateOf(context, call.receiver.Value());
            if (state != nullptr && state->playing) {
                state->base_position_ms =
                    VideoPositionOf(*state, context->uptime_millis.load());
                state->playing = false;
            }
            return dx::VmValue::Void();
        });
    builder.Virtual("seekTo", "(I)V",
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
    builder.Virtual("stopPlayback", "()V",
        [context](dx::IntrinsicContext& call) {
            context->video_views.erase(call.receiver.Value());
            context->pending_video_completion.erase(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.Virtual("getDuration", "()I",
        [context](dx::IntrinsicContext& call) {
            const auto* state =
                VideoStateOf(context, call.receiver.Value());
            return dx::VmValue::Int(state == nullptr
                                        ? 0
                                        : static_cast<std::int32_t>(
                                              state->duration_ms));
        });
    builder.Virtual("getCurrentPosition", "()I",
        [context](dx::IntrinsicContext& call) {
            const auto* state = VideoStateOf(context, call.receiver.Value());
            if (state == nullptr) return dx::VmValue::Int(0);
            return dx::VmValue::Int(static_cast<std::int32_t>(
                VideoPositionOf(*state, context->uptime_millis.load())));
        });
    builder.Virtual("canSeekForward", "()Z",
        [context](dx::IntrinsicContext& call) {
            // AOSP reports the prepared stream capability. Every host
            // VideoPlayer implements bounded SeekTo, while an unopened or
            // released view has not reached the prepared state.
            const auto* state = VideoStateOf(context, call.receiver.Value());
            return dx::VmValue::Int(
                state != nullptr && state->player != nullptr ? 1 : 0);
        });
    builder.Virtual("canSeekBackward", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto* state = VideoStateOf(context, call.receiver.Value());
            return dx::VmValue::Int(
                state != nullptr && state->player != nullptr ? 1 : 0);
        });
    builder.Virtual("canPause", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto* state = VideoStateOf(context, call.receiver.Value());
            return dx::VmValue::Int(
                state != nullptr && state->player != nullptr ? 1 : 0);
        });
    builder.Virtual("setOnCompletionListener", "(Landroid/media/MediaPlayer$OnCompletionListener;)V",
        [context](dx::IntrinsicContext& call) {
            context->video_completion[call.receiver.Value()] =
                call.arguments[0].ref;
            return dx::VmValue::Void();
        });
    builder.Virtual("setOnErrorListener",
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

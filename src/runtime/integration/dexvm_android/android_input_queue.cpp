#include "rom_input.h"

namespace ogplay::runtime::android_intrinsics {
namespace {
using V = dx::VmValue;

void WakeQueue(const std::weak_ptr<DexVmAndroidContext>& weak, const std::shared_ptr<RomInputState::NativeQueue>& q,
               std::uint32_t token, int sequence, bool handled) {
    {
        std::scoped_lock lock(q->mutex);
        q->finished.emplace_back(sequence, handled);
    }
    const auto context = weak.lock();
    if (!context) return;
    {
        std::scoped_lock lock(context->scheduler_mutex);
        const auto found = context->message_queues.find(token);
        if (found != context->message_queues.end()) found->second->wake = true;
    }
    if (context->wake_native_looper) context->wake_native_looper(q->thread);
    context->scheduler_changed.notify_all();
}
}  // namespace

void AddRomInputQueueNatives(dx::IntrinsicClassBuilder& b, const Context& context) {
    if (!context->rom_window_services_enabled) return;
    constexpr auto native = dx::kAccPrivate | dx::kAccStatic | dx::kAccNative;
    b.StaticMethod(
        "nativeInit", "(Ljava/lang/ref/WeakReference;Landroid/os/MessageQueue;)I",
        [context](dx::IntrinsicContext& c) {
            auto& s = RomInput(context);
            const auto queue = dx::IntrinsicCall(c).NonNullRef(1, "queue");
            const auto token = WindowField(c.vm, queue, "mPtr", "I").bits;
            {
                std::scoped_lock lock(context->scheduler_mutex);
                const auto found = context->message_queues.find(token);
                if (found == context->message_queues.end() ||
                    found->second->context_token != c.vm.CurrentContextToken())
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "InputQueue requires its own Looper"};
            }
            auto q = std::make_shared<RomInputState::NativeQueue>();
            q->weak = c.arguments[0].ref;
            q->queue = queue;
            q->thread = c.vm.CurrentContextToken();
            const auto id = AllocateRomInputHandle(s);
            s.native_queues.emplace(id, std::move(q));
            return V::Int(id);
        },
        native);
    b.StaticMethod(
        "nativeDispose", "(I)V",
        [context](dx::IntrinsicContext& c) {
            RomInput(context).native_queues.erase(c.arguments[0].AsInt());
            return V::Void();
        },
        native);
    for (bool key : {false, true})
        b.StaticMethod(
            key ? "nativeSendKeyEvent" : "nativeSendMotionEvent",
            key ? "(ILandroid/view/KeyEvent;Z)I" : "(ILandroid/view/MotionEvent;)I",
            [context, key](dx::IntrinsicContext& c) {
                auto& s = RomInput(context);
                const auto found = s.native_queues.find(c.arguments[0].AsInt());
                if (found == s.native_queues.end())
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "InputQueue is disposed"};
                const auto q = found->second;
                if (q->thread != c.vm.CurrentContextToken())
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "InputQueue sent on another thread"};
                const auto queue_token = WindowField(c.vm, q->queue, "mPtr", "I").bits;
                const int sequence = AllocateRomInputSequence(s);
                const auto finish = [weak = std::weak_ptr<DexVmAndroidContext>(context), q, queue_token,
                                     sequence](bool handled) { WakeQueue(weak, q, queue_token, sequence, handled); };
                if (!q->post || q->native.IsNull())
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                          "InputQueue is not bound to a NativeActivity"};
                AndroidBoundaryInput event;
                if (!key)
                    event = ReadMotionEvent(c.vm, c.arguments[1].ref);
                else {
                    event.type = AndroidBoundaryInputType::key;
                    const auto get = [&](const char* name) {
                        return CallAndroidMethod(c.vm, c.arguments[1].ref, name, "()I").AsInt();
                    };
                    event.action = get("getAction");
                    event.pressed = event.action == 0;
                    event.code = get("getKeyCode");
                    event.device_id = get("getDeviceId");
                    event.source = get("getSource");
                    event.flags = get("getFlags");
                    if (c.arguments[2].AsInt()) event.flags |= kAndroidKeyPreDispatchFlag;
                    event.meta_state = get("getMetaState");
                    event.scan_code = get("getScanCode");
                    event.repeat_count = get("getRepeatCount");
                    event.event_time_ns =
                        CallAndroidMethod(c.vm, c.arguments[1].ref, "getEventTime", "()J").AsLong() * 1'000'000;
                    event.down_time_ns =
                        CallAndroidMethod(c.vm, c.arguments[1].ref, "getDownTime", "()J").AsLong() * 1'000'000;
                }
                try {
                    q->post(event, finish);
                } catch (const std::invalid_argument& e) {
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", e.what()};
                }
                return V::Int(sequence);
            },
            native);
}
}  // namespace ogplay::runtime::android_intrinsics

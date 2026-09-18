// DVM-80: API-family translation unit. Physical consolidation only.

#include "ogplay/runtime/dexvm/io_runtime.h"

// ---- migrated from android_os_AsyncTask.cpp ----
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <limits>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {


[[nodiscard]] std::string_view BuildProperty(const std::string_view key) {
    if (key == "ro.product.cpu.abi") return "armeabi";
    if (key == "ro.build.tags") return "release-keys";
    if (key == "ro.build.version.release") return "4.4.4";
    if (key == "ro.build.version.sdk") return "19";
    if (key == "ro.build.version.codename") return "REL";
    return {};
}

template <typename Integer>
[[nodiscard]] Integer ParseBuildProperty(const std::string_view value,
                                         const Integer fallback) {
    Integer parsed{};
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    return error == std::errc{} && end == value.data() + value.size()
               ? parsed
               : fallback;
}

}  // namespace
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_AsyncTask(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/os/AsyncTask;", "Ljava/lang/Object;", {},
        dx::kAccPublic | dx::kAccAbstract);
    builder.Constructor("()V", [context](dx::IntrinsicContext& call) {
        std::scoped_lock lock(context->scheduler_mutex);
        context->async_tasks.try_emplace(call.receiver.Value());
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("onPreExecute", "()V", NeutralHandler('V'),
                          dx::kAccProtected);
    builder.VirtualMethod("onPostExecute", "(Ljava/lang/Object;)V",
                          NeutralHandler('V'), dx::kAccProtected);
    builder.VirtualMethod("onProgressUpdate", "([Ljava/lang/Object;)V",
                          NeutralHandler('V'), dx::kAccProtected);
    builder.VirtualMethod("onCancelled", "(Ljava/lang/Object;)V",
                          NeutralHandler('V'), dx::kAccProtected);
    builder.VirtualMethod("doInBackground", "([Ljava/lang/Object;)Ljava/lang/Object;",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "AsyncTask.doInBackground is not overridden"};
        }, dx::kAccProtected | dx::kAccAbstract);
    builder.FinalMethod("execute", "([Ljava/lang/Object;)Landroid/os/AsyncTask;",
        [context](dx::IntrinsicContext& call) {
            StartAsyncTask(call, context, call.receiver,
                           call.arguments[0].ref);
            return dx::VmValue::Ref(call.receiver);
        });
    builder.FinalMethod("publishProgress", "([Ljava/lang/Object;)V",
        [context](dx::IntrinsicContext& call) {
            ScheduleAsyncProgress(context, call.receiver,
                                  call.arguments[0].ref);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("cancel", "(Z)Z",
        [context](dx::IntrinsicContext& call) {
            dx::VmObjectRef thread{};
            {
                std::scoped_lock lock(context->scheduler_mutex);
                auto& state = context->async_tasks[call.receiver.Value()];
                if (state.status == DexVmAndroidContext::AsyncStatus::finished) {
                    return dx::VmValue::Int(0);
                }
                state.cancelled = true;
                thread = state.thread;
            }
            if (call.arguments[0].AsInt() != 0 && thread.IsValid()) {
                ThreadRuntime(context).Interrupt(thread);
            }
            return dx::VmValue::Int(1);
        });
    builder.FinalMethod("isCancelled", "()Z",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock lock(context->scheduler_mutex);
            const auto found = context->async_tasks.find(call.receiver.Value());
            return dx::VmValue::Int(
                found != context->async_tasks.end() && found->second.cancelled
                    ? 1 : 0);
        });
    builder.FinalMethod("getStatus", "()Landroid/os/AsyncTask$Status;",
        [context](dx::IntrinsicContext& call) {
            DexVmAndroidContext::AsyncStatus status{};
            {
                std::scoped_lock lock(context->scheduler_mutex);
                status = context->async_tasks[call.receiver.Value()].status;
            }
            const char* key = status == DexVmAndroidContext::AsyncStatus::pending
                                  ? "async_status_pending"
                              : status == DexVmAndroidContext::AsyncStatus::running
                                  ? "async_status_running"
                                  : "async_status_finished";
            const auto status_class = call.vm.Linker().FindClass(
                "Landroid/os/AsyncTask$Status;");
            if (!status_class.has_value()) {
                throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                                     "AsyncTask.Status is unavailable");
            }
            const auto initialized =
                call.vm.EnsureClassInitialized(*status_class);
            if (initialized.exception.IsValid()) {
                call.vm.SetPendingException(initialized.exception);
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            return dx::VmValue::Ref(Singleton(
                call, context, key, "Landroid/os/AsyncTask$Status;"));
        });
    builder.FinalMethod("get", "()Ljava/lang/Object;",
        [context](dx::IntrinsicContext& call) {
            dx::VmObjectRef thread{};
            {
                std::scoped_lock lock(context->scheduler_mutex);
                thread = context->async_tasks[call.receiver.Value()].thread;
            }
            if (thread.IsValid()) ThreadRuntime(context).Join(thread);
            std::scoped_lock lock(context->scheduler_mutex);
            return dx::VmValue::Ref(
                context->async_tasks[call.receiver.Value()].result);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_AsyncTask_Status(const Context& context) {
    constexpr std::array singleton_keys{
        "async_status_pending", "async_status_running",
        "async_status_finished"};
    dx::IntrinsicEnumBuilder builder(
        "Landroid/os/AsyncTask$Status;",
        {"PENDING", "RUNNING", "FINISHED"});
    builder.WithObjectFactory(
        [context, singleton_keys](dx::IntrinsicContext& call,
                                  std::string_view, std::size_t ordinal) {
            return Singleton(call, context, singleton_keys[ordinal],
                             "Landroid/os/AsyncTask$Status;");
        });
    return std::move(builder).Build();
}

Decl Declare_android_os_AsyncTask_Worker(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/os/AsyncTask$Worker;", "Ljava/lang/Object;",
        {"Ljava/lang/Runnable;"});
    builder.VirtualMethod("run", "()V", [context](dx::IntrinsicContext& call) {
        RunAsyncWorker(call, context, call.receiver);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- BootDex native boundary for android_os_SystemProperties.cpp ----

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_SystemProperties(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/os/SystemProperties;");
    constexpr auto flags = dx::kAccPrivate | dx::kAccNative;
    builder.StaticMethod(
        "native_get", "(Ljava/lang/String;)Ljava/lang/String;",
        [](dx::IntrinsicContext& call) {
            return MakeString(call, std::string(BuildProperty(
                call.vm.StringUtf8(call.arguments[0].ref))));
        }, flags);
    builder.StaticMethod(
        "native_get",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        [](dx::IntrinsicContext& call) {
            const auto value = BuildProperty(
                call.vm.StringUtf8(call.arguments[0].ref));
            if (!value.empty()) return MakeString(call, std::string(value));
            return dx::VmValue::Ref(call.arguments[1].ref.IsValid()
                                        ? call.arguments[1].ref
                                        : call.vm.NewStringUtf8(""));
        }, flags);
    builder.StaticMethod(
        "native_get_int", "(Ljava/lang/String;I)I",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(ParseBuildProperty<std::int32_t>(
                BuildProperty(call.vm.StringUtf8(call.arguments[0].ref)),
                call.arguments[1].AsInt()));
        }, flags);
    builder.StaticMethod(
        "native_get_long", "(Ljava/lang/String;J)J",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Long(ParseBuildProperty<std::int64_t>(
                BuildProperty(call.vm.StringUtf8(call.arguments[0].ref)),
                call.arguments[1].AsLong()));
        }, flags);
    builder.StaticMethod(
        "native_get_boolean", "(Ljava/lang/String;Z)Z",
        [](dx::IntrinsicContext& call) {
            const auto value = BuildProperty(
                call.vm.StringUtf8(call.arguments[0].ref));
            if (value == "1" || value == "y" || value == "yes" ||
                value == "true" || value == "on")
                return dx::VmValue::Int(1);
            if (value == "0" || value == "n" || value == "no" ||
                value == "false" || value == "off")
                return dx::VmValue::Int(0);
            return dx::VmValue::Int(call.arguments[1].AsInt());
        }, flags);
    const auto unsupported = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger())
            ledger->RecordUnimplemented("dexvm.system_properties_write", 0);
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            "system property mutation and callbacks are unsupported"};
    };
    builder.StaticMethod(
        "native_set", "(Ljava/lang/String;Ljava/lang/String;)V",
        unsupported, flags);
    builder.StaticMethod("native_add_change_callback", "()V", unsupported,
                         flags);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_Bundle.cpp ----
// Bundle handlers store key/value pairs in the per-session bundle map
// keyed by the receiver object handle.

namespace ogplay::runtime::android_intrinsics {


}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_os_CountDownTimer.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_CountDownTimer(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/CountDownTimer;", "Ljava/lang/Object;");
    builder.Constructor("(JJ)V", [context](dx::IntrinsicContext& call) {
        const auto duration = call.arguments[0].AsLong();
        const auto interval = call.arguments[1].AsLong();
        if (interval <= 0) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "countDownInterval must be positive"};
        }
        std::scoped_lock lock(context->scheduler_mutex);
        context->countdown_timers[call.receiver.Value()] = {
            duration, interval, 0, 0, false};
        return dx::VmValue::Void();
    });
    builder.FinalMethod("start", "()Landroid/os/CountDownTimer;",
        [context](dx::IntrinsicContext& call) {
            static_cast<void>(EnsureMainLooper(call, context));
            ScheduleCountDown(context, call.receiver);
            return dx::VmValue::Ref(call.receiver);
        });
    builder.FinalMethod("cancel", "()V", [context](dx::IntrinsicContext& call) {
        CancelCountDown(context, call.receiver);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("onTick", "(J)V", [](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "CountDownTimer.onTick is not overridden"};
    });
    builder.VirtualMethod("onFinish", "()V", [](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "CountDownTimer.onFinish is not overridden"};
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_os_Environment.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Environment(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Environment;", "Ljava/lang/Object;");
    builder.StaticMethod("getDataDirectory", "()Ljava/io/File;",
        [context](dx::IntrinsicContext& call) {
            constexpr auto key = "environment_data_directory";
            const auto found = context->singletons.find(key);
            if (found != context->singletons.end()) {
                return dx::VmValue::Ref(found->second);
            }
            const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
            const auto slots = call.vm.Model().InstanceSlots(file);
            slots[0] = {call.vm.NewStringUtf8("/data").Value(),
                        dx::SlotTag::ref};
            context->singletons.emplace(key, file);
            return dx::VmValue::Ref(file);
        });
    builder.StaticMethod("getExternalStorageDirectory", "()Ljava/io/File;",
        [context](dx::IntrinsicContext& call) {
            const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
            const auto slots = call.vm.Model().InstanceSlots(file);
            slots[0] = {
                call.vm.NewStringUtf8(context->external_storage_root).Value(),
                dx::SlotTag::ref};
            return dx::VmValue::Ref(file);
        });
    builder.StaticMethod("getExternalStorageState", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            // The external mount is required by the profile and read at
            // startup, so MEDIA_MOUNTED is the truthful state.
            return MakeString(call, "mounted");
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_os_Handler.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

std::int64_t DelayedWhen(const std::int64_t now,
                         const std::int64_t delay) {
    if (delay <= 0) return now;
    return delay > std::numeric_limits<std::int64_t>::max() - now
               ? std::numeric_limits<std::int64_t>::max()
               : now + delay;
}

dx::VmObjectRef HandlerLooper(dx::IntrinsicContext& call,
                              const Context& context,
                              const dx::VmObjectRef explicit_looper) {
    if (explicit_looper.IsValid()) return explicit_looper;
    auto looper = CurrentLooper(context, call.vm.CurrentContextToken());
    if (!looper.IsValid() && call.vm.CurrentContextToken() == 1U) {
        looper = EnsureMainLooper(call, context);
    }
    if (!looper.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                              "Handler created on a thread without a Looper"};
    }
    return looper;
}

void Configure(dx::IntrinsicContext& call, const Context& context,
               const dx::VmObjectRef looper,
               const dx::VmObjectRef callback) {
    std::scoped_lock lock(context->scheduler_mutex);
    context->handler_loopers[call.receiver.Value()] = looper;
    if (callback.IsValid()) {
        context->handler_callbacks[call.receiver.Value()] = callback;
    }
}

dx::VmObjectRef LooperOf(const Context& context,
                         const dx::VmObjectRef handler) {
    std::scoped_lock lock(context->scheduler_mutex);
    const auto found = context->handler_loopers.find(handler.Value());
    return found == context->handler_loopers.end() ? dx::VmObjectRef{}
                                                   : found->second;
}

void SetMessageTarget(dx::IntrinsicContext& call,
                      const dx::VmObjectRef message) {
    if (!message.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "message == null"};
    }
    call.vm.Model().InstanceSlots(message)[4] = {
        call.receiver.Value(), dx::SlotTag::ref};
}

bool QueueMessage(dx::IntrinsicContext& call, const Context& context,
                  const dx::VmObjectRef message,
                  const std::int64_t when) {
    SetMessageTarget(call, message);
    const auto slots = call.vm.Model().InstanceSlots(message);
    return EnqueueHandlerWork(
        context, LooperOf(context, call.receiver), call.receiver, message,
        slots[3].tag == dx::SlotTag::ref ? dx::VmObjectRef(slots[3].bits)
                                        : dx::VmObjectRef{},
        static_cast<std::int32_t>(slots[0].bits), false, when);
}

bool QueueRunnable(dx::IntrinsicContext& call, const Context& context,
                   const dx::VmObjectRef runnable,
                   const dx::VmObjectRef token, const std::int64_t when) {
    if (!runnable.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "runnable == null"};
    }
    return EnqueueHandlerWork(context, LooperOf(context, call.receiver),
                              call.receiver, runnable, token, 0, true, when);
}

}  // namespace

Decl Declare_android_os_Handler(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Handler;", "Ljava/lang/Object;");
    builder.Constructor("()V", [context](dx::IntrinsicContext& call) {
        Configure(call, context,
                  HandlerLooper(call, context, dx::VmObjectRef{}),
                  dx::VmObjectRef{});
        return dx::VmValue::Void();
    });
    builder.Constructor("(Landroid/os/Handler$Callback;)V",
        [context](dx::IntrinsicContext& call) {
            Configure(call, context,
                      HandlerLooper(call, context, dx::VmObjectRef{}),
                      call.arguments[0].ref);
            return dx::VmValue::Void();
        });
    builder.Constructor("(Landroid/os/Looper;)V",
        [context](dx::IntrinsicContext& call) {
            const auto looper = call.arguments[0].ref;
            if (!looper.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "looper == null"};
            }
            Configure(call, context, looper, dx::VmObjectRef{});
            return dx::VmValue::Void();
        });
    builder.Constructor("(Landroid/os/Looper;Landroid/os/Handler$Callback;)V",
        [context](dx::IntrinsicContext& call) {
            const auto looper = call.arguments[0].ref;
            if (!looper.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "looper == null"};
            }
            Configure(call, context, looper, call.arguments[1].ref);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("obtainMessage", "()Landroid/os/Message;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(MakeMessage(
                call, 0, dx::VmObjectRef{}, call.receiver));
        });
    builder.FinalMethod("obtainMessage", "(I)Landroid/os/Message;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(MakeMessage(
                call, call.arguments[0].AsInt(), dx::VmObjectRef{},
                call.receiver));
        });
    builder.FinalMethod("obtainMessage", "(ILjava/lang/Object;)Landroid/os/Message;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(MakeMessage(
                call, call.arguments[0].AsInt(), call.arguments[1].ref,
                call.receiver));
        });
    builder.FinalMethod("obtainMessage", "(III)Landroid/os/Message;",
        [](dx::IntrinsicContext& call) {
            const auto message = MakeMessage(
                call, call.arguments[0].AsInt(), dx::VmObjectRef{},
                call.receiver);
            const auto slots = call.vm.Model().InstanceSlots(message);
            slots[1] = {static_cast<std::uint32_t>(
                            call.arguments[1].AsInt()),
                        dx::SlotTag::cat1};
            slots[2] = {static_cast<std::uint32_t>(
                            call.arguments[2].AsInt()),
                        dx::SlotTag::cat1};
            return dx::VmValue::Ref(message);
        });
    builder.FinalMethod(
        "obtainMessage", "(IIILjava/lang/Object;)Landroid/os/Message;",
        [](dx::IntrinsicContext& call) {
            const auto message = MakeMessage(
                call, call.arguments[0].AsInt(), call.arguments[3].ref,
                call.receiver);
            const auto slots = call.vm.Model().InstanceSlots(message);
            slots[1] = {static_cast<std::uint32_t>(
                            call.arguments[1].AsInt()),
                        dx::SlotTag::cat1};
            slots[2] = {static_cast<std::uint32_t>(
                            call.arguments[2].AsInt()),
                        dx::SlotTag::cat1};
            return dx::VmValue::Ref(message);
        });
    builder.FinalMethod("sendMessage", "(Landroid/os/Message;)Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(QueueMessage(
                call, context, call.arguments[0].ref,
                context->uptime_millis.load()) ? 1 : 0);
        });
    builder.FinalMethod("sendMessageDelayed", "(Landroid/os/Message;J)Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(QueueMessage(
                call, context, call.arguments[0].ref,
                DelayedWhen(context->uptime_millis.load(),
                            call.arguments[1].AsLong())) ? 1 : 0);
        });
    builder.FinalMethod("sendMessageAtTime", "(Landroid/os/Message;J)Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(QueueMessage(
                call, context, call.arguments[0].ref,
                call.arguments[1].AsLong()) ? 1 : 0);
        });
    builder.FinalMethod("sendEmptyMessage", "(I)Z",
        [context](dx::IntrinsicContext& call) {
            const auto message = MakeMessage(
                call, call.arguments[0].AsInt(), dx::VmObjectRef{},
                call.receiver);
            return dx::VmValue::Int(QueueMessage(
                call, context, message, context->uptime_millis.load())
                ? 1 : 0);
        });
    builder.FinalMethod("sendEmptyMessageDelayed", "(IJ)Z",
        [context](dx::IntrinsicContext& call) {
            const auto message = MakeMessage(
                call, call.arguments[0].AsInt(), dx::VmObjectRef{},
                call.receiver);
            return dx::VmValue::Int(QueueMessage(
                call, context, message,
                DelayedWhen(context->uptime_millis.load(),
                            call.arguments[1].AsLong())) ? 1 : 0);
        });
    builder.FinalMethod("sendEmptyMessageAtTime", "(IJ)Z",
        [context](dx::IntrinsicContext& call) {
            const auto message = MakeMessage(
                call, call.arguments[0].AsInt(), dx::VmObjectRef{},
                call.receiver);
            return dx::VmValue::Int(QueueMessage(
                call, context, message, call.arguments[1].AsLong())
                ? 1 : 0);
        });
    builder.FinalMethod("dispatchMessage", "(Landroid/os/Message;)V",
        [context](dx::IntrinsicContext& call) {
            dx::VmObjectRef callback{};
            {
                std::scoped_lock lock(context->scheduler_mutex);
                const auto found = context->handler_callbacks.find(
                    call.receiver.Value());
                if (found != context->handler_callbacks.end()) {
                    callback = found->second;
                }
            }
            if (callback.IsValid()) {
                auto& linker = call.vm.Linker();
                const auto owner = call.vm.Model().ObjectClass(callback);
                const auto index = linker.FindVtableIndex(
                    owner, "handleMessage", "(Landroid/os/Message;)Z");
                if (!index.has_value()) {
                    throw dx::VmJavaThrow{"Ljava/lang/AbstractMethodError;",
                                          "Handler.Callback.handleMessage"};
                }
                const auto outcome = call.vm.Call(
                    linker.Class(owner).vtable[*index],
                    std::vector<dx::VmValue>{dx::VmValue::Ref(callback),
                                             call.arguments[0]});
                if (outcome.exception.IsValid()) {
                    call.vm.SetPendingException(outcome.exception);
                    return dx::VmValue::Void();
                }
                if (outcome.value.AsInt() != 0) return dx::VmValue::Void();
            }
            DeliverMessage(call, call.receiver, call.arguments[0].ref);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("post", "(Ljava/lang/Runnable;)Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(QueueRunnable(
                call, context, call.arguments[0].ref, dx::VmObjectRef{},
                context->uptime_millis.load()) ? 1 : 0);
        });
    builder.FinalMethod("postDelayed", "(Ljava/lang/Runnable;J)Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(QueueRunnable(
                call, context, call.arguments[0].ref, dx::VmObjectRef{},
                DelayedWhen(context->uptime_millis.load(),
                            call.arguments[1].AsLong()))
                ? 1 : 0);
        });
    builder.FinalMethod("postAtTime", "(Ljava/lang/Runnable;J)Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(QueueRunnable(
                call, context, call.arguments[0].ref, dx::VmObjectRef{},
                call.arguments[1].AsLong()) ? 1 : 0);
        });
    builder.FinalMethod("postAtTime", "(Ljava/lang/Runnable;Ljava/lang/Object;J)Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(QueueRunnable(
                call, context, call.arguments[0].ref, call.arguments[1].ref,
                call.arguments[2].AsLong()) ? 1 : 0);
        });
    builder.FinalMethod("removeCallbacks", "(Ljava/lang/Runnable;)V",
        [context](dx::IntrinsicContext& call) {
            RemoveHandlerWork(context, call.receiver, std::nullopt,
                              call.arguments[0].ref, true,
                              dx::VmObjectRef{}, false);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("removeMessages", "(I)V",
        [context](dx::IntrinsicContext& call) {
            RemoveHandlerWork(context, call.receiver,
                              call.arguments[0].AsInt(), dx::VmObjectRef{},
                              false, dx::VmObjectRef{}, false);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("removeMessages", "(ILjava/lang/Object;)V",
        [context](dx::IntrinsicContext& call) {
            RemoveHandlerWork(context, call.receiver,
                              call.arguments[0].AsInt(), dx::VmObjectRef{}, false,
                              call.arguments[1].ref, true);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("removeCallbacksAndMessages", "(Ljava/lang/Object;)V",
        [context](dx::IntrinsicContext& call) {
            const auto token = call.arguments[0].ref;
            RemoveHandlerWork(context, call.receiver, std::nullopt,
                              dx::VmObjectRef{}, false,
                              token, token.IsValid());
            RemoveHandlerWork(context, call.receiver, std::nullopt,
                              dx::VmObjectRef{}, true,
                              token, token.IsValid());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("hasMessages", "(I)Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(HasHandlerWork(
                context, call.receiver, call.arguments[0].AsInt(),
                dx::VmObjectRef{}, false)
                ? 1 : 0);
        });
    builder.FinalMethod("getLooper", "()Landroid/os/Looper;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(LooperOf(context, call.receiver));
        });
    builder.VirtualMethod("handleMessage", "(Landroid/os/Message;)V",
                          NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Handler_Callback(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Interface(
        "Landroid/os/Handler$Callback;");
    builder.VirtualMethod("handleMessage", "(Landroid/os/Message;)Z",
                          NeutralHandler('Z'));
    return std::move(builder).Build();
}

Decl Declare_android_os_HandlerThread(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/os/HandlerThread;", "Ljava/lang/Thread;");
    const auto construct = [](dx::IntrinsicContext& call) {
        auto& linker = call.vm.Linker();
        const auto owner = linker.FindClass("Ljava/lang/Thread;");
        const auto method = owner.has_value()
                                ? linker.FindDirectMethod(
                                      *owner, "<init>",
                                      "(Ljava/lang/String;)V")
                                : std::nullopt;
        if (!method.has_value()) {
            throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                                 "Thread(String) is unavailable");
        }
        const auto outcome = call.vm.Call(
            *method, std::vector<dx::VmValue>{dx::VmValue::Ref(call.receiver),
                                              call.arguments[0]});
        if (outcome.exception.IsValid()) {
            call.vm.SetPendingException(outcome.exception);
        }
        return dx::VmValue::Void();
    };
    builder.Constructor("(Ljava/lang/String;)V", construct);
    builder.Constructor("(Ljava/lang/String;I)V", construct);
    builder.VirtualMethod("onLooperPrepared", "()V", NeutralHandler('V'),
                          dx::kAccProtected);
    builder.OverrideMethod("run", "()V", [context](dx::IntrinsicContext& call) {
        const auto looper = PrepareLooper(call, context, false);
        PublishHandlerThreadLooper(context, call.receiver, looper);
        auto& linker = call.vm.Linker();
        const auto owner = call.vm.Model().ObjectClass(call.receiver);
        const auto index = linker.FindVtableIndex(owner, "onLooperPrepared", "()V");
        if (index.has_value()) {
            const auto outcome = call.vm.Call(
                linker.Class(owner).vtable[*index],
                std::vector<dx::VmValue>{dx::VmValue::Ref(call.receiver)});
            if (outcome.exception.IsValid()) {
                call.vm.SetPendingException(outcome.exception);
                return dx::VmValue::Void();
            }
        }
        LoopLooper(call, context, looper);
        return dx::VmValue::Void();
    });
    builder.FinalMethod("getLooper", "()Landroid/os/Looper;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                WaitForHandlerThreadLooper(call, context, call.receiver));
        });
    builder.FinalMethod("quit", "()Z", [context](dx::IntrinsicContext& call) {
        const auto looper = WaitForHandlerThreadLooper(
            call, context, call.receiver);
        return dx::VmValue::Int(
            looper.IsValid() && QuitLooper(context, looper) ? 1 : 0);
    });
    builder.FinalMethod("quitSafely", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto looper = WaitForHandlerThreadLooper(
                call, context, call.receiver);
            return dx::VmValue::Int(
                looper.IsValid() && QuitLooper(context, looper) ? 1 : 0);
        });
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_IBinder.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {


Decl Declare_android_os_Binder(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Binder;");
    constexpr auto public_native = dx::kAccPublic | dx::kAccStatic |
                                   dx::kAccFinal | dx::kAccNative;
    constexpr auto private_native = dx::kAccPrivate | dx::kAccFinal |
                                    dx::kAccNative;
    builder.StaticMethod("getCallingPid", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); }, public_native);
    builder.StaticMethod("getCallingUid", "()I",
        [context](dx::IntrinsicContext&) { return dx::VmValue::Int(static_cast<std::int32_t>(context->application_uid)); }, public_native);
    builder.StaticMethod("clearCallingIdentity", "()J",
        [context](dx::IntrinsicContext& call) {
            auto& state = context->binder_threads[call.vm.CurrentContextToken()];
            const auto previous = state.identity_token; state.identity_token = 0;
            return dx::VmValue::Long(static_cast<std::int64_t>(previous));
        }, public_native);
    builder.StaticMethod("restoreCallingIdentity", "(J)V",
        [context](dx::IntrinsicContext& call) {
            context->binder_threads[call.vm.CurrentContextToken()].identity_token =
                static_cast<std::uint64_t>(call.arguments[0].AsLong());
            return dx::VmValue::Void();
        }, public_native);
    builder.StaticMethod("setThreadStrictModePolicy", "(I)V",
        [context](dx::IntrinsicContext& call) {
            context->binder_threads[call.vm.CurrentContextToken()].strict_mode_policy = call.arguments[0].AsInt();
            return dx::VmValue::Void();
        }, public_native);
    builder.StaticMethod("getThreadStrictModePolicy", "()I",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(context->binder_threads[call.vm.CurrentContextToken()].strict_mode_policy);
        }, public_native);
    builder.StaticMethod("flushPendingCommands", "()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, public_native);
    builder.StaticMethod("joinThreadPool", "()V",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "remote Binder thread pool is unsupported"};
        }, public_native);
    builder.DirectMethod("init", "()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, private_native);
    builder.DirectMethod("destroy", "()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, private_native);
    return std::move(builder).Build();
}

Decl Declare_android_os_StrictMode(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/StrictMode;");
    // Local-only Binder never runs StrictMode detectors. These are the narrow
    // platform hooks used by the original Parcel exception protocol.
    builder.StaticMethod("hasGatheredViolations", "()Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.StaticMethod("clearGatheredViolations", "()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.StaticMethod("vmSqliteObjectLeaksEnabled", "()Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.StaticMethod("vmClosableObjectLeaksEnabled", "()Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.StaticMethod("vmRegistrationLeaksEnabled", "()Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    const auto unsupported = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger())
            ledger->RecordUnimplemented("dexvm.local_binder", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "Binder StrictMode violation processing is unsupported"};
    };
    builder.StaticMethod("writeGatheredViolationsToParcel", "(Landroid/os/Parcel;)V", unsupported);
    builder.StaticMethod("readAndHandleBinderCallViolations", "(Landroid/os/Parcel;)V", unsupported);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_os_Looper.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Looper(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Looper;", "Ljava/lang/Object;");
    builder.StaticMethod("prepare", "()V", [context](dx::IntrinsicContext& call) {
        static_cast<void>(PrepareLooper(call, context, false));
        return dx::VmValue::Void();
    });
    builder.StaticMethod("prepareMainLooper", "()V",
        [context](dx::IntrinsicContext& call) {
            static_cast<void>(PrepareLooper(call, context, true));
            return dx::VmValue::Void();
        });
    builder.StaticMethod("loop", "()V", [context](dx::IntrinsicContext& call) {
        LoopLooper(call, context,
                   CurrentLooper(context, call.vm.CurrentContextToken()));
        return dx::VmValue::Void();
    });
    builder.StaticMethod("getMainLooper", "()Landroid/os/Looper;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(EnsureMainLooper(call, context));
    });
    builder.StaticMethod("myLooper", "()Landroid/os/Looper;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(CurrentLooper(
                context, call.vm.CurrentContextToken()));
        });
    builder.StaticMethod("myQueue", "()Landroid/os/MessageQueue;",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "MessageQueue is internal to DexVM"};
        });
    builder.FinalMethod("getThread", "()Ljava/lang/Thread;",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock lock(context->scheduler_mutex);
            const auto found = context->loopers.find(call.receiver.Value());
            return dx::VmValue::Ref(found == context->loopers.end()
                                        ? dx::VmObjectRef{}
                                        : found->second.thread);
        });
    builder.FinalMethod("quit", "()V", [context](dx::IntrinsicContext& call) {
        static_cast<void>(QuitLooper(context, call.receiver));
        return dx::VmValue::Void();
    });
    builder.FinalMethod("quitSafely", "()V",
        [context](dx::IntrinsicContext& call) {
            static_cast<void>(QuitLooper(context, call.receiver));
            return dx::VmValue::Void();
        });
    builder.FinalMethod("isCurrentThread", "()Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(
                CurrentLooper(context, call.vm.CurrentContextToken()) ==
                        call.receiver
                    ? 1 : 0);
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_os_Message.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Message(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Message;", "Ljava/lang/Object;");
    builder.InstanceField("what", "I");
    builder.InstanceField("arg1", "I");
    builder.InstanceField("arg2", "I");
    builder.InstanceField("obj", "Ljava/lang/Object;");
    builder.InstanceField("target", "Landroid/os/Handler;");
    builder.StaticMethod("obtain", "(Landroid/os/Handler;ILjava/lang/Object;)Landroid/os/Message;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(MakeMessage(
                call, call.arguments[1].AsInt(), call.arguments[2].ref,
                call.arguments[0].ref));
        });
    builder.FinalMethod("sendToTarget", "()V", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        const auto handler = dx::VmObjectRef(slots[4].bits);
        if (!handler.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "Message target is null"};
        }
        auto& linker = call.vm.Linker();
        const auto owner = call.vm.Model().ObjectClass(handler);
        const auto index = linker.FindVtableIndex(
            owner, "sendMessage", "(Landroid/os/Message;)Z");
        if (!index.has_value()) {
            throw dx::VmJavaThrow{"Ljava/lang/AbstractMethodError;",
                                  "Handler.sendMessage"};
        }
        const auto outcome = call.vm.Call(
            linker.Class(owner).vtable[*index],
            std::vector<dx::VmValue>{dx::VmValue::Ref(handler),
                                     dx::VmValue::Ref(call.receiver)});
        if (outcome.exception.IsValid()) {
            call.vm.SetPendingException(outcome.exception);
        }
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_SystemClock(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/os/SystemClock;", "Ljava/lang/Object;");
    const auto millis = [context](dx::IntrinsicContext&) {
        return dx::VmValue::Long(context->uptime_millis.load());
    };
    builder.StaticMethod("uptimeMillis", "()J", millis);
    builder.StaticMethod("elapsedRealtime", "()J", millis);
    builder.StaticMethod("elapsedRealtimeNanos", "()J",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Long(
                context->uptime_millis.load() * 1'000'000LL);
        });
    builder.StaticMethod("currentThreadTimeMillis", "()J", millis);
    builder.StaticMethod("sleep", "(J)V", [context](dx::IntrinsicContext& call) {
        const auto delay = call.arguments[0].AsLong();
        if (delay > 0) AdvanceAndroidClock(*context, delay);
        ThreadRuntime(context).Yield();
        return dx::VmValue::Void();
    });
    builder.StaticMethod("setCurrentTimeMillis", "(J)Z",
                         [](dx::IntrinsicContext&) {
                             return dx::VmValue::Int(0);
                         });
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_os_StatFs.cpp ----
#include <algorithm>
#include <cstdint>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_StatFs(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/StatFs;", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/lang/String;)V",
        [](dx::IntrinsicContext&) {
            // Only the external volume is queryable on this platform; the
            // constructor path argument selects nothing further.
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getBlockSize", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(4096);
    });
    builder.FinalMethod("getAvailableBlocks", "()I",
        [context](dx::IntrinsicContext&) {
            const auto blocks = context->external_free_bytes / 4096U;
            return dx::VmValue::Int(static_cast<std::int32_t>(
                std::min<std::uint64_t>(blocks, INT32_MAX)));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


namespace ogplay::runtime::android_intrinsics {


Decl Declare_android_os_Parcel(const Context& context) {
    using Backing = DexVmAndroidContext::ParcelBacking;
    constexpr std::size_t kMaxParcelBytes = 16U * 1024U * 1024U;
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Parcel;");
    const auto native_ptr = builder.BoundInstanceField(
        "mNativePtr", "I", dx::kAccPrivate);
    const auto owns = builder.BoundInstanceField(
        "mOwnsNativeParcelObject", "Z", dx::kAccPrivate);

    const auto require = [context](const std::int32_t raw) -> Backing& {
        if (raw <= 0)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid Parcel token"};
        const auto found = context->parcel_backings.find(
            static_cast<std::uint32_t>(raw));
        if (found == context->parcel_backings.end())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "stale Parcel token"};
        return found->second;
    };
    const auto resize = [](Backing& parcel, const std::size_t size) {
        if (size > kMaxParcelBytes)
            throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "Parcel exceeds OGPlay limit"};
        parcel.bytes.resize(size);
        if (parcel.position > size) parcel.position = size;
        std::erase_if(parcel.binders, [size](const auto& record) {
            return record.offset >= size || record.span > size - record.offset;
        });
    };
    const auto write = [resize](Backing& parcel, const std::span<const std::byte> bytes) {
        if (bytes.size() > kMaxParcelBytes - parcel.position)
            throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "Parcel exceeds OGPlay limit"};
        const auto end = parcel.position + bytes.size();
        if (end > parcel.bytes.size()) resize(parcel, end);
        std::erase_if(parcel.binders, [start = parcel.position, end](const auto& record) {
            return record.offset < end && start < record.offset + record.span;
        });
        std::copy(bytes.begin(), bytes.end(), parcel.bytes.begin() +
                  static_cast<std::ptrdiff_t>(parcel.position));
        parcel.position = end;
    };
    const auto read = [](Backing& parcel, const std::size_t count) {
        if (count > parcel.bytes.size() - std::min(parcel.position, parcel.bytes.size()))
            throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "Parcel underflow"};
        const auto start = parcel.position;
        parcel.position += count;
        return std::span<const std::byte>(parcel.bytes).subspan(start, count);
    };
    const auto write32 = [write](Backing& parcel, const std::uint32_t value) {
        const std::array bytes{std::byte(value), std::byte(value >> 8U),
                               std::byte(value >> 16U), std::byte(value >> 24U)};
        write(parcel, bytes);
    };
    const auto read32 = [read](Backing& parcel) {
        const auto bytes = read(parcel, 4U);
        return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8U) |
               (std::uint32_t(bytes[2]) << 16U) | (std::uint32_t(bytes[3]) << 24U);
    };
    const auto write64 = [write](Backing& parcel, const std::uint64_t value) {
        std::array<std::byte, 8> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = std::byte(value >> (i * 8U));
        write(parcel, bytes);
    };
    const auto read64 = [read](Backing& parcel) {
        const auto bytes = read(parcel, 8U);
        std::uint64_t value{};
        for (std::size_t i = 0; i < bytes.size(); ++i)
            value |= std::uint64_t(bytes[i]) << (i * 8U);
        return value;
    };
    const auto native_flags = dx::kAccPrivate | dx::kAccStatic | dx::kAccNative;

    builder.DirectMethod("init", "(I)V",
        [context, native_ptr, owns](dx::IntrinsicContext& call) {
            if (call.arguments[0].AsInt() != 0)
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "imported Parcel holders are unsupported"};
            auto token = context->next_parcel_token++;
            if (token == 0 || context->parcel_backings.contains(token))
                throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "Parcel token space exhausted"};
            context->parcel_backings.emplace(token, Backing{});
            context->parcel_owner_tokens[call.receiver.Value()] = token;
            dx::IntrinsicCall fields(call);
            fields.SetInt(native_ptr, static_cast<std::int32_t>(token));
            fields.SetInt(owns, 1);
            return dx::VmValue::Void();
        }, dx::kAccPrivate | dx::kAccFinal);
    builder.StaticMethod("nativeCreate", "()I", [](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "bare Parcel allocation has no guest owner"};
    }, native_flags);
    builder.StaticMethod("nativeFreeBuffer", "(I)V", [require](dx::IntrinsicContext& call) {
        auto& parcel = require(call.arguments[0].AsInt());
        parcel.bytes.clear(); parcel.position = 0; parcel.binders.clear();
        return dx::VmValue::Void();
    }, native_flags);
    builder.StaticMethod("nativeDestroy", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto token = static_cast<std::uint32_t>(call.arguments[0].AsInt());
        context->parcel_backings.erase(token);
        return dx::VmValue::Void();
    }, native_flags);
    builder.StaticMethod("nativeDataSize", "(I)I", [require](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(require(call.arguments[0].AsInt()).bytes.size()));
    }, native_flags);
    builder.StaticMethod("nativeDataAvail", "(I)I", [require](dx::IntrinsicContext& call) {
        const auto& p = require(call.arguments[0].AsInt());
        return dx::VmValue::Int(static_cast<std::int32_t>(p.bytes.size() - std::min(p.position, p.bytes.size())));
    }, native_flags);
    builder.StaticMethod("nativeDataPosition", "(I)I", [require](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(require(call.arguments[0].AsInt()).position));
    }, native_flags);
    builder.StaticMethod("nativeDataCapacity", "(I)I", [require](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(require(call.arguments[0].AsInt()).bytes.capacity()));
    }, native_flags);
    builder.StaticMethod("nativeSetDataSize", "(II)V", [require, resize](dx::IntrinsicContext& call) {
        if (call.arguments[1].AsInt() < 0) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "size"};
        resize(require(call.arguments[0].AsInt()), static_cast<std::size_t>(call.arguments[1].AsInt()));
        return dx::VmValue::Void();
    }, native_flags);
    builder.StaticMethod("nativeSetDataPosition", "(II)V", [require](dx::IntrinsicContext& call) {
        auto& p = require(call.arguments[0].AsInt());
        const auto pos = call.arguments[1].AsInt();
        if (pos < 0 || static_cast<std::size_t>(pos) > p.bytes.size())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "position"};
        p.position = static_cast<std::size_t>(pos); return dx::VmValue::Void();
    }, native_flags);
    builder.StaticMethod("nativeSetDataCapacity", "(II)V", [require](dx::IntrinsicContext& call) {
        auto& p = require(call.arguments[0].AsInt()); const auto size = call.arguments[1].AsInt();
        if (size < 0 || static_cast<std::size_t>(size) > kMaxParcelBytes)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "capacity"};
        p.bytes.reserve(static_cast<std::size_t>(size)); return dx::VmValue::Void();
    }, native_flags);
    builder.StaticMethod("nativePushAllowFds", "(IZ)Z", [require](dx::IntrinsicContext& call) {
        auto& p = require(call.arguments[0].AsInt()); const bool old = p.allow_fds;
        p.allow_fds = call.arguments[1].AsInt() != 0; return dx::VmValue::Int(old ? 1 : 0);
    }, native_flags);
    builder.StaticMethod("nativeRestoreAllowFds", "(IZ)V", [require](dx::IntrinsicContext& call) {
        require(call.arguments[0].AsInt()).allow_fds = call.arguments[1].AsInt() != 0;
        return dx::VmValue::Void();
    }, native_flags);
    builder.StaticMethod("nativeWriteInt", "(II)V", [require, write32](dx::IntrinsicContext& call) {
        write32(require(call.arguments[0].AsInt()), static_cast<std::uint32_t>(call.arguments[1].AsInt()));
        return dx::VmValue::Void();
    }, native_flags);
    builder.StaticMethod("nativeReadInt", "(I)I", [require, read32](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(read32(require(call.arguments[0].AsInt()))));
    }, native_flags);
    builder.StaticMethod("nativeWriteLong", "(IJ)V", [require, write64](dx::IntrinsicContext& call) {
        write64(require(call.arguments[0].AsInt()), static_cast<std::uint64_t>(call.arguments[1].AsLong()));
        return dx::VmValue::Void();
    }, native_flags);
    builder.StaticMethod("nativeReadLong", "(I)J", [require, read64](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(static_cast<std::int64_t>(read64(require(call.arguments[0].AsInt()))));
    }, native_flags);
    builder.StaticMethod("nativeWriteFloat", "(IF)V", [require, write32](dx::IntrinsicContext& call) {
        write32(require(call.arguments[0].AsInt()), std::bit_cast<std::uint32_t>(call.arguments[1].AsFloat()));
        return dx::VmValue::Void();
    }, native_flags);
    builder.StaticMethod("nativeReadFloat", "(I)F", [require, read32](dx::IntrinsicContext& call) {
        return dx::VmValue::Float(std::bit_cast<float>(read32(require(call.arguments[0].AsInt()))));
    }, native_flags);
    builder.StaticMethod("nativeWriteDouble", "(ID)V", [require, write64](dx::IntrinsicContext& call) {
        write64(require(call.arguments[0].AsInt()), std::bit_cast<std::uint64_t>(call.arguments[1].AsDouble()));
        return dx::VmValue::Void();
    }, native_flags);
    builder.StaticMethod("nativeReadDouble", "(I)D", [require, read64](dx::IntrinsicContext& call) {
        return dx::VmValue::Double(std::bit_cast<double>(read64(require(call.arguments[0].AsInt()))));
    }, native_flags);
    builder.StaticMethod("nativeWriteString", "(ILjava/lang/String;)V",
        [require, write32, write](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt()); const auto ref = call.arguments[1].ref;
            if (!ref.IsValid()) { write32(p, 0xffffffffU); return dx::VmValue::Void(); }
            const auto value = call.vm.Model().StringValue(ref);
            write32(p, static_cast<std::uint32_t>(value.size()));
            for (const auto unit : value) {
                const std::array bytes{std::byte(unit), std::byte(unit >> 8U)}; write(p, bytes);
            }
            const std::array zero{std::byte{0}, std::byte{0}}; write(p, zero);
            while ((p.position & 3U) != 0U) { const std::array pad{std::byte{0}}; write(p, pad); }
            return dx::VmValue::Void();
        }, native_flags);
    builder.StaticMethod("nativeReadString", "(I)Ljava/lang/String;",
        [require, read32, read](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt()); const auto length = read32(p);
            if (length == 0xffffffffU) return dx::VmValue::Ref(dx::VmObjectRef{});
            if (length > (kMaxParcelBytes / 2U)) throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "string length"};
            std::u16string value; value.reserve(length);
            for (std::uint32_t i = 0; i < length; ++i) { const auto b = read(p, 2U); value.push_back(char16_t(std::uint16_t(b[0]) | (std::uint16_t(b[1]) << 8U))); }
            static_cast<void>(read(p, 2U)); while ((p.position & 3U) != 0U) static_cast<void>(read(p, 1U));
            return dx::VmValue::Ref(call.vm.Model().NewString(value));
        }, native_flags);
    builder.StaticMethod("nativeWriteByteArray", "(I[BII)V",
        [require, write32, write](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt()); const auto array = call.arguments[1].ref;
            const auto offset = call.arguments[2].AsInt(), length = call.arguments[3].AsInt();
            if (!array.IsValid()) { write32(p, 0xffffffffU); return dx::VmValue::Void(); }
            const auto total = call.vm.Model().ArrayLength(array);
            if (offset < 0 || length < 0 || offset > total || length > total - offset)
                throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "byte array range"};
            write32(p, static_cast<std::uint32_t>(length));
            const auto bytes = call.vm.Model().ReadByteRegion(array, offset, length); write(p, bytes);
            while ((p.position & 3U) != 0U) { const std::array pad{std::byte{0}}; write(p, pad); }
            return dx::VmValue::Void();
        }, native_flags);
    builder.StaticMethod("nativeCreateByteArray", "(I)[B",
        [require, read32, read](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt()); const auto length = read32(p);
            if (length == 0xffffffffU) return dx::VmValue::Ref(dx::VmObjectRef{});
            if (length > kMaxParcelBytes) throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "array length"};
            const auto bytes = read(p, length);
            const auto result = call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte, static_cast<JniSize>(length));
            call.vm.Model().WriteByteRegion(result, 0, bytes);
            while ((p.position & 3U) != 0U) static_cast<void>(read(p, 1U));
            return dx::VmValue::Ref(result);
        }, native_flags);
    builder.StaticMethod("nativeWriteStrongBinder", "(ILandroid/os/IBinder;)V",
        [require, write](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt()); const auto offset = p.position;
            std::array<std::byte, 16> flat{}; write(p, flat);
            if (call.arguments[1].ref.IsValid()) p.binders.push_back({offset, flat.size(), call.arguments[1].ref});
            return dx::VmValue::Void();
        }, native_flags);
    builder.StaticMethod("nativeReadStrongBinder", "(I)Landroid/os/IBinder;",
        [require, read](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt()); const auto offset = p.position;
            static_cast<void>(read(p, 16U));
            const auto it = std::find_if(p.binders.begin(), p.binders.end(), [offset](const auto& r) { return r.offset == offset; });
            return dx::VmValue::Ref(it == p.binders.end() ? dx::VmObjectRef{} : it->binder);
        }, native_flags);
    builder.StaticMethod("nativeHasFileDescriptors", "(I)Z", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); }, native_flags);
    const auto fd_unsupported = [](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Parcel file descriptors are unsupported"};
    };
    builder.StaticMethod("nativeWriteFileDescriptor", "(ILjava/io/FileDescriptor;)V", fd_unsupported, native_flags);
    builder.StaticMethod("nativeReadFileDescriptor", "(I)Ljava/io/FileDescriptor;", fd_unsupported, native_flags);
    constexpr auto package_native = dx::kAccStatic | dx::kAccNative;
    builder.StaticMethod("openFileDescriptor", "(Ljava/lang/String;I)Ljava/io/FileDescriptor;", fd_unsupported, package_native);
    builder.StaticMethod("dupFileDescriptor", "(Ljava/io/FileDescriptor;)Ljava/io/FileDescriptor;", fd_unsupported, package_native);
    builder.StaticMethod("closeFileDescriptor", "(Ljava/io/FileDescriptor;)V", fd_unsupported, package_native);
    builder.StaticMethod("clearFileDescriptor", "(Ljava/io/FileDescriptor;)V", fd_unsupported, package_native);
    builder.StaticMethod("nativeMarshall", "(I)[B", [require](dx::IntrinsicContext& call) {
        const auto& p = require(call.arguments[0].AsInt());
        if (!p.binders.empty()) throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "cannot marshall Binder objects"};
        const auto result = call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte, static_cast<JniSize>(p.bytes.size()));
        call.vm.Model().WriteByteRegion(result, 0, p.bytes); return dx::VmValue::Ref(result);
    }, native_flags);
    builder.StaticMethod("nativeUnmarshall", "(I[BII)V", [require, resize](dx::IntrinsicContext& call) {
        auto& p = require(call.arguments[0].AsInt()); const auto array = call.arguments[1].ref;
        const auto offset = call.arguments[2].AsInt(), length = call.arguments[3].AsInt();
        if (!array.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "data"};
        const auto total = call.vm.Model().ArrayLength(array);
        if (offset < 0 || length < 0 || offset > total || length > total - offset)
            throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "unmarshall range"};
        resize(p, static_cast<std::size_t>(length));
        const auto bytes = call.vm.Model().ReadByteRegion(array, offset, length);
        std::copy(bytes.begin(), bytes.end(), p.bytes.begin()); p.position = 0; p.binders.clear();
        return dx::VmValue::Void();
    }, native_flags);
    builder.StaticMethod("nativeAppendFrom", "(IIII)V", [require, write](dx::IntrinsicContext& call) {
        auto& target = require(call.arguments[0].AsInt()); auto& source = require(call.arguments[1].AsInt());
        const auto offset = call.arguments[2].AsInt(), length = call.arguments[3].AsInt();
        if (offset < 0 || length < 0 || static_cast<std::size_t>(offset) > source.bytes.size() ||
            static_cast<std::size_t>(length) > source.bytes.size() - static_cast<std::size_t>(offset))
            throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "append range"};
        const auto source_start = static_cast<std::size_t>(offset), source_end = source_start + static_cast<std::size_t>(length);
        for (const auto& record : source.binders) {
            const bool overlaps = record.offset < source_end && source_start < record.offset + record.span;
            const bool contained = record.offset >= source_start && record.offset + record.span <= source_end;
            if (overlaps && !contained) throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "partial Binder record append"};
        }
        const auto source_records = source.binders;
        const auto target_start = target.position;
        const std::vector copy(source.bytes.begin() + offset, source.bytes.begin() + offset + length);
        write(target, copy);
        for (const auto& record : source_records) if (record.offset >= source_start && record.offset + record.span <= source_end)
            target.binders.push_back({target_start + record.offset - source_start, record.span, record.binder});
        return dx::VmValue::Void();
    }, native_flags);
    builder.StaticMethod("nativeWriteInterfaceToken", "(ILjava/lang/String;)V",
        [context, require, write32, write](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            // API19 STRICT_MODE_PENALTY_GATHER. Recording a mask does not run detectors.
            write32(p, static_cast<std::uint32_t>(context->binder_threads[
                call.vm.CurrentContextToken()].strict_mode_policy) | 0x100U);
            const auto value = call.vm.Model().StringValue(call.arguments[1].ref); write32(p, static_cast<std::uint32_t>(value.size()));
            for (const auto unit : value) { const std::array bytes{std::byte(unit), std::byte(unit >> 8U)}; write(p, bytes); }
            const std::array zero{std::byte{0}, std::byte{0}}; write(p, zero);
            while ((p.position & 3U) != 0U) { const std::array pad{std::byte{0}}; write(p, pad); }
            return dx::VmValue::Void();
        }, native_flags);
    builder.StaticMethod("nativeEnforceInterface", "(ILjava/lang/String;)V",
        [context, require, read32, read](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            const auto policy = read32(p);
            const auto length = read32(p);
            const auto remaining = p.bytes.size() - p.position;
            if (length > kMaxParcelBytes / 2U ||
                (static_cast<std::size_t>(length) + 1U) * 2U > remaining)
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "invalid Binder interface length"};
            std::u16string actual; actual.reserve(length);
            for (std::uint32_t i = 0; i < length; ++i) { const auto b = read(p, 2U); actual.push_back(char16_t(std::uint16_t(b[0]) | (std::uint16_t(b[1]) << 8U))); }
            static_cast<void>(read(p, 2U)); while ((p.position & 3U) != 0U) static_cast<void>(read(p, 1U));
            if (actual != call.vm.Model().StringValue(call.arguments[1].ref))
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "Binder interface mismatch"};
            context->binder_threads[call.vm.CurrentContextToken()].strict_mode_policy =
                static_cast<std::int32_t>(policy);
            return dx::VmValue::Void();
        }, native_flags);
    return std::move(builder).Build();
}

Decl Declare_android_os_PowerManager_WakeLock(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/PowerManager$WakeLock;", "Ljava/lang/Object;");
    builder.FinalMethod("acquire", "()V", [context](dx::IntrinsicContext& call) {
        auto& state = context->wake_locks[call.receiver.Value()];
        state.count = state.reference_counted ? state.count + 1 : 1;
        return dx::VmValue::Void();
    }).FinalMethod("acquire", "(J)V", [context](dx::IntrinsicContext& call) {
        if (call.arguments[0].AsLong() <= 0)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "timeout"};
        auto& state = context->wake_locks[call.receiver.Value()];
        state.count = state.reference_counted ? state.count + 1 : 1;
        return dx::VmValue::Void();
    }).FinalMethod("release", "()V", [context](dx::IntrinsicContext& call) {
        auto& state = context->wake_locks[call.receiver.Value()];
        if (state.count == 0)
            throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", "WakeLock under-locked"};
        state.count = state.reference_counted ? state.count - 1 : 0;
        return dx::VmValue::Void();
    }).FinalMethod("isHeld", "()Z", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(context->wake_locks[call.receiver.Value()].count > 0);
    }).FinalMethod("setReferenceCounted", "(Z)V", [context](dx::IntrinsicContext& call) {
        context->wake_locks[call.receiver.Value()].reference_counted = call.arguments[0].AsInt() != 0;
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_os_ParcelFileDescriptor(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/os/ParcelFileDescriptor;", "Ljava/lang/Object;");
    builder.ConstantInt(
        "MODE_READ_ONLY", "I", 0x10000000,
        dx::kAccPublic | dx::kAccStatic | dx::kAccFinal);
    builder.InstanceField("mFd", "Ljava/io/FileDescriptor;",
                          dx::kAccPrivate | dx::kAccFinal);
    builder.StaticMethod(
        "open", "(Ljava/io/File;I)Landroid/os/ParcelFileDescriptor;",
        [](dx::IntrinsicContext& call) {
            constexpr std::int32_t kModeReadOnly = 0x10000000;
            if (call.arguments[1].AsInt() != kModeReadOnly) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "only MODE_READ_ONLY is supported"};
            }
            const auto path = FilePathOf(call, call.arguments[0].ref);
            const auto info = call.vm.IO().Stat(path);
            if (!info.has_value() || info->is_directory) {
                throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;",
                                      "file not found: " + path};
            }
            const auto fd = call.vm.NewIntrinsicInstance(
                "Ljava/io/FileDescriptor;");
            call.vm.IO().SetDescriptor(
                fd, {dx::IoRuntime::DescriptorKind::vfs_path, path, 0,
                     false, {}, {}, {}});
            const auto pfd = call.vm.NewIntrinsicInstance(
                "Landroid/os/ParcelFileDescriptor;");
            call.vm.Model().InstanceSlots(pfd)[0] = {
                fd.Value(), dx::SlotTag::ref};
            return dx::VmValue::Ref(pfd);
        });
    builder.FinalMethod("getFileDescriptor", "()Ljava/io/FileDescriptor;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(dx::VmObjectRef(
                call.vm.Model().InstanceSlots(call.receiver)[0].bits));
        });
    builder.FinalMethod("close", "()V", [](dx::IntrinsicContext& call) {
        const auto fd = dx::VmObjectRef(
            call.vm.Model().InstanceSlots(call.receiver)[0].bits);
        if (fd.IsValid()) call.vm.IO().CloseDescriptor(fd);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_os_PowerManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/PowerManager;", "Ljava/lang/Object;");
    constexpr auto constant_access =
        dx::kAccPublic | dx::kAccStatic | dx::kAccFinal;
    builder.ConstantInt("PARTIAL_WAKE_LOCK", "I", 1, constant_access)
        .ConstantInt("SCREEN_DIM_WAKE_LOCK", "I", 6, constant_access)
        .ConstantInt("SCREEN_BRIGHT_WAKE_LOCK", "I", 10, constant_access)
        .ConstantInt("FULL_WAKE_LOCK", "I", 26, constant_access)
        .ConstantInt("ACQUIRE_CAUSES_WAKEUP", "I", 0x10000000,
                     constant_access)
        .ConstantInt("ON_AFTER_RELEASE", "I", 0x20000000,
                     constant_access);
    builder.FinalMethod("newWakeLock", "(ILjava/lang/String;)Landroid/os/PowerManager$WakeLock;",
        [context](dx::IntrinsicContext& call) {
            if (!call.arguments[1].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "tag"};
            const auto level = call.arguments[0].AsInt();
            const auto base = level & 0xffff;
            if (base != 1 && base != 6 && base != 10 && base != 26)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "unsupported wake lock level"};
            const auto lock = call.vm.NewIntrinsicInstance("Landroid/os/PowerManager$WakeLock;");
            context->wake_locks[lock.Value()] = {level, call.vm.StringUtf8(call.arguments[1].ref), 0, true};
            return dx::VmValue::Ref(lock);
        });
    builder.FinalMethod("isScreenOn", "()Z", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    return std::move(builder).Build();
}

Decl Declare_android_os_Vibrator(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Vibrator;", "Ljava/lang/Object;");
    builder.FinalMethod("hasVibrator", "()Z", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.FinalMethod("vibrate", "(J)V", [context](dx::IntrinsicContext& call) {
        if (call.arguments[0].AsLong() < 0)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "milliseconds"};
        context->last_vibration_millis = call.arguments[0].AsLong();
        return dx::VmValue::Void();
    }).FinalMethod("cancel", "()V", [context](dx::IntrinsicContext&) {
        context->last_vibration_millis = 0; return dx::VmValue::Void();
    });
    builder.FinalMethod("vibrate", "([JI)V", [](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "repeating vibration is not provided"};
    });
    return std::move(builder).Build();
}

Decl Declare_android_os_Process(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Process;", "Ljava/lang/Object;");
    constexpr auto constant_access =
        dx::kAccPublic | dx::kAccStatic | dx::kAccFinal;
    builder.ConstantInt("SYSTEM_UID", "I", 1000, constant_access)
        .ConstantInt("FIRST_APPLICATION_UID", "I", 10000, constant_access)
        .ConstantInt("THREAD_PRIORITY_DEFAULT", "I", 0, constant_access)
        .ConstantInt("THREAD_PRIORITY_BACKGROUND", "I", 10, constant_access)
        .ConstantInt("THREAD_PRIORITY_URGENT_DISPLAY", "I", -8,
                     constant_access);
    builder.StaticMethod("myPid", "()I", [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); })
        .StaticMethod("myUid", "()I", [context](dx::IntrinsicContext&) {
            return dx::VmValue::Int(static_cast<std::int32_t>(context->application_uid));
        }).StaticMethod("myTid", "()I", [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); })
        .StaticMethod("killProcess", "(I)V", [context](dx::IntrinsicContext& call) {
            if (call.arguments[0].AsInt() != 1)
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "foreign process"};
            context->exit_requested = true; return dx::VmValue::Void();
        }).StaticMethod("setThreadPriority", "(I)V", [](dx::IntrinsicContext& call) {
            const auto priority = call.arguments[0].AsInt();
            if (priority < -20 || priority > 19)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "priority"};
            return dx::VmValue::Void();
        }).StaticMethod("setThreadPriority", "(II)V", [](dx::IntrinsicContext& call) {
            const auto priority = call.arguments[1].AsInt();
            if (priority < -20 || priority > 19)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "priority"};
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {

void RegisterAndroidValueStateTables(
    dexvm::Interpreter& vm,
    const std::shared_ptr<DexVmAndroidContext>& context) {
    if (context == nullptr) return;
    vm.RegisterIntrinsicStateTable({
        "android.preference-editors",
        [](dexvm::VmObjectRef, const dexvm::VmRootVisitor&) {},
        [context](const dexvm::VmObjectRef owner) {
            if (context->preference_editors.erase(owner.Value()) != 0)
                context->preference_names.erase(owner.Value());
        }, {}});
    vm.RegisterIntrinsicStateTable({
        "android.value",
        [context](const dexvm::VmObjectRef owner, const dexvm::VmRootVisitor& visit) {
            if (const auto connections = context->service_connections.find(owner.Value());
                connections != context->service_connections.end())
                for (const auto connection : connections->second) visit(connection);
            if (const auto token = context->parcel_owner_tokens.find(owner.Value());
                token != context->parcel_owner_tokens.end()) {
                if (const auto parcel = context->parcel_backings.find(token->second);
                    parcel != context->parcel_backings.end()) {
                    for (const auto& record : parcel->second.binders)
                        if (record.binder.IsValid()) visit(record.binder);
                }
            }
        },
        [context](const dexvm::VmObjectRef owner) {
            context->service_connections.erase(owner.Value());
            context->paths.erase(owner.Value());
            if (const auto token = context->parcel_owner_tokens.find(owner.Value());
                token != context->parcel_owner_tokens.end()) {
                context->parcel_backings.erase(token->second);
                context->parcel_owner_tokens.erase(token);
            }
            context->wake_locks.erase(owner.Value());
        }, {}});
}

}  // namespace ogplay::runtime

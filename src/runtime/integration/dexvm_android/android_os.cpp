// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_os_AsyncTask.cpp ----
#include <array>
#include <bit>
#include <cstddef>
#include <limits>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {

[[nodiscard]] DexVmAndroidContext::ParcelState& RequireParcel(
    dx::IntrinsicContext& call, const Context& context,
    const dx::VmObjectRef parcel) {
    static_cast<void>(call);
    if (!parcel.IsValid())
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "parcel"};
    const auto found = context->parcels.find(parcel.Value());
    if (found == context->parcels.end() || found->second.recycled)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Parcel recycled"};
    return found->second;
}

[[nodiscard]] std::size_t ParcelAtomBytes(
    const DexVmAndroidContext::ParcelAtom& atom) {
    using Kind = DexVmAndroidContext::ParcelAtom::Kind;
    switch (atom.kind) {
        case Kind::long_integer:
        case Kind::double_value: return 8U;
        case Kind::string: return 4U + atom.text.size();
        case Kind::byte_array: return 4U + atom.bytes.size();
        default: return 4U;
    }
}

void WriteParcelAtom(DexVmAndroidContext::ParcelState& parcel,
                     DexVmAndroidContext::ParcelAtom atom) {
    if (parcel.position < parcel.atoms.size())
        parcel.atoms[parcel.position] = std::move(atom);
    else
        parcel.atoms.push_back(std::move(atom));
    ++parcel.position;
}

[[nodiscard]] DexVmAndroidContext::ParcelAtom ReadParcelAtom(
    DexVmAndroidContext::ParcelState& parcel,
    const DexVmAndroidContext::ParcelAtom::Kind expected) {
    if (parcel.position >= parcel.atoms.size())
        throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "Parcel underflow"};
    const auto& atom = parcel.atoms[parcel.position++];
    if (atom.kind != expected)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Parcel type mismatch"};
    return atom;
}

}  // namespace
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics::dvm80_android_os_AsyncTask {

Decl Declare_android_os_AsyncTask(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/AsyncTask;", "Ljava/lang/Object;");
    builder.Constructor("()V", [context](dx::IntrinsicContext& call) {
        std::scoped_lock lock(context->scheduler_mutex);
        context->async_tasks.try_emplace(call.receiver.Value());
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("onPreExecute", "()V", NeutralHandler('V'));
    builder.VirtualMethod("onPostExecute", "(Ljava/lang/Object;)V",
                          NeutralHandler('V'));
    builder.VirtualMethod("onProgressUpdate", "([Ljava/lang/Object;)V",
                          NeutralHandler('V'));
    builder.VirtualMethod("onCancelled", "(Ljava/lang/Object;)V",
                          NeutralHandler('V'));
    builder.VirtualMethod("doInBackground", "([Ljava/lang/Object;)Ljava/lang/Object;",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "AsyncTask.doInBackground is not overridden"};
        });
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
Decl Declare_android_os_AsyncTask(const Context& context) {
    return dvm80_android_os_AsyncTask::Declare_android_os_AsyncTask(context);
}

Decl Declare_android_os_AsyncTask_Status(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/os/AsyncTask$Status;", "Ljava/lang/Enum;");
    builder.StaticField("PENDING", "Landroid/os/AsyncTask$Status;")
        .StaticField("RUNNING", "Landroid/os/AsyncTask$Status;")
        .StaticField("FINISHED", "Landroid/os/AsyncTask$Status;");
    builder.ClassInitializer([context](dx::IntrinsicContext& call) {
        const std::array entries{
            std::pair{"PENDING", "async_status_pending"},
            std::pair{"RUNNING", "async_status_running"},
            std::pair{"FINISHED", "async_status_finished"}};
        const auto enum_class = call.vm.Linker().FindClass("Ljava/lang/Enum;");
        const auto constructor = enum_class.has_value()
                                     ? call.vm.Linker().FindDirectMethod(
                                           *enum_class, "<init>",
                                           "(Ljava/lang/String;I)V")
                                     : std::nullopt;
        if (!constructor.has_value()) {
            throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                                 "Enum constructor is unavailable");
        }
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto [field, key] = entries[index];
            const auto value = Singleton(
                call, context, key, "Landroid/os/AsyncTask$Status;");
            const auto outcome = call.vm.Call(
                *constructor,
                std::vector<dx::VmValue>{
                    dx::VmValue::Ref(value),
                    dx::VmValue::Ref(call.vm.NewStringUtf8(field)),
                    dx::VmValue::Int(static_cast<std::int32_t>(index))});
            if (outcome.exception.IsValid()) {
                call.vm.SetPendingException(outcome.exception);
                return dx::VmValue::Void();
            }
            call.vm.SetIntrinsicStaticRef(
                "Landroid/os/AsyncTask$Status;", field,
                "Landroid/os/AsyncTask$Status;", value);
        }
        return dx::VmValue::Void();
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

// ---- migrated from android_os_Build_VERSION.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_os_Build_VERSION {

Decl Declare_android_os_Build_VERSION(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Build$VERSION;", "Ljava/lang/Object;");
    builder.ConstantInt("SDK_INT", "I", 19);
    builder.ConstantString("SDK", "19");
    builder.ConstantString("RELEASE", "4.4.4");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_Build_VERSION(const Context& context) {
    return dvm80_android_os_Build_VERSION::Declare_android_os_Build_VERSION(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_Build.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_os_Build {

Decl Declare_android_os_Build(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Build;", "Ljava/lang/Object;");
    builder.ConstantString("CPU_ABI", "armeabi");
    builder.ConstantString("DEVICE", "unknown");
    builder.ConstantString("MANUFACTURER", "unknown");
    builder.ConstantString("MODEL", "unknown");
    builder.ConstantString("PRODUCT", "unknown");
    builder.ConstantString("TAGS", "release-keys");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_Build(const Context& context) {
    return dvm80_android_os_Build::Declare_android_os_Build(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_Bundle.cpp ----
// Bundle handlers store key/value pairs in the per-session bundle map
// keyed by the receiver object handle.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_os_Bundle {

Decl Declare_android_os_Bundle(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/os/Bundle;", "Ljava/lang/Object;", {"Landroid/os/Parcelable;"});
    builder.StaticField("CREATOR", "Landroid/os/Parcelable$Creator;", 0x0019U);
    builder.ClassInitializer([](dx::IntrinsicContext& call) {
        call.vm.SetIntrinsicStaticRef(
            "Landroid/os/Bundle;", "CREATOR", "Landroid/os/Parcelable$Creator;",
            call.vm.NewIntrinsicInstance("Landroid/os/Bundle$1;"));
        return dx::VmValue::Void();
    });
    builder.Constructor("()V",
        [context](dx::IntrinsicContext& call) {
            context->bundles.try_emplace(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.Constructor("(Landroid/os/Parcel;)V",
        [context](dx::IntrinsicContext& call) {
            auto& parcel = RequireParcel(call, context, call.arguments[0].ref);
            const auto atom = ReadParcelAtom(
                parcel, DexVmAndroidContext::ParcelAtom::Kind::object);
            if (atom.text != "Bundle") {
                context->bundles.try_emplace(call.receiver.Value());
            } else {
                context->bundles[call.receiver.Value()] = atom.bundle_values;
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("get", "(Ljava/lang/String;)Ljava/lang/Object;",
        [context](dx::IntrinsicContext& call) {
            const auto bundle = context->bundles.find(call.receiver.Value());
            if (bundle == context->bundles.end()) {
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            const auto value =
                bundle->second.find(call.vm.StringUtf8(call.arguments[0].ref));
            if (value == bundle->second.end()) {
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            if (const auto* text = std::get_if<std::string>(&value->second)) {
                return MakeString(call, *text);
            }
            if (const auto* object =
                    std::get_if<dx::VmObjectRef>(&value->second)) {
                return dx::VmValue::Ref(*object);
            }
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("getInt", "(Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            const auto& values = context->bundles[call.receiver.Value()];
            const auto found =
                values.find(call.vm.StringUtf8(call.arguments[0].ref));
            const auto* value =
                found == values.end()
                    ? nullptr
                    : std::get_if<std::int32_t>(&found->second);
            return dx::VmValue::Int(value == nullptr ? 0 : *value);
        });
    builder.FinalMethod("getString", "(Ljava/lang/String;)Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            const auto& values = context->bundles[call.receiver.Value()];
            const auto found =
                values.find(call.vm.StringUtf8(call.arguments[0].ref));
            const auto* value =
                found == values.end()
                    ? nullptr
                    : std::get_if<std::string>(&found->second);
            return value == nullptr ? dx::VmValue::Ref(dx::VmObjectRef{})
                                    : MakeString(call, *value);
        });
    builder.FinalMethod("putString", "(Ljava/lang/String;Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            context->bundles[call.receiver.Value()]
                            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.vm.StringUtf8(call.arguments[1].ref);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("putInt", "(Ljava/lang/String;I)V",
        [context](dx::IntrinsicContext& call) {
            context->bundles[call.receiver.Value()]
                            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsInt();
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getLong", "(Ljava/lang/String;)J",
        [context](dx::IntrinsicContext& call) {
            const auto& values = context->bundles[call.receiver.Value()];
            const auto found =
                values.find(call.vm.StringUtf8(call.arguments[0].ref));
            const auto* value =
                found == values.end()
                    ? nullptr
                    : std::get_if<std::int64_t>(&found->second);
            return dx::VmValue::Long(value == nullptr ? 0 : *value);
        });
    builder.FinalMethod("putLong", "(Ljava/lang/String;J)V",
        [context](dx::IntrinsicContext& call) {
            context->bundles[call.receiver.Value()]
                            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsLong();
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getByteArray", "(Ljava/lang/String;)[B",
        [context](dx::IntrinsicContext& call) {
            const auto& values = context->bundles[call.receiver.Value()];
            const auto found =
                values.find(call.vm.StringUtf8(call.arguments[0].ref));
            const auto* value =
                found == values.end()
                    ? nullptr
                    : std::get_if<dx::VmObjectRef>(&found->second);
            return dx::VmValue::Ref(value == nullptr ? dx::VmObjectRef{}
                                                     : *value);
        });
    builder.FinalMethod("putByteArray", "(Ljava/lang/String;[B)V",
        [context](dx::IntrinsicContext& call) {
            context->bundles[call.receiver.Value()]
                            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].ref;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getParcelable",
        "(Ljava/lang/String;)Landroid/os/Parcelable;",
        [context](dx::IntrinsicContext& call) {
            const auto& values = context->bundles[call.receiver.Value()];
            const auto found = values.find(call.vm.StringUtf8(call.arguments[0].ref));
            const auto* value = found == values.end()
                                    ? nullptr
                                    : std::get_if<dx::VmObjectRef>(&found->second);
            return dx::VmValue::Ref(value == nullptr ? dx::VmObjectRef{} : *value);
        });
    builder.FinalMethod("putParcelable",
        "(Ljava/lang/String;Landroid/os/Parcelable;)V",
        [context](dx::IntrinsicContext& call) {
            context->bundles[call.receiver.Value()]
                            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].ref;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("describeContents", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.FinalMethod("writeToParcel", "(Landroid/os/Parcel;I)V",
        [context](dx::IntrinsicContext& call) {
            auto& parcel = RequireParcel(call, context, call.arguments[0].ref);
            DexVmAndroidContext::ParcelAtom atom;
            atom.kind = DexVmAndroidContext::ParcelAtom::Kind::object;
            atom.text = "Bundle";
            atom.bundle_values = context->bundles[call.receiver.Value()];
            WriteParcelAtom(parcel, std::move(atom));
            return dx::VmValue::Void();
        });
    builder.FinalMethod("containsKey", "(Ljava/lang/String;)Z",
        [context](dx::IntrinsicContext& call) {
            const auto& values = context->bundles[call.receiver.Value()];
            return dx::VmValue::Int(
                values.contains(call.vm.StringUtf8(call.arguments[0].ref))
                    ? 1
                    : 0);
        });
    builder.FinalMethod("clear", "()V",
        [context](dx::IntrinsicContext& call) {
            context->bundles[call.receiver.Value()].clear();
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_Bundle(const Context& context) {
    return dvm80_android_os_Bundle::Declare_android_os_Bundle(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_CountDownTimer.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_os_CountDownTimer {

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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_CountDownTimer(const Context& context) {
    return dvm80_android_os_CountDownTimer::Declare_android_os_CountDownTimer(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_Environment.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_os_Environment {

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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_Environment(const Context& context) {
    return dvm80_android_os_Environment::Declare_android_os_Environment(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_Handler.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_os_Handler {

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
Decl Declare_android_os_Handler(const Context& context) {
    return dvm80_android_os_Handler::Declare_android_os_Handler(context);
}

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
    builder.VirtualMethod("onLooperPrepared", "()V", NeutralHandler('V'));
    builder.VirtualMethod("run", "()V", [context](dx::IntrinsicContext& call) {
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

namespace ogplay::runtime::android_intrinsics::dvm80_android_os_IBinder {

Decl Declare_android_os_IBinder(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/os/IBinder;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_IBinder(const Context& context) {
    return dvm80_android_os_IBinder::Declare_android_os_IBinder(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_Looper.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_os_Looper {

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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_Looper(const Context& context) {
    return dvm80_android_os_Looper::Declare_android_os_Looper(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_Message.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_os_Message {

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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_Message(const Context& context) {
    return dvm80_android_os_Message::Declare_android_os_Message(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_StatFs.cpp ----
#include <algorithm>
#include <cstdint>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_os_StatFs {

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
Decl Declare_android_os_StatFs(const Context& context) {
    return dvm80_android_os_StatFs::Declare_android_os_StatFs(context);
}
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Parcelable(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/os/Parcelable;");
    builder.ConstantInt("CONTENTS_FILE_DESCRIPTOR", "I", 1, 0x0019U)
        .ConstantInt("PARCELABLE_WRITE_RETURN_VALUE", "I", 1, 0x0019U);
    builder.VirtualMethod("describeContents", "()I", [](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "Parcelable.describeContents is not implemented"};
    }).VirtualMethod("writeToParcel", "(Landroid/os/Parcel;I)V",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "Parcelable.writeToParcel is not implemented"};
        });
    return std::move(builder).Build();
}

Decl Declare_android_os_Parcelable_Creator(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/os/Parcelable$Creator;");
    builder.VirtualMethod("createFromParcel", "(Landroid/os/Parcel;)Ljava/lang/Object;",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "Parcelable.Creator is not implemented"};
        }).VirtualMethod("newArray", "(I)[Ljava/lang/Object;",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "Parcelable.Creator is not implemented"};
        });
    return std::move(builder).Build();
}

Decl Declare_android_os_Bundle_1(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/os/Bundle$1;", "Ljava/lang/Object;", {"Landroid/os/Parcelable$Creator;"});
    builder.Constructor("()V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("createFromParcel", "(Landroid/os/Parcel;)Ljava/lang/Object;",
        [context](dx::IntrinsicContext& call) {
            auto& parcel = RequireParcel(call, context, call.arguments[0].ref);
            const auto atom = ReadParcelAtom(parcel, DexVmAndroidContext::ParcelAtom::Kind::object);
            if (atom.text != "Bundle") return dx::VmValue::Ref(dx::VmObjectRef{});
            const auto result = call.vm.NewIntrinsicInstance("Landroid/os/Bundle;");
            context->bundles[result.Value()] = atom.bundle_values;
            return dx::VmValue::Ref(result);
        });
    builder.FinalMethod("newArray", "(I)[Ljava/lang/Object;",
        [](dx::IntrinsicContext& call) {
            const auto length = call.arguments[0].AsInt();
            if (length < 0)
                throw dx::VmJavaThrow{"Ljava/lang/NegativeArraySizeException;", "length"};
            const auto element = call.vm.Linker().ResolveDescriptor("Ljava/lang/Object;");
            return dx::VmValue::Ref(call.vm.Model().NewObjectArray(
                call.vm.Linker().ResolveDescriptor("[Ljava/lang/Object;"), element,
                static_cast<JniSize>(length)));
        });
    return std::move(builder).Build();
}

Decl Declare_android_os_Parcel(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Parcel;", "Ljava/lang/Object;");
    builder.Constructor("()V", [context](dx::IntrinsicContext& call) {
        context->parcels[call.receiver.Value()] = {};
        return dx::VmValue::Void();
    }, 0x0002U);
    builder.StaticMethod("obtain", "()Landroid/os/Parcel;", [context](dx::IntrinsicContext& call) {
        const auto parcel = call.vm.NewIntrinsicInstance("Landroid/os/Parcel;");
        context->parcels[parcel.Value()] = {};
        return dx::VmValue::Ref(parcel);
    });
    builder.FinalMethod("recycle", "()V", [context](dx::IntrinsicContext& call) {
        auto& parcel = context->parcels[call.receiver.Value()];
        parcel.atoms.clear(); parcel.position = 0; parcel.recycled = true;
        return dx::VmValue::Void();
    });
    const auto write_integer = [context](const DexVmAndroidContext::ParcelAtom::Kind kind) {
        return dx::IntrinsicHandler([context, kind](dx::IntrinsicContext& call) {
            auto& parcel = RequireParcel(call, context, call.receiver);
            DexVmAndroidContext::ParcelAtom atom; atom.kind = kind;
            atom.integer = kind == DexVmAndroidContext::ParcelAtom::Kind::integer
                               ? call.arguments[0].AsInt() : call.arguments[0].AsLong();
            WriteParcelAtom(parcel, std::move(atom)); return dx::VmValue::Void();
        });
    };
    builder.FinalMethod("writeInt", "(I)V", write_integer(DexVmAndroidContext::ParcelAtom::Kind::integer))
        .FinalMethod("writeLong", "(J)V", write_integer(DexVmAndroidContext::ParcelAtom::Kind::long_integer));
    builder.FinalMethod("writeFloat", "(F)V", [context](dx::IntrinsicContext& call) {
        auto& parcel = RequireParcel(call, context, call.receiver);
        DexVmAndroidContext::ParcelAtom atom; atom.kind = DexVmAndroidContext::ParcelAtom::Kind::float_value;
        atom.real = call.arguments[0].AsFloat(); WriteParcelAtom(parcel, std::move(atom));
        return dx::VmValue::Void();
    }).FinalMethod("writeDouble", "(D)V", [context](dx::IntrinsicContext& call) {
        auto& parcel = RequireParcel(call, context, call.receiver);
        DexVmAndroidContext::ParcelAtom atom; atom.kind = DexVmAndroidContext::ParcelAtom::Kind::double_value;
        atom.real = call.arguments[0].AsDouble(); WriteParcelAtom(parcel, std::move(atom));
        return dx::VmValue::Void();
    }).FinalMethod("writeString", "(Ljava/lang/String;)V", [context](dx::IntrinsicContext& call) {
        auto& parcel = RequireParcel(call, context, call.receiver);
        DexVmAndroidContext::ParcelAtom atom; atom.kind = DexVmAndroidContext::ParcelAtom::Kind::string;
        atom.integer = call.arguments[0].ref.IsValid() ? 0 : -1;
        if (call.arguments[0].ref.IsValid()) atom.text = call.vm.StringUtf8(call.arguments[0].ref);
        WriteParcelAtom(parcel, std::move(atom)); return dx::VmValue::Void();
    }).FinalMethod("writeByteArray", "([B)V", [context](dx::IntrinsicContext& call) {
        auto& parcel = RequireParcel(call, context, call.receiver);
        DexVmAndroidContext::ParcelAtom atom; atom.kind = DexVmAndroidContext::ParcelAtom::Kind::byte_array;
        atom.integer = call.arguments[0].ref.IsValid() ? 0 : -1;
        if (call.arguments[0].ref.IsValid()) atom.bytes = call.vm.Model().ReadByteRegion(
            call.arguments[0].ref, 0, call.vm.Model().ArrayLength(call.arguments[0].ref));
        WriteParcelAtom(parcel, std::move(atom)); return dx::VmValue::Void();
    }).FinalMethod("writeParcelable", "(Landroid/os/Parcelable;I)V",
        [context](dx::IntrinsicContext& call) {
            auto& parcel = RequireParcel(call, context, call.receiver);
            DexVmAndroidContext::ParcelAtom atom; atom.kind = DexVmAndroidContext::ParcelAtom::Kind::object;
            atom.object = call.arguments[0].ref;
            if (atom.object.IsValid()) {
                const auto descriptor = call.vm.Linker().Class(
                    call.vm.Model().ObjectClass(atom.object)).descriptor;
                if (descriptor == "Landroid/os/Bundle;") {
                    atom.text = "Bundle";
                    atom.bundle_values = context->bundles[atom.object.Value()];
                    atom.object = dx::VmObjectRef{};
                }
            }
            WriteParcelAtom(parcel, std::move(atom));
            return dx::VmValue::Void();
        }).FinalMethod("writeBundle", "(Landroid/os/Bundle;)V",
        [context](dx::IntrinsicContext& call) {
            auto& parcel = RequireParcel(call, context, call.receiver);
            DexVmAndroidContext::ParcelAtom atom; atom.kind = DexVmAndroidContext::ParcelAtom::Kind::object;
            if (call.arguments[0].ref.IsValid()) {
                atom.text = "Bundle";
                atom.bundle_values = context->bundles[call.arguments[0].ref.Value()];
            }
            WriteParcelAtom(parcel, std::move(atom));
            return dx::VmValue::Void();
        });
    builder.FinalMethod("readInt", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(ReadParcelAtom(
            RequireParcel(call, context, call.receiver), DexVmAndroidContext::ParcelAtom::Kind::integer).integer));
    }).FinalMethod("readLong", "()J", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(ReadParcelAtom(RequireParcel(call, context, call.receiver),
            DexVmAndroidContext::ParcelAtom::Kind::long_integer).integer);
    }).FinalMethod("readFloat", "()F", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Float(static_cast<float>(ReadParcelAtom(RequireParcel(call, context, call.receiver),
            DexVmAndroidContext::ParcelAtom::Kind::float_value).real));
    }).FinalMethod("readDouble", "()D", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Double(ReadParcelAtom(RequireParcel(call, context, call.receiver),
            DexVmAndroidContext::ParcelAtom::Kind::double_value).real);
    }).FinalMethod("readString", "()Ljava/lang/String;", [context](dx::IntrinsicContext& call) {
        const auto atom = ReadParcelAtom(RequireParcel(call, context, call.receiver),
            DexVmAndroidContext::ParcelAtom::Kind::string);
        return atom.integer < 0 ? dx::VmValue::Ref(dx::VmObjectRef{}) : MakeString(call, atom.text);
    }).FinalMethod("createByteArray", "()[B", [context](dx::IntrinsicContext& call) {
        const auto atom = ReadParcelAtom(RequireParcel(call, context, call.receiver),
            DexVmAndroidContext::ParcelAtom::Kind::byte_array);
        if (atom.integer < 0) return dx::VmValue::Ref(dx::VmObjectRef{});
        const auto result = call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[B"),
            JniPrimitiveKind::byte, static_cast<JniSize>(atom.bytes.size()));
        call.vm.Model().WriteByteRegion(result, 0, atom.bytes);
        return dx::VmValue::Ref(result);
    }).FinalMethod("readParcelable", "(Ljava/lang/ClassLoader;)Landroid/os/Parcelable;",
        [context](dx::IntrinsicContext& call) {
            const auto atom = ReadParcelAtom(RequireParcel(call, context, call.receiver),
                DexVmAndroidContext::ParcelAtom::Kind::object);
            if (atom.text == "Bundle") {
                const auto result = call.vm.NewIntrinsicInstance("Landroid/os/Bundle;");
                context->bundles[result.Value()] = atom.bundle_values;
                return dx::VmValue::Ref(result);
            }
            return dx::VmValue::Ref(atom.object);
        }).FinalMethod("readBundle", "()Landroid/os/Bundle;", [context](dx::IntrinsicContext& call) {
            const auto atom = ReadParcelAtom(RequireParcel(call, context, call.receiver),
                DexVmAndroidContext::ParcelAtom::Kind::object);
            if (atom.text != "Bundle") return dx::VmValue::Ref(dx::VmObjectRef{});
            const auto result = call.vm.NewIntrinsicInstance("Landroid/os/Bundle;");
            context->bundles[result.Value()] = atom.bundle_values;
            return dx::VmValue::Ref(result);
        });
    builder.FinalMethod("dataSize", "()I", [context](dx::IntrinsicContext& call) {
        const auto& parcel = RequireParcel(call, context, call.receiver);
        std::size_t bytes{}; for (const auto& atom : parcel.atoms) bytes += ParcelAtomBytes(atom);
        return dx::VmValue::Int(static_cast<std::int32_t>(bytes));
    }).FinalMethod("dataPosition", "()I", [context](dx::IntrinsicContext& call) {
        const auto& parcel = RequireParcel(call, context, call.receiver);
        std::size_t bytes{};
        for (std::size_t i = 0; i < parcel.position; ++i) bytes += ParcelAtomBytes(parcel.atoms[i]);
        return dx::VmValue::Int(static_cast<std::int32_t>(bytes));
    }).FinalMethod("setDataPosition", "(I)V", [context](dx::IntrinsicContext& call) {
        auto& parcel = RequireParcel(call, context, call.receiver);
        const auto target = call.arguments[0].AsInt();
        if (target < 0) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "position"};
        std::size_t bytes{}; std::size_t index{};
        while (index < parcel.atoms.size() && bytes < static_cast<std::size_t>(target))
            bytes += ParcelAtomBytes(parcel.atoms[index++]);
        if (bytes != static_cast<std::size_t>(target))
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "position is not an item boundary"};
        parcel.position = index; return dx::VmValue::Void();
    });
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

Decl Declare_android_os_PowerManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/PowerManager;", "Ljava/lang/Object;");
    builder.ConstantInt("PARTIAL_WAKE_LOCK", "I", 1, 0x0019U)
        .ConstantInt("SCREEN_DIM_WAKE_LOCK", "I", 6, 0x0019U)
        .ConstantInt("SCREEN_BRIGHT_WAKE_LOCK", "I", 10, 0x0019U)
        .ConstantInt("FULL_WAKE_LOCK", "I", 26, 0x0019U)
        .ConstantInt("ACQUIRE_CAUSES_WAKEUP", "I", 0x10000000, 0x0019U)
        .ConstantInt("ON_AFTER_RELEASE", "I", 0x20000000, 0x0019U);
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
    builder.ConstantInt("SYSTEM_UID", "I", 1000, 0x0019U)
        .ConstantInt("FIRST_APPLICATION_UID", "I", 10000, 0x0019U)
        .ConstantInt("THREAD_PRIORITY_DEFAULT", "I", 0, 0x0019U)
        .ConstantInt("THREAD_PRIORITY_BACKGROUND", "I", 10, 0x0019U)
        .ConstantInt("THREAD_PRIORITY_URGENT_DISPLAY", "I", -8, 0x0019U);
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
        "android.value",
        [context](const dexvm::VmObjectRef owner, const dexvm::VmRootVisitor& visit) {
            if (const auto sparse = context->sparse_arrays.find(owner.Value());
                sparse != context->sparse_arrays.end()) {
                for (const auto& entry : sparse->second)
                    if (entry.value.IsValid()) visit(entry.value);
            }
            if (const auto parcel = context->parcels.find(owner.Value());
                parcel != context->parcels.end()) {
                for (const auto& atom : parcel->second.atoms)
                    if (atom.object.IsValid()) visit(atom.object);
                for (const auto& atom : parcel->second.atoms) {
                    for (const auto& [_, value] : atom.bundle_values) {
                        if (const auto* ref = std::get_if<dexvm::VmObjectRef>(&value);
                            ref != nullptr && ref->IsValid()) visit(*ref);
                    }
                }
            }
        },
        [context](const dexvm::VmObjectRef owner) {
            context->sparse_arrays.erase(owner.Value());
            context->sparse_int_arrays.erase(owner.Value());
            context->paths.erase(owner.Value());
            context->parcels.erase(owner.Value());
            context->wake_locks.erase(owner.Value());
            context->bundles.erase(owner.Value());
        }, {}});
}

}  // namespace ogplay::runtime

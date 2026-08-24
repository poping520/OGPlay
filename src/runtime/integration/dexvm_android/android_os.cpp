// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_os_AsyncTask.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_os_AsyncTask {

Decl Declare_android_os_AsyncTask(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/AsyncTask;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_AsyncTask(const Context& context) {
    return dvm80_android_os_AsyncTask::Declare_android_os_AsyncTask(context);
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
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Bundle;", "Ljava/lang/Object;");
    builder.Constructor("()V",
        [context](dx::IntrinsicContext& call) {
            context->bundles.try_emplace(call.receiver.Value());
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
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/CountDownTimer;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
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

Decl Declare_android_os_Handler(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Handler;", "Ljava/lang/Object;");
    const auto noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Constructor("()V", noop);
    builder.Constructor("(Landroid/os/Looper;)V", noop);
    builder.FinalMethod("obtainMessage", "()Landroid/os/Message;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/os/Message;"));
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
    builder.FinalMethod("sendMessage", "(Landroid/os/Message;)Z",
        [](dx::IntrinsicContext& call) {
            DeliverMessage(call, call.receiver, call.arguments[0].ref);
            return dx::VmValue::Int(1);
        });
    builder.FinalMethod("dispatchMessage", "(Landroid/os/Message;)V",
        [](dx::IntrinsicContext& call) {
            DeliverMessage(call, call.receiver, call.arguments[0].ref);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("post", "(Ljava/lang/Runnable;)Z",
        [](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            auto& linker = vm.Linker();
            const auto runnable = call.arguments[0].ref;
            if (!runnable.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "posted Runnable is null"};
            }
            const auto runnable_class = vm.Model().ObjectClass(runnable);
            const auto index =
                linker.FindVtableIndex(runnable_class, "run", "()V");
            if (!index.has_value()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "posted object has no run()"};
            }
            const auto outcome = vm.Call(
                linker.Class(runnable_class).vtable[*index],
                std::vector<dx::VmValue>{dx::VmValue::Ref(runnable)});
            if (outcome.exception.IsValid()) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/RuntimeException;",
                    "posted run() raised: " + outcome.exception_message};
            }
            return dx::VmValue::Int(1);
        });
    builder.VirtualMethod("handleMessage", "(Landroid/os/Message;)V", noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_Handler(const Context& context) {
    return dvm80_android_os_Handler::Declare_android_os_Handler(context);
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
    const auto noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.StaticMethod("prepare", "()V", noop);
    builder.StaticMethod("loop", "()V", noop);
    builder.StaticMethod("getMainLooper", "()Landroid/os/Looper;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(call, context, "main_looper", "Landroid/os/Looper;"));
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
        DeliverMessage(call, dx::VmObjectRef(slots[4].bits), call.receiver);
        return dx::VmValue::Void();
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

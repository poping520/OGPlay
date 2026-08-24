// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_util_Log.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_util_Log {

Decl Declare_android_util_Log(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/util/Log;", "Ljava/lang/Object;");
    const auto log = [](const core::LogLevel level) {
        return dx::IntrinsicHandler([level](dx::IntrinsicContext& call) {
            GuestLog(call, level,
                     call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                         call.vm.StringUtf8(call.arguments[1].ref));
            return dx::VmValue::Int(0);
        });
    };
    const auto debug = log(core::LogLevel::debug);
    const auto error = log(core::LogLevel::error);
    builder.StaticMethod("d", "(Ljava/lang/String;Ljava/lang/String;)I", debug);
    builder.StaticMethod("e", "(Ljava/lang/String;Ljava/lang/String;)I", error);
    builder.StaticMethod("i", "(Ljava/lang/String;Ljava/lang/String;)I", log(core::LogLevel::info));
    builder.StaticMethod("w", "(Ljava/lang/String;Ljava/lang/String;)I", log(core::LogLevel::warn));
    builder.StaticMethod("v", "(Ljava/lang/String;Ljava/lang/String;)I", debug);
    builder.StaticMethod("e", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", error);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_util_Log(const Context& context) {
    return dvm80_android_util_Log::Declare_android_util_Log(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_util_Pair.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_util_Pair {

Decl Declare_android_util_Pair(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/util/Pair;", "Ljava/lang/Object;");
    builder.InstanceField("first", "Ljava/lang/Object;");
    builder.InstanceField("second", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/lang/Object;Ljava/lang/Object;)V",
        [](dx::IntrinsicContext& call) {
            const auto slots = call.vm.Model().InstanceSlots(call.receiver);
            slots[0] = {call.arguments[0].ref.Value(), dx::SlotTag::ref};
            slots[1] = {call.arguments[1].ref.Value(), dx::SlotTag::ref};
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_util_Pair(const Context& context) {
    return dvm80_android_util_Pair::Declare_android_util_Pair(context);
}
}  // namespace ogplay::runtime::android_intrinsics

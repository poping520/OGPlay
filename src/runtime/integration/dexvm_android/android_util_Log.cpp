#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_util_Log(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/util/Log;");
    builder.Super("Ljava/lang/Object;");
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
    builder.Static("d", "(Ljava/lang/String;Ljava/lang/String;)I", debug);
    builder.Static("e", "(Ljava/lang/String;Ljava/lang/String;)I", error);
    builder.Static("i", "(Ljava/lang/String;Ljava/lang/String;)I", log(core::LogLevel::info));
    builder.Static("w", "(Ljava/lang/String;Ljava/lang/String;)I", log(core::LogLevel::warn));
    builder.Static("v", "(Ljava/lang/String;Ljava/lang/String;)I", debug);
    builder.Static("e", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", error);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_provider_Settings_System(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/provider/Settings$System;");
    builder.Super("Ljava/lang/Object;");
    // System settings table shares the session-lifetime preference store.
    builder.Static("getInt", "(Landroid/content/ContentResolver;Ljava/lang/String;I)I",
        [context](dx::IntrinsicContext& call) {
            const auto key = call.vm.StringUtf8(call.arguments[1].ref);
            auto& store = context->preferences["__android.settings.system"];
            const auto found = store.find(key);
            if (found != store.end()) {
                if (const auto* value = std::get_if<std::int32_t>(
                        &found->second)) {
                    return dx::VmValue::Int(*value);
                }
            }
            return dx::VmValue::Int(call.arguments[2].AsInt());
        });
    builder.Static("putInt", "(Landroid/content/ContentResolver;Ljava/lang/String;I)Z",
        [context](dx::IntrinsicContext& call) {
            const auto key = call.vm.StringUtf8(call.arguments[1].ref);
            context->preferences["__android.settings.system"][key] =
                call.arguments[2].AsInt();
            return dx::VmValue::Int(1);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

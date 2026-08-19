#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_provider_Settings_System(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/provider/Settings$System;", "Ljava/lang/Object;");
    // System settings table shares the session-lifetime preference store.
    builder.StaticMethod("getInt", "(Landroid/content/ContentResolver;Ljava/lang/String;I)I",
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
    builder.StaticMethod("putInt", "(Landroid/content/ContentResolver;Ljava/lang/String;I)Z",
        [context](dx::IntrinsicContext& call) {
            const auto key = call.vm.StringUtf8(call.arguments[1].ref);
            context->preferences["__android.settings.system"][key] =
                call.arguments[2].AsInt();
            return dx::VmValue::Int(1);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

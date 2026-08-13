#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_Resources(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/content/res/Resources;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getConfiguration", "()Landroid/content/res/Configuration;",
        [context](dx::IntrinsicContext& call) {
            const auto instance = Singleton(call, context, "configuration",
                "Landroid/content/res/Configuration;");
            // keyboard = KEYBOARD_NOKEYS (1): desktop host has no guest
            // keypad.
            const auto slots = call.vm.Model().InstanceSlots(instance);
            slots[0] = {1U, dx::SlotTag::cat1};
            return dx::VmValue::Ref(instance);
        });
    builder.Virtual("getIdentifier",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            const auto entry_name = call.vm.StringUtf8(call.arguments[0].ref);
            const auto type_name = call.vm.StringUtf8(call.arguments[1].ref);
            const auto* entry = context->arsc.FindByName(type_name, entry_name);
            return dx::VmValue::Int(
                entry == nullptr
                    ? 0
                    : static_cast<std::int32_t>(entry->resource_id));
        });
    builder.Virtual("openRawResource", "(I)Ljava/io/InputStream;",
        [context](dx::IntrinsicContext& call) {
            const auto resource_id =
                static_cast<std::uint32_t>(call.arguments[0].AsInt());
            const auto* entry = context->arsc.FindById(resource_id);
            if (entry == nullptr || !entry->string_value.has_value()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                    "resource id has no file entry: " +
                        std::to_string(resource_id)};
            }
            return dx::VmValue::Ref(OpenStream(
                call, context, ReadApkFile(context, *entry->string_value)));
        });
    builder.Virtual("getString", "(I)Ljava/lang/String;",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "string resources are not provided yet"};
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

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

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Environment(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/os/Environment;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("getExternalStorageDirectory", "()Ljava/io/File;",
        [context](dx::IntrinsicContext& call) {
            const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
            const auto slots = call.vm.Model().InstanceSlots(file);
            slots[0] = {
                call.vm.NewStringUtf8(context->external_storage_root).Value(),
                dx::SlotTag::ref};
            return dx::VmValue::Ref(file);
        });
    builder.Static("getExternalStorageState", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            // The external mount is required by the profile and read at
            // startup, so MEDIA_MOUNTED is the truthful state.
            return MakeString(call, "mounted");
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

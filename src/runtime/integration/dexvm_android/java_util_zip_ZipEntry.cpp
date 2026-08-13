#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_zip_ZipEntry(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljava/util/zip/ZipEntry;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("name", "Ljava/lang/String;", false);
    builder.Virtual("getName", "()Ljava/lang/String;",
        [](dx::IntrinsicContext& call) {
            const auto slots = call.vm.Model().InstanceSlots(call.receiver);
            return dx::VmValue::Ref(dx::VmObjectRef(slots[0].bits));
        });
    builder.Virtual("isDirectory", "()Z",
        [](dx::IntrinsicContext& call) {
            const auto slots = call.vm.Model().InstanceSlots(call.receiver);
            const auto name =
                call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits));
            return dx::VmValue::Int(!name.empty() && name.back() == '/' ? 1
                                                                        : 0);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

#include <algorithm>
#include <cstdint>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_StatFs(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/os/StatFs;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](dx::IntrinsicContext&) {
            // Only the external volume is queryable on this platform; the
            // constructor path argument selects nothing further.
            return dx::VmValue::Void();
        });
    builder.Virtual("getBlockSize", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(4096);
    });
    builder.Virtual("getAvailableBlocks", "()I",
        [context](dx::IntrinsicContext&) {
            const auto blocks = context->external_free_bytes / 4096U;
            return dx::VmValue::Int(static_cast<std::int32_t>(
                std::min<std::uint64_t>(blocks, INT32_MAX)));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

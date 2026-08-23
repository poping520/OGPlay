#include <cstdint>
#include <utility>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_AssetFileDescriptor(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/res/AssetFileDescriptor;", "Ljava/lang/Object;");
    builder.InstanceField("mLength", "J", 0x0012U);
    builder.FinalMethod("getLength", "()J",
        [](dx::IntrinsicContext& call) {
            const auto slots = call.vm.Model().InstanceSlots(call.receiver);
            const auto bits = static_cast<std::uint64_t>(slots[0].bits) |
                              (static_cast<std::uint64_t>(slots[1].bits)
                               << 32U);
            return dx::VmValue::Long(static_cast<std::int64_t>(bits));
        });
    builder.FinalMethod("close", "()V", [](dx::IntrinsicContext&) {
        // This logical descriptor owns no host or guest fd/lease. Closing an
        // empty resource set is intentionally idempotent.
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

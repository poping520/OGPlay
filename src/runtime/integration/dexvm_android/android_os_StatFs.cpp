#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_StatFs(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/os/StatFs;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Ljava/lang/String;)V", handlers.handler_android_statfs_init);
    builder.Virtual("getBlockSize", "()I", handlers.handler_android_statfs_get_block_size);
    builder.Virtual("getAvailableBlocks", "()I", handlers.handler_android_statfs_get_available_blocks);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

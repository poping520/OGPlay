#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Bundle(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/os/Bundle;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", handlers.handler_android_bundle_init);
    builder.Virtual("get", "(Ljava/lang/String;)Ljava/lang/Object;", handlers.handler_android_bundle_get);
    builder.Virtual("getInt", "(Ljava/lang/String;)I", handlers.handler_android_bundle_get_int);
    builder.Virtual("getString", "(Ljava/lang/String;)Ljava/lang/String;", handlers.handler_android_bundle_get_string);
    builder.Virtual("putString", "(Ljava/lang/String;Ljava/lang/String;)V", handlers.handler_android_bundle_put_string);
    builder.Virtual("putInt", "(Ljava/lang/String;I)V", handlers.handler_android_bundle_put_int);
    builder.Virtual("getLong", "(Ljava/lang/String;)J", handlers.handler_android_bundle_get_long);
    builder.Virtual("putLong", "(Ljava/lang/String;J)V", handlers.handler_android_bundle_put_long);
    builder.Virtual("getByteArray", "(Ljava/lang/String;)[B", handlers.handler_android_bundle_get_byte_array);
    builder.Virtual("putByteArray", "(Ljava/lang/String;[B)V", handlers.handler_android_bundle_put_byte_array);
    builder.Virtual("containsKey", "(Ljava/lang/String;)Z", handlers.handler_android_bundle_contains);
    builder.Virtual("clear", "()V", handlers.handler_android_bundle_clear);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_File(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/File;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("path", "Ljava/lang/String;", false);
    builder.Virtual("<init>", "(Ljava/lang/String;)V", handlers.handler_android_file_init);
    builder.Virtual("<init>", "(Ljava/lang/String;Ljava/lang/String;)V", handlers.handler_android_file_init_parent_child);
    builder.Virtual("exists", "()Z", handlers.handler_android_file_exists);
    builder.Virtual("length", "()J", handlers.handler_android_file_length);
    builder.Virtual("getPath", "()Ljava/lang/String;", handlers.handler_android_file_get_path);
    builder.Virtual("getAbsolutePath", "()Ljava/lang/String;", handlers.handler_android_file_get_path);
    builder.Virtual("mkdir", "()Z", handlers.handler_android_file_mkdirs);
    builder.Virtual("mkdirs", "()Z", handlers.handler_android_file_mkdirs);
    builder.Virtual("createNewFile", "()Z", handlers.handler_android_file_create_new);
    builder.Virtual("delete", "()Z", handlers.handler_android_file_delete);
    builder.Virtual("isDirectory", "()Z", handlers.handler_android_file_is_directory);
    builder.Virtual("list", "()[Ljava/lang/String;", handlers.handler_android_file_list);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

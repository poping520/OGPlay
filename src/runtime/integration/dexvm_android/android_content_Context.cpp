#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_Context(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/content/Context;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", handlers.handler_android_context_init);
    builder.Virtual("getAssets", "()Landroid/content/res/AssetManager;", handlers.handler_android_context_get_assets);
    builder.Virtual("getPackageName", "()Ljava/lang/String;", handlers.handler_android_context_get_package_name);
    builder.Virtual("getResources", "()Landroid/content/res/Resources;", handlers.handler_android_context_get_resources);
    builder.Virtual("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", handlers.handler_android_context_get_system_service);
    builder.Virtual("registerReceiver", "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;", handlers.handler_android_context_register_receiver);
    builder.Virtual("startActivity", "(Landroid/content/Intent;)V", handlers.handler_android_context_start_activity);
    builder.Virtual("getSharedPreferences", "(Ljava/lang/String;I)Landroid/content/SharedPreferences;", handlers.handler_android_context_get_shared_preferences);
    builder.Virtual("getContentResolver", "()Landroid/content/ContentResolver;", handlers.handler_android_context_get_content_resolver);
    builder.Virtual("sendBroadcast", "(Landroid/content/Intent;)V", handlers.handler_android_context_send_broadcast);
    builder.Virtual("getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;", handlers.handler_android_context_get_external_files_dir);
    builder.Virtual("startService", "(Landroid/content/Intent;)Landroid/content/ComponentName;", handlers.handler_android_context_start_service_none);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

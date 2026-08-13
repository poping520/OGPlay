#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_Intent(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/content/Intent;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Ljava/lang/String;)V", handlers.handler_android_intent_init);
    builder.Virtual("<init>", "()V", handlers.handler_android_intent_init);
    builder.Virtual("<init>", "(Ljava/lang/String;Landroid/net/Uri;)V", handlers.handler_android_intent_init);
    builder.Virtual("<init>", "(Landroid/content/Context;Ljava/lang/Class;)V", handlers.handler_android_intent_init_component);
    builder.Virtual("setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;", handlers.handler_android_intent_set_class_name);
    builder.Virtual("addFlags", "(I)Landroid/content/Intent;", handlers.handler_android_intent_set_flags);
    builder.Virtual("putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;", handlers.handler_android_intent_put_extra_int);
    builder.Virtual("putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;", handlers.handler_android_intent_put_extra_string);
    builder.Virtual("getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;", handlers.handler_android_intent_get_string_extra);
    builder.Virtual("getIntExtra", "(Ljava/lang/String;I)I", handlers.handler_android_intent_get_int_extra);
    builder.Virtual("addCategory", "(Ljava/lang/String;)Landroid/content/Intent;", handlers.handler_android_intent_set_flags);
    builder.Virtual("getAction", "()Ljava/lang/String;", handlers.handler_android_intent_get_action);
    builder.Virtual("getExtras", "()Landroid/os/Bundle;", handlers.handler_android_intent_get_extras);
    builder.Virtual("setFlags", "(I)Landroid/content/Intent;", handlers.handler_android_intent_set_flags);
    builder.Virtual("setDataAndType", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;", handlers.handler_android_intent_set_data_and_type);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_IntentFilter(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/content/IntentFilter;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Ljava/lang/String;)V", handlers.handler_android_intent_filter_init);
    builder.Virtual("<init>", "()V", handlers.handler_android_intent_filter_init_empty);
    builder.Virtual("addAction", "(Ljava/lang/String;)V", handlers.handler_android_intent_filter_add_action);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

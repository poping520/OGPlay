#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TextView(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/TextView;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", handlers.handler_android_view_init);
    builder.Virtual("setText", "(Ljava/lang/CharSequence;)V", handlers.handler_android_textview_set_text);
    builder.Virtual("getText", "()Ljava/lang/CharSequence;", handlers.handler_android_textview_get_text);
    builder.Virtual("setTextColor", "(I)V", handlers.handler_android_widget_noop);
    builder.Virtual("setTextSize", "(F)V", handlers.handler_android_widget_noop);
    builder.Virtual("setTextSize", "(IF)V", handlers.handler_android_widget_noop);
    builder.Virtual("setLines", "(I)V", handlers.handler_android_widget_noop);
    builder.Virtual("setMaxLines", "(I)V", handlers.handler_android_widget_noop);
    builder.Virtual("setMaxWidth", "(I)V", handlers.handler_android_widget_noop);
    builder.Virtual("setGravity", "(I)V", handlers.handler_android_widget_noop);
    builder.Virtual("setId", "(I)V", handlers.handler_android_widget_noop);
    builder.Virtual("setTypeface", "(Landroid/graphics/Typeface;)V", handlers.handler_android_widget_noop);
    builder.Virtual("getPaint", "()Landroid/text/TextPaint;", handlers.handler_android_textview_get_paint);
    builder.Virtual("addTextChangedListener", "(Landroid/text/TextWatcher;)V", handlers.handler_android_widget_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

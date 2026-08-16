#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TextView(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/widget/TextView;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V",
                    ViewInitHandler(context));
    // TextView text is real state in the interpreter's builder buffer so
    // interpreted logic round-trips what it stored.
    builder.Virtual("setText", "(Ljava/lang/CharSequence;)V",
        [](dx::IntrinsicContext& call) {
            auto& buffer = call.vm.BuilderBuffer(call.receiver);
            const auto value = call.arguments[0].ref;
            buffer = value.IsValid()
                         ? call.vm.Model().StringValue(value)
                         : std::u16string();
            return dx::VmValue::Void();
        });
    builder.Virtual("getText", "()Ljava/lang/CharSequence;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.Model().NewString(
                    call.vm.BuilderBuffer(call.receiver)));
        });
    builder.Virtual("setTextColor", "(I)V", WidgetNoopHandler());
    builder.Virtual("setTextSize", "(F)V", WidgetNoopHandler());
    builder.Virtual("setTextSize", "(IF)V", WidgetNoopHandler());
    builder.Virtual("setLines", "(I)V", WidgetNoopHandler());
    builder.Virtual("setMaxLines", "(I)V", WidgetNoopHandler());
    builder.Virtual("setMaxWidth", "(I)V", WidgetNoopHandler());
    builder.Virtual("setGravity", "(I)V", WidgetNoopHandler());
    builder.Virtual("setId", "(I)V", ViewSetIdHandler(context));
    builder.Virtual("setTypeface", "(Landroid/graphics/Typeface;)V", WidgetNoopHandler());
    builder.Virtual("getPaint", "()Landroid/text/TextPaint;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "text_paint",
                          "Landroid/text/TextPaint;"));
        });
    builder.Virtual("addTextChangedListener", "(Landroid/text/TextWatcher;)V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

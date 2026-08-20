#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {

ui::UiNodeId TextNode(dx::IntrinsicContext& call, const Context& context) {
    const auto descriptor = call.vm.Linker()
                                .Class(call.vm.Model().ObjectClass(call.receiver))
                                .descriptor;
    return EnsureViewUiNode(
        *context, call.receiver, UiClassForDescriptor(descriptor));
}

std::uint32_t AndroidColorToRgba(const std::uint32_t argb) {
    return ((argb & 0x00ffffffU) << 8U) | (argb >> 24U);
}

}  // namespace

Decl Declare_android_widget_TextView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TextView;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V",
                    ViewInitHandler(context));
    builder.FinalMethod("setText", "(Ljava/lang/CharSequence;)V",
        [context](dx::IntrinsicContext& call) {
            const auto value = call.arguments[0].ref;
            auto text = value.IsValid() ? call.vm.Model().StringValue(value)
                                        : std::u16string();
            try {
                static_cast<void>(ui::MeasureFixedText(text, 8.0F));
            } catch (const std::runtime_error& error) {
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      error.what()};
            }
            const auto node = TextNode(call, context);
            context->ui_tree.Get(node)->text = std::move(text);
            context->ui_tree.MarkLayoutDirty(node);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getText", "()Ljava/lang/CharSequence;",
        [context](dx::IntrinsicContext& call) {
            const auto node = TextNode(call, context);
            return dx::VmValue::Ref(call.vm.Model().NewString(
                context->ui_tree.Get(node)->text));
        });
    builder.FinalMethod("setTextColor", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = TextNode(call, context);
            context->ui_tree.Get(node)->text_color = AndroidColorToRgba(
                static_cast<std::uint32_t>(call.arguments[0].AsInt()));
            context->ui_tree.MarkDrawDirty(node);
            return dx::VmValue::Void();
        });
    const auto set_text_size = [context](dx::IntrinsicContext& call,
                                         const std::size_t index) {
        const auto size = call.arguments[index].AsFloat();
        try {
            static_cast<void>(ui::MeasureFixedText(u"", size));
        } catch (const std::runtime_error& error) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  error.what()};
        }
        const auto node = TextNode(call, context);
        context->ui_tree.Get(node)->text_size_px = size;
        context->ui_tree.MarkLayoutDirty(node);
        return dx::VmValue::Void();
    };
    builder.FinalMethod("setTextSize", "(F)V",
        [set_text_size](dx::IntrinsicContext& call) {
            return set_text_size(call, 0);
        });
    builder.FinalMethod("setTextSize", "(IF)V",
        [set_text_size](dx::IntrinsicContext& call) {
            const auto unit = call.arguments[0].AsInt();
            if (unit < 0 || unit > 2) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "unsupported TextView dimension unit"};
            }
            return set_text_size(call, 1);
        });
    const auto one_line = [context](dx::IntrinsicContext& call) {
        if (call.arguments[0].AsInt() != 1) {
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "multiline TextView is unsupported"};
        }
        const auto node = TextNode(call, context);
        context->ui_tree.Get(node)->max_lines = 1;
        return dx::VmValue::Void();
    };
    builder.FinalMethod("setLines", "(I)V", one_line);
    builder.FinalMethod("setMaxLines", "(I)V", one_line);
    builder.FinalMethod("setSingleLine", "()V",
        [context](dx::IntrinsicContext& call) {
            const auto node = TextNode(call, context);
            context->ui_tree.Get(node)->max_lines = 1;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setSingleLine", "(Z)V",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[0].AsInt() == 0) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "multiline TextView is unsupported"};
            }
            const auto node = TextNode(call, context);
            context->ui_tree.Get(node)->max_lines = 1;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setMaxWidth", "(I)V", WidgetNoopHandler());
    builder.FinalMethod("setGravity", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = TextNode(call, context);
            context->ui_tree.Get(node)->gravity =
                static_cast<std::uint32_t>(call.arguments[0].AsInt());
            context->ui_tree.MarkLayoutDirty(node);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setTypeface", "(Landroid/graphics/Typeface;)V", WidgetNoopHandler());
    builder.FinalMethod("getPaint", "()Landroid/text/TextPaint;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "text_paint",
                          "Landroid/text/TextPaint;"));
        });
    builder.FinalMethod("addTextChangedListener", "(Landroid/text/TextWatcher;)V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

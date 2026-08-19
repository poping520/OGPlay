#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_TextPaint(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/text/TextPaint;", "Landroid/graphics/Paint;");
    builder.FinalMethod("getTextBounds", "(Ljava/lang/String;IILandroid/graphics/Rect;)V",
        [](dx::IntrinsicContext& call) {
            // No font engine exists; the v1 metric is a deterministic
            // monospace estimate (8x16 px per glyph) so layout math stays
            // finite and consistent.
            const auto start = call.arguments[1].AsInt();
            const auto end = call.arguments[2].AsInt();
            const auto rect = call.arguments[3].ref;
            if (!rect.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "getTextBounds bounds rect is null"};
            }
            const auto count = end > start ? end - start : 0;
            const auto slots = call.vm.Model().InstanceSlots(rect);
            slots[0] = {0, dx::SlotTag::cat1};
            slots[1] = {static_cast<std::uint32_t>(-16), dx::SlotTag::cat1};
            slots[2] = {static_cast<std::uint32_t>(count * 8),
                        dx::SlotTag::cat1};
            slots[3] = {0, dx::SlotTag::cat1};
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

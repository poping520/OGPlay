// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_text_Editable.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_text_Editable {

Decl Declare_android_text_Editable(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/text/Editable;");
    builder.FinalMethod("clear", "()V", EditableClearHandler(context));
    builder.FinalMethod("length", "()I", EditableLengthHandler(context));
    builder.FinalMethod("replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;", EditableReplaceHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_text_Editable(const Context& context) {
    return dvm80_android_text_Editable::Declare_android_text_Editable(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_text_EditableImpl.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_text_EditableImpl {

Decl Declare_android_text_EditableImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/text/EditableImpl;", "Ljava/lang/Object;", {"Landroid/text/Editable;"});
    builder.FinalMethod("clear", "()V", EditableClearHandler(context));
    builder.FinalMethod("length", "()I", EditableLengthHandler(context));
    builder.FinalMethod("replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;", EditableReplaceHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_text_EditableImpl(const Context& context) {
    return dvm80_android_text_EditableImpl::Declare_android_text_EditableImpl(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_text_TextPaint.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_text_TextPaint {

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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_text_TextPaint(const Context& context) {
    return dvm80_android_text_TextPaint::Declare_android_text_TextPaint(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_text_TextWatcher.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_text_TextWatcher {

Decl Declare_android_text_TextWatcher(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/text/TextWatcher;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_text_TextWatcher(const Context& context) {
    return dvm80_android_text_TextWatcher::Declare_android_text_TextWatcher(context);
}
}  // namespace ogplay::runtime::android_intrinsics

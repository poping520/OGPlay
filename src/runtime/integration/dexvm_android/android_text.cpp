// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_text_Editable.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_Editable(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/text/Editable;");
    builder.FinalMethod("clear", "()V", EditableClearHandler(context));
    builder.FinalMethod("length", "()I", EditableLengthHandler(context));
    builder.FinalMethod("replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;", EditableReplaceHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_text_EditableImpl.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_EditableImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/text/EditableImpl;", "Ljava/lang/Object;", {"Landroid/text/Editable;"});
    builder.FinalMethod("clear", "()V", EditableClearHandler(context));
    builder.FinalMethod("length", "()I", EditableLengthHandler(context));
    builder.FinalMethod("replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;", EditableReplaceHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_text_TextPaint.cpp ----
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


// ---- migrated from android_text_TextWatcher.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_TextWatcher(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/text/TextWatcher;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_TextUtils(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/text/TextUtils;", "Ljava/lang/Object;");
    const auto utf8 = [](dx::IntrinsicContext& call, const dx::VmObjectRef ref) {
        return ref.IsValid() ? call.vm.StringUtf8(ref) : std::string{};
    };
    builder.StaticMethod("isEmpty", "(Ljava/lang/CharSequence;)Z",
        [utf8](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(!call.arguments[0].ref.IsValid() ||
                                    utf8(call, call.arguments[0].ref).empty());
        });
    builder.StaticMethod("equals", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Z",
        [utf8](dx::IntrinsicContext& call) {
            const auto left = call.arguments[0].ref;
            const auto right = call.arguments[1].ref;
            return dx::VmValue::Int(left == right ||
                (left.IsValid() && right.IsValid() &&
                 utf8(call, left) == utf8(call, right)));
        });
    builder.StaticMethod("getTrimmedLength", "(Ljava/lang/CharSequence;)I",
        [utf8](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "text"};
            const auto value = utf8(call, call.arguments[0].ref);
            std::size_t begin{};
            std::size_t end = value.size();
            while (begin < end && static_cast<unsigned char>(value[begin]) <= ' ') ++begin;
            while (end > begin && static_cast<unsigned char>(value[end - 1U]) <= ' ') --end;
            return dx::VmValue::Int(static_cast<std::int32_t>(end - begin));
        });
    builder.StaticMethod("substring", "(Ljava/lang/CharSequence;II)Ljava/lang/String;",
        [utf8](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "text"};
            const auto value = utf8(call, call.arguments[0].ref);
            const auto start = call.arguments[1].AsInt();
            const auto end = call.arguments[2].AsInt();
            if (start < 0 || end < start || static_cast<std::size_t>(end) > value.size())
                throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "substring"};
            return MakeString(call, value.substr(static_cast<std::size_t>(start),
                                                  static_cast<std::size_t>(end - start)));
        });
    builder.StaticMethod("join", "(Ljava/lang/CharSequence;[Ljava/lang/Object;)Ljava/lang/String;",
        [utf8](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid() || !call.arguments[1].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "join"};
            const auto delimiter = utf8(call, call.arguments[0].ref);
            const auto array = call.arguments[1].ref;
            std::string result;
            const auto length = call.vm.Model().ArrayLength(array);
            for (JniSize index = 0; index < length; ++index) {
                if (index != 0) result += delimiter;
                const auto element = call.vm.Model().GetObjectElement(array, index);
                result += element.IsValid() ? utf8(call, element) : "null";
            }
            return MakeString(call, result);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

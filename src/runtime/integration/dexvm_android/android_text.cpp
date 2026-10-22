// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_text_Editable.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_Editable(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/text/Editable;");
    builder.UnimplementedVirtual("clear", "()V", dx::kAccPublic | dx::kAccAbstract);
    builder.FinalMethod("length", "()I", EditableLengthHandler(context));
    builder.UnimplementedVirtual("replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;",
                                 dx::kAccPublic | dx::kAccAbstract);
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
    builder.FinalOverrideMethod("toString", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            const auto owner = context->editable_owner.find(call.receiver.Value());
            if (owner == context->editable_owner.end()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "Editable has no owning EditText"};
            }
            const auto node = FindViewUiNode(*context, owner->second);
            if (!node.has_value()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "Editable owner is detached"};
            }
            return dx::VmValue::Ref(call.vm.Model().NewString(
                context->ui_tree.Get(*node)->text));
        });
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
    const auto wu_density = builder.BoundInstanceField("density", "F", dx::kAccPublic);
    const auto wu_underlineColor = builder.BoundInstanceField("underlineColor", "I", dx::kAccPublic);
    // AOSP 4.4 ROM line 45.
    builder.Constructor(
        "()V",
        [wu_density, wu_underlineColor](dx::IntrinsicContext& c) {
            const auto target = c.vm.Linker().FindDirectMethod(
                c.vm.Linker().ResolveDescriptor("Landroid/graphics/Paint;"), "<init>", "()V");
            const auto result = c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            dx::IntrinsicCall(c).SetFloat(wu_density, 1.0F);
            dx::IntrinsicCall(c).SetInt(wu_underlineColor, 0);
            return dx::VmValue::Void();
        },
        dx::kAccPublic);

    // AOSP 4.4 ROM line 49.
    builder.Constructor(
        "(I)V",
        [wu_density, wu_underlineColor](dx::IntrinsicContext& c) {
            const auto target = c.vm.Linker().FindDirectMethod(
                c.vm.Linker().ResolveDescriptor("Landroid/graphics/Paint;"), "<init>", "(I)V");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver), c.arguments[0]});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            dx::IntrinsicCall(c).SetFloat(wu_density, 1.0F);
            dx::IntrinsicCall(c).SetInt(wu_underlineColor, 0);
            return dx::VmValue::Void();
        },
        dx::kAccPublic);

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
    builder.StaticMethod(
        "getLayoutDirectionFromLocale", "(Ljava/util/Locale;)I",
        [](dx::IntrinsicContext& call) {
            const auto locale = call.arguments[0].ref;
            if (!locale.IsValid()) return dx::VmValue::Int(0);
            const auto language = CallAndroidMethod(
                call.vm, locale, "getLanguage", "()Ljava/lang/String;").ref;
            const auto code = call.vm.StringUtf8(language);
            if (code.empty() || code == "en" || code == "zh") {
                return dx::VmValue::Int(0);
            }
            if (auto* ledger = call.vm.Ledger(); ledger != nullptr) {
                ledger->RecordUnimplemented(
                    "dexvm.locale_layout_direction", 0);
            }
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "layout direction is unavailable for locale " + code};
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
    // AOSP 4.4 ROM line 308.
    builder.StaticMethod(
        "join", "(Ljava/lang/CharSequence;Ljava/lang/Iterable;)Ljava/lang/String;",
        [](dx::IntrinsicContext& c) {
            const auto builder = c.vm.NewIntrinsicInstance("Ljava/lang/StringBuilder;");
            const auto builder_root = c.vm.ProtectReferences(std::array{builder});
            const auto ctor = c.vm.Linker().FindDirectMethod(c.vm.Model().ObjectClass(builder), "<init>", "()V");
            const auto initialized = c.vm.Call(*ctor, std::array{dx::VmValue::Ref(builder)});
            if (initialized.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(initialized.exception_class).descriptor,
                                      initialized.exception_message, initialized.exception};
            const auto iterator = CallAndroidMethod(c.vm, c.arguments[1].ref, "iterator", "()Ljava/util/Iterator;").ref;
            const auto iterator_root = c.vm.ProtectReferences(std::array{iterator});
            bool first = true;
            while (CallAndroidMethod(c.vm, iterator, "hasNext", "()Z").AsInt() != 0) {
                const auto element = CallAndroidMethod(c.vm, iterator, "next", "()Ljava/lang/Object;").ref;
                const auto element_root = c.vm.ProtectReferences(std::array{element});
                if (!first)
                    static_cast<void>(CallAndroidMethod(c.vm, builder, "append",
                                                        "(Ljava/lang/CharSequence;)Ljava/lang/StringBuilder;",
                                                        {c.arguments[0]}));
                first = false;
                static_cast<void>(CallAndroidMethod(c.vm, builder, "append",
                                                    "(Ljava/lang/Object;)Ljava/lang/StringBuilder;",
                                                    {dx::VmValue::Ref(element)}));
            }
            return CallAndroidMethod(c.vm, builder, "toString", "()Ljava/lang/String;");
        },
        dx::kAccPublic);

    // AOSP 4.4 ROM line 279.
    builder.StaticMethod(
        "join", "(Ljava/lang/Iterable;)Ljava/lang/CharSequence;",
        [](dx::IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/content/res/Resources;"),
                                               "getSystem", "()Landroid/content/res/Resources;");
            const auto result = c.vm.Call(*target, {});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            const auto resources = result.value.ref;
            const auto root = c.vm.ProtectReferences(std::array{resources});
            const auto delimiter = CallAndroidMethod(c.vm, resources, "getText", "(I)Ljava/lang/CharSequence;",
                                                     {dx::VmValue::Int(0x1040646)})
                                       .ref;
            const auto delimiter_root = c.vm.ProtectReferences(std::array{delimiter});
            const auto join =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/text/TextUtils;"), "join",
                                               "(Ljava/lang/CharSequence;Ljava/lang/Iterable;)Ljava/lang/String;");
            const auto joined = c.vm.Call(*join, std::array{dx::VmValue::Ref(delimiter), c.arguments[0]});
            if (joined.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(joined.exception_class).descriptor, joined.exception_message,
                                      joined.exception};
            return joined.value;
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;
namespace {

using SystemPropertyMap = std::unordered_map<std::string, std::string>;

[[nodiscard]] std::string PropertyKey(IntrinsicContext& context,
                                      const VmObjectRef reference) {
    if (!reference.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "system property key is null"};
    }
    auto key = context.vm.StringUtf8(reference);
    if (key.empty()) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "system property key is empty"};
    }
    return key;
}

[[nodiscard]] std::string PropertyValue(IntrinsicContext& context,
                                        const VmObjectRef reference) {
    if (!reference.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "system property value is null"};
    }
    return context.vm.StringUtf8(reference);
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_System() {
    auto properties = std::make_shared<SystemPropertyMap>(SystemPropertyMap{
        {"file.separator", "/"},
        {"line.separator", "\n"},
        {"path.separator", ":"},
    });
    IntrinsicClassBuilder builder("Ljava/lang/System;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("out", "Ljava/io/PrintStream;", true);
    builder.Field("err", "Ljava/io/PrintStream;", true);
    builder.Static("arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V",
        [](IntrinsicContext &context) {
                auto& model = context.vm.Model();
                const auto source = context.arguments[0].ref;
                const auto source_pos = context.arguments[1].AsInt();
                const auto target = context.arguments[2].ref;
                const auto target_pos = context.arguments[3].AsInt();
                const auto length = context.arguments[4].AsInt();
                if (!source.IsValid() || !target.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "arraycopy on null array"};
                }
                if (length < 0 || source_pos < 0 || target_pos < 0 ||
                    source_pos > model.ArrayLength(source) - length ||
                    target_pos > model.ArrayLength(target) - length) {
              throw VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;",
                        "arraycopy range is out of bounds"};
                }
                const auto source_kind = model.Kind(source);
                const auto target_kind = model.Kind(target);
                if (source_kind == VmObjectKind::object_array &&
                    target_kind == VmObjectKind::object_array) {
                    // Overlap-safe copy: buffer the source range first.
                    std::vector<VmObjectRef> staged;
                    staged.reserve(static_cast<std::size_t>(length));
                    for (std::int32_t index = 0; index < length; ++index) {
                staged.push_back(model.GetObjectElement(source, source_pos + index));
                    }
                    for (std::int32_t index = 0; index < length; ++index) {
                        model.SetObjectElement(target, target_pos + index,
                                       staged[static_cast<std::size_t>(index)]);
                    }
                    return VmValue::Void();
                }
                if (source_kind == VmObjectKind::primitive_array &&
                    target_kind == VmObjectKind::primitive_array &&
                model.PrimitiveArrayKind(source) == model.PrimitiveArrayKind(target)) {
                    std::vector<std::uint64_t> staged;
                    staged.reserve(static_cast<std::size_t>(length));
                    for (std::int32_t index = 0; index < length; ++index) {
                staged.push_back(model.GetPrimitiveElement(source, source_pos + index));
                    }
                    for (std::int32_t index = 0; index < length; ++index) {
                        model.SetPrimitiveElement(target, target_pos + index,
                                          staged[static_cast<std::size_t>(index)]);
                    }
                    return VmValue::Void();
                }
                throw VmJavaThrow{"Ljava/lang/ArrayStoreException;",
                                  "arraycopy element types are incompatible"};
            });
    builder.Static("gc", "()V",
        [](IntrinsicContext&) {
                // GC-A never collects (04 §5); the call is legal and does nothing.
                return VmValue::Void();
            });
    builder.Static("getProperty", "(Ljava/lang/String;)Ljava/lang/String;",
        [properties](IntrinsicContext& context) {
                const auto key = PropertyKey(context, context.arguments[0].ref);
                const auto found = properties->find(key);
                if (found == properties->end()) {
                    return VmValue::Ref(VmObjectRef{});
                }
                return VmValue::Ref(context.vm.NewStringUtf8(found->second));
            });
    builder.Static("setProperty", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        [properties](IntrinsicContext& context) {
                const auto key = PropertyKey(context, context.arguments[0].ref);
                const auto value = PropertyValue(context, context.arguments[1].ref);
                    const auto previous = properties->find(key);
                    VmValue result = VmValue::Ref(VmObjectRef{});
                    if (previous != properties->end()) {
                  result = VmValue::Ref(context.vm.NewStringUtf8(previous->second));
                    }
                    (*properties)[key] = value;
                    return result;
                });
    builder.Static("currentTimeMillis", "()J",
        {});
    builder.Static("nanoTime", "()J",
        {});
    builder.Static("loadLibrary", "(Ljava/lang/String;)V",
        {});
    builder.Static("exit", "(I)V",
        {});
    builder.Clinit(
        [](IntrinsicContext& context) {
                auto& vm = context.vm;
            vm.SetIntrinsicStaticRef("Ljava/lang/System;", "out",
                                     "Ljava/io/PrintStream;",
                    vm.NewIntrinsicInstance("Ljava/io/PrintStream;"));
            vm.SetIntrinsicStaticRef("Ljava/lang/System;", "err",
                                     "Ljava/io/PrintStream;",
                    vm.NewIntrinsicInstance("Ljava/io/PrintStream;"));
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

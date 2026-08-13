#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_System() {
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
    builder.Static("setProperty", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        [properties =
                   std::make_shared<std::unordered_map<std::string, std::string>>()](
                    IntrinsicContext& context) {
                const auto key = context.vm.StringUtf8(context.arguments[0].ref);
                const auto value = context.vm.StringUtf8(context.arguments[1].ref);
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

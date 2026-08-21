#include "catalog.h"
#include "shared.h"

#include <bit>
#include <cstdint>
#include <string>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/reflection_codec.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {
using namespace detail;

[[nodiscard]] bool IsArrayKind(const VmObjectKind kind) {
    return kind == VmObjectKind::primitive_array ||
           kind == VmObjectKind::object_array;
}

[[nodiscard]] VmObjectKind RequireArray(JavaObjectModel& model,
                                        const VmObjectRef array) {
    if (!array.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "array == null"};
    }
    const auto kind = model.Kind(array);
    if (!IsArrayKind(kind)) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "object is not an array"};
    }
    return kind;
}

void CheckIndex(JavaObjectModel& model, const VmObjectRef array,
                const std::int32_t index) {
    const auto length = model.ArrayLength(array);
    if (index < 0 || index >= length) {
        throw VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;",
                          std::to_string(index)};
    }
}

[[nodiscard]] std::string PrimitiveDescriptor(const JniPrimitiveKind kind) {
    switch (kind) {
        case JniPrimitiveKind::boolean: return "Z";
        case JniPrimitiveKind::byte: return "B";
        case JniPrimitiveKind::character: return "C";
        case JniPrimitiveKind::short_integer: return "S";
        case JniPrimitiveKind::integer: return "I";
        case JniPrimitiveKind::long_integer: return "J";
        case JniPrimitiveKind::float_value: return "F";
        case JniPrimitiveKind::double_value: return "D";
    }
    throw DexVmError(DexVmErrorReason::internal_invariant,
                     "unknown primitive array kind");
}

[[nodiscard]] VmValue PrimitiveValue(JavaObjectModel& model,
                                     const VmObjectRef array,
                                     const std::int32_t index) {
    const auto kind = model.PrimitiveArrayKind(array);
    const auto bits = model.GetPrimitiveElement(array, index);
    switch (kind) {
        case JniPrimitiveKind::boolean:
            return VmValue::Int(static_cast<std::int32_t>(bits & 1U));
        case JniPrimitiveKind::byte:
            return VmValue::Int(static_cast<std::int8_t>(bits));
        case JniPrimitiveKind::character:
            return VmValue::Int(static_cast<std::uint16_t>(bits));
        case JniPrimitiveKind::short_integer:
            return VmValue::Int(static_cast<std::int16_t>(bits));
        case JniPrimitiveKind::integer:
            return VmValue::Int(static_cast<std::int32_t>(bits));
        case JniPrimitiveKind::long_integer:
            return VmValue::Long(static_cast<std::int64_t>(bits));
        case JniPrimitiveKind::float_value:
            return VmValue::Float(
                std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
        case JniPrimitiveKind::double_value:
            return VmValue::Double(std::bit_cast<double>(bits));
    }
    throw DexVmError(DexVmErrorReason::internal_invariant,
                     "unknown primitive array kind");
}

[[nodiscard]] std::uint64_t PrimitiveBits(const VmValue& value,
                                          const std::string_view descriptor) {
    if (descriptor == "J" || descriptor == "D") return value.wide;
    return value.cat1;
}

[[nodiscard]] VmObjectRef NewArray(IntrinsicContext& context,
                                   const VmObjectRef class_object,
                                   const std::vector<std::int32_t>& dims) {
    if (!class_object.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "componentType == null"};
    }
    const auto element_class =
        context.vm.Model().ClassOfClassObject(class_object);
    const auto element_descriptor =
        context.vm.Linker().Class(element_class).descriptor;
    if (element_descriptor == "V") {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "cannot allocate an array of void"};
    }
    for (const auto length : dims) {
        if (length < 0) {
            throw VmJavaThrow{"Ljava/lang/NegativeArraySizeException;",
                              std::to_string(length)};
        }
    }
    return BuildReflectArray(context.vm, element_descriptor, dims);
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_reflect_Array() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/reflect/Array;", "Ljava/lang/Object;", {}, 0x0011U);
    builder.StaticMethod(
        "newInstance", "(Ljava/lang/Class;I)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(NewArray(
                context, context.arguments[0].ref,
                {context.arguments[1].AsInt()}));
        });
    builder.StaticMethod(
        "newInstance", "(Ljava/lang/Class;[I)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            auto& model = context.vm.Model();
            const auto dims_ref = context.arguments[1].ref;
            if (!dims_ref.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "dimensions == null"};
            }
            if (model.Kind(dims_ref) != VmObjectKind::primitive_array ||
                model.PrimitiveArrayKind(dims_ref) !=
                    JniPrimitiveKind::integer) {
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "dimensions is not int[]"};
            }
            const auto count = model.ArrayLength(dims_ref);
            if (count <= 0 || count > 255) {
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "invalid dimension count"};
            }
            std::vector<std::int32_t> dims;
            dims.reserve(static_cast<std::size_t>(count));
            for (JniSize index = 0; index < count; ++index) {
                dims.push_back(static_cast<std::int32_t>(
                    model.GetPrimitiveElement(dims_ref, index)));
            }
            return VmValue::Ref(
                NewArray(context, context.arguments[0].ref, dims));
        });
    builder.StaticMethod("getLength", "(Ljava/lang/Object;)I",
        [](IntrinsicContext& context) {
            auto& model = context.vm.Model();
            static_cast<void>(RequireArray(model, context.arguments[0].ref));
            return VmValue::Int(model.ArrayLength(context.arguments[0].ref));
        });
    builder.StaticMethod(
        "get", "(Ljava/lang/Object;I)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            auto& model = context.vm.Model();
            const auto array = context.arguments[0].ref;
            const auto index = context.arguments[1].AsInt();
            const auto kind = RequireArray(model, array);
            CheckIndex(model, array, index);
            if (kind == VmObjectKind::object_array) {
                return VmValue::Ref(model.GetObjectElement(array, index));
            }
            const auto type = context.vm.Linker().ResolveDescriptor(
                PrimitiveDescriptor(model.PrimitiveArrayKind(array)));
            return VmValue::Ref(context.vm.Reflection().Codec().BoxReturn(
                type, PrimitiveValue(model, array, index)));
        });
    builder.StaticMethod(
        "set", "(Ljava/lang/Object;ILjava/lang/Object;)V",
        [](IntrinsicContext& context) {
            auto& model = context.vm.Model();
            const auto array = context.arguments[0].ref;
            const auto index = context.arguments[1].AsInt();
            const auto kind = RequireArray(model, array);
            CheckIndex(model, array, index);
            if (kind == VmObjectKind::object_array) {
                const auto converted = context.vm.Reflection().Codec()
                    .ConvertArgument(context.arguments[2].ref,
                                     model.ObjectArrayElementClass(array));
                model.SetObjectElement(array, index, converted.ref);
                return VmValue::Void();
            }
            const auto descriptor =
                PrimitiveDescriptor(model.PrimitiveArrayKind(array));
            const auto type = context.vm.Linker().ResolveDescriptor(descriptor);
            const auto converted = context.vm.Reflection().Codec()
                .ConvertArgument(context.arguments[2].ref, type);
            model.SetPrimitiveElement(
                array, index, PrimitiveBits(converted, descriptor));
            return VmValue::Void();
        });

    struct PrimitiveMethod final {
        const char* suffix;
        const char* descriptor;
    };
    constexpr PrimitiveMethod primitives[]{
        {"Boolean", "Z"}, {"Byte", "B"}, {"Char", "C"},
        {"Short", "S"},   {"Int", "I"},  {"Long", "J"},
        {"Float", "F"},   {"Double", "D"},
    };
    for (const auto& primitive : primitives) {
        const std::string requested(primitive.descriptor);
        builder.StaticMethod(
            std::string("get") + primitive.suffix,
            std::string("(Ljava/lang/Object;I)") + requested,
            [requested](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto array = context.arguments[0].ref;
                const auto index = context.arguments[1].AsInt();
                if (RequireArray(model, array) !=
                    VmObjectKind::primitive_array) {
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "array has incompatible type"};
                }
                CheckIndex(model, array, index);
                const auto source_descriptor =
                    PrimitiveDescriptor(model.PrimitiveArrayKind(array));
                const auto source = context.vm.Linker().ResolveDescriptor(
                    source_descriptor);
                const auto target =
                    context.vm.Linker().ResolveDescriptor(requested);
                const auto boxed = context.vm.Reflection().Codec().BoxReturn(
                    source, PrimitiveValue(model, array, index));
                return context.vm.Reflection().Codec().ConvertArgument(
                    boxed, target);
            });
        builder.StaticMethod(
            std::string("set") + primitive.suffix,
            std::string("(Ljava/lang/Object;I") + requested + ")V",
            [requested](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto array = context.arguments[0].ref;
                const auto index = context.arguments[1].AsInt();
                if (RequireArray(model, array) !=
                    VmObjectKind::primitive_array) {
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "array has incompatible type"};
                }
                CheckIndex(model, array, index);
                const auto target_descriptor =
                    PrimitiveDescriptor(model.PrimitiveArrayKind(array));
                const auto source =
                    context.vm.Linker().ResolveDescriptor(requested);
                const auto target = context.vm.Linker().ResolveDescriptor(
                    target_descriptor);
                const auto boxed = context.vm.Reflection().Codec().BoxReturn(
                    source, context.arguments[2]);
                const auto converted = context.vm.Reflection().Codec()
                    .ConvertArgument(boxed, target);
                model.SetPrimitiveElement(
                    array, index,
                    PrimitiveBits(converted, target_descriptor));
                return VmValue::Void();
            });
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics

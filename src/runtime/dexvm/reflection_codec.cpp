#include "ogplay/runtime/dexvm/reflection_codec.h"

#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

namespace ogplay::runtime::dexvm {
namespace {

enum class PrimitiveKind : std::uint8_t {
    none,
    boolean_value,
    byte_value,
    char_value,
    short_value,
    int_value,
    long_value,
    float_value,
    double_value,
    void_value,
};

struct PrimitiveValue final {
    PrimitiveKind kind{PrimitiveKind::none};
    std::uint64_t bits{};
};

[[nodiscard]] PrimitiveKind PrimitiveForDescriptor(
    const std::string_view descriptor) {
    if (descriptor.size() != 1U) return PrimitiveKind::none;
    switch (descriptor.front()) {
        case 'Z': return PrimitiveKind::boolean_value;
        case 'B': return PrimitiveKind::byte_value;
        case 'C': return PrimitiveKind::char_value;
        case 'S': return PrimitiveKind::short_value;
        case 'I': return PrimitiveKind::int_value;
        case 'J': return PrimitiveKind::long_value;
        case 'F': return PrimitiveKind::float_value;
        case 'D': return PrimitiveKind::double_value;
        case 'V': return PrimitiveKind::void_value;
        default: return PrimitiveKind::none;
    }
}

[[nodiscard]] PrimitiveKind PrimitiveForWrapper(
    const std::string_view descriptor) {
    if (descriptor == "Ljava/lang/Boolean;") return PrimitiveKind::boolean_value;
    if (descriptor == "Ljava/lang/Byte;") return PrimitiveKind::byte_value;
    if (descriptor == "Ljava/lang/Character;") return PrimitiveKind::char_value;
    if (descriptor == "Ljava/lang/Short;") return PrimitiveKind::short_value;
    if (descriptor == "Ljava/lang/Integer;") return PrimitiveKind::int_value;
    if (descriptor == "Ljava/lang/Long;") return PrimitiveKind::long_value;
    if (descriptor == "Ljava/lang/Float;") return PrimitiveKind::float_value;
    if (descriptor == "Ljava/lang/Double;") return PrimitiveKind::double_value;
    return PrimitiveKind::none;
}

[[nodiscard]] std::string_view WrapperForPrimitive(const PrimitiveKind kind) {
    switch (kind) {
        case PrimitiveKind::boolean_value: return "Ljava/lang/Boolean;";
        case PrimitiveKind::byte_value: return "Ljava/lang/Byte;";
        case PrimitiveKind::char_value: return "Ljava/lang/Character;";
        case PrimitiveKind::short_value: return "Ljava/lang/Short;";
        case PrimitiveKind::int_value: return "Ljava/lang/Integer;";
        case PrimitiveKind::long_value: return "Ljava/lang/Long;";
        case PrimitiveKind::float_value: return "Ljava/lang/Float;";
        case PrimitiveKind::double_value: return "Ljava/lang/Double;";
        default: return {};
    }
}

[[nodiscard]] bool CanWiden(const PrimitiveKind source,
                            const PrimitiveKind target) {
    if (source == target) return source != PrimitiveKind::none &&
                                  source != PrimitiveKind::void_value;
    switch (source) {
        case PrimitiveKind::byte_value:
            return target == PrimitiveKind::short_value ||
                   target == PrimitiveKind::int_value ||
                   target == PrimitiveKind::long_value ||
                   target == PrimitiveKind::float_value ||
                   target == PrimitiveKind::double_value;
        case PrimitiveKind::short_value:
        case PrimitiveKind::char_value:
            return target == PrimitiveKind::int_value ||
                   target == PrimitiveKind::long_value ||
                   target == PrimitiveKind::float_value ||
                   target == PrimitiveKind::double_value;
        case PrimitiveKind::int_value:
            return target == PrimitiveKind::long_value ||
                   target == PrimitiveKind::float_value ||
                   target == PrimitiveKind::double_value;
        case PrimitiveKind::long_value:
            return target == PrimitiveKind::float_value ||
                   target == PrimitiveKind::double_value;
        case PrimitiveKind::float_value:
            return target == PrimitiveKind::double_value;
        default: return false;
    }
}

[[nodiscard]] PrimitiveValue ReadPrimitive(JavaObjectModel& model,
                                           DexClassLinker& linker,
                                           const VmObjectRef object) {
    if (!object.IsValid()) return {};
    const auto source_class = model.ObjectClass(object);
    const auto kind = PrimitiveForWrapper(linker.Class(source_class).descriptor);
    if (kind == PrimitiveKind::none) return {};
    const auto slots = model.InstanceSlots(object);
    const bool wide = kind == PrimitiveKind::long_value ||
                      kind == PrimitiveKind::double_value;
    if (slots.size() < (wide ? 2U : 1U)) return {};
    std::uint64_t bits = slots[0].bits;
    if (wide) bits |= static_cast<std::uint64_t>(slots[1].bits) << 32U;
    return {kind, bits};
}

template <typename Source>
[[nodiscard]] VmValue ConvertNumeric(const Source numeric,
                                     const PrimitiveKind target) {
    switch (target) {
        case PrimitiveKind::byte_value:
            return VmValue::Int(static_cast<std::int8_t>(numeric));
        case PrimitiveKind::char_value:
            return VmValue::Int(static_cast<std::uint16_t>(numeric));
        case PrimitiveKind::short_value:
            return VmValue::Int(static_cast<std::int16_t>(numeric));
        case PrimitiveKind::int_value:
            return VmValue::Int(static_cast<std::int32_t>(numeric));
        case PrimitiveKind::long_value:
            return VmValue::Long(static_cast<std::int64_t>(numeric));
        case PrimitiveKind::float_value:
            return VmValue::Float(static_cast<float>(numeric));
        case PrimitiveKind::double_value:
            return VmValue::Double(static_cast<double>(numeric));
        default: break;
    }
    throw DexVmError(DexVmErrorReason::internal_invariant,
                     "invalid reflection primitive conversion");
}

[[nodiscard]] VmValue ConvertPrimitive(const PrimitiveValue source,
                                       const PrimitiveKind target) {
    if (target == PrimitiveKind::boolean_value) {
        return VmValue::Int(source.bits != 0U ? 1 : 0);
    }
    switch (source.kind) {
        case PrimitiveKind::byte_value:
            return ConvertNumeric(static_cast<std::int8_t>(source.bits),
                                  target);
        case PrimitiveKind::char_value:
            return ConvertNumeric(static_cast<std::uint16_t>(source.bits),
                                  target);
        case PrimitiveKind::short_value:
            return ConvertNumeric(static_cast<std::int16_t>(source.bits),
                                  target);
        case PrimitiveKind::int_value:
            return ConvertNumeric(static_cast<std::int32_t>(source.bits),
                                  target);
        case PrimitiveKind::long_value:
            return ConvertNumeric(static_cast<std::int64_t>(source.bits),
                                  target);
        case PrimitiveKind::float_value:
            return ConvertNumeric(
                std::bit_cast<float>(static_cast<std::uint32_t>(source.bits)),
                target);
        case PrimitiveKind::double_value:
            return ConvertNumeric(std::bit_cast<double>(source.bits), target);
        default: break;
    }
    throw DexVmError(DexVmErrorReason::internal_invariant,
                     "invalid reflection primitive source");
}

[[noreturn]] void ThrowMismatch(const std::size_t index) {
    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                      "argument " + std::to_string(index + 1U) +
                          " has incompatible type"};
}

}  // namespace

ReflectionCodec::ReflectionCodec(Interpreter& interpreter,
                                 DexClassLinker& linker,
                                 JavaObjectModel& model)
    : interpreter_(&interpreter), linker_(&linker), model_(&model) {}

VmValue ReflectionCodec::ConvertArgument(const VmObjectRef argument,
                                         const DexClassId target_type) {
    const auto& target = linker_->Class(target_type);
    const auto target_primitive = PrimitiveForDescriptor(target.descriptor);
    if (target_primitive == PrimitiveKind::none) {
        if (!argument.IsValid()) return VmValue::Ref(VmObjectRef{});
        if (!linker_->IsAssignable(target_type, model_->ObjectClass(argument))) {
            ThrowMismatch(0U);
        }
        return VmValue::Ref(argument);
    }
    if (target_primitive == PrimitiveKind::void_value) ThrowMismatch(0U);
    const auto source = ReadPrimitive(*model_, *linker_, argument);
    if (!CanWiden(source.kind, target_primitive)) ThrowMismatch(0U);
    return ConvertPrimitive(source, target_primitive);
}

std::vector<VmValue> ReflectionCodec::ConvertArguments(
    const VmObjectRef arguments,
    const std::span<const DexClassId> parameter_types) {
    const auto count = arguments.IsValid()
        ? static_cast<std::size_t>(model_->ArrayLength(arguments))
        : 0U;
    if (count != parameter_types.size()) {
        throw VmJavaThrow{
            "Ljava/lang/IllegalArgumentException;",
            "wrong number of arguments; expected " +
                std::to_string(parameter_types.size()) + ", got " +
                std::to_string(count)};
    }
    std::vector<VmValue> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        try {
            result.push_back(ConvertArgument(
                model_->GetObjectElement(arguments,
                                         static_cast<JniSize>(index)),
                parameter_types[index]));
        } catch (const VmJavaThrow&) {
            ThrowMismatch(index);
        }
    }
    return result;
}

VmObjectRef ReflectionCodec::BoxReturn(const DexClassId return_type,
                                       const VmValue& value) {
    const auto primitive = PrimitiveForDescriptor(
        linker_->Class(return_type).descriptor);
    if (primitive == PrimitiveKind::none) return value.ref;
    if (primitive == PrimitiveKind::void_value) return VmObjectRef{};
    const auto wrapper = interpreter_->NewIntrinsicInstance(
        WrapperForPrimitive(primitive));
    auto slots = model_->InstanceSlots(wrapper);
    const bool wide = primitive == PrimitiveKind::long_value ||
                      primitive == PrimitiveKind::double_value;
    const auto bits = wide ? value.wide
                           : static_cast<std::uint64_t>(value.cat1);
    if (slots.size() < (wide ? 2U : 1U)) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "primitive wrapper has invalid value layout");
    }
    slots[0] = {static_cast<std::uint32_t>(bits), SlotTag::cat1};
    if (wide) {
        slots[1] = {static_cast<std::uint32_t>(bits >> 32U), SlotTag::cat1};
    }
    return wrapper;
}

}  // namespace ogplay::runtime::dexvm

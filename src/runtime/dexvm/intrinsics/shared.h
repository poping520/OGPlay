#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../interpreter_internal.h"
#include "ogplay/core/text.h"
#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::detail {

[[nodiscard]] VmObjectRef OpenClasspathResource(IntrinsicContext&, const CoreIntrinsicServices&,
                                                const CoreIntrinsicServices::ClasspathResource&);

inline VmValue InvokeGuest(Interpreter& vm, VmObjectRef receiver, const std::string& name, const std::string& signature,
                           std::vector<VmValue> args = {}) {
    if (!receiver.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "receiver == null"};
    const auto type = vm.Model().ObjectClass(receiver);
    const auto slot = vm.Linker().FindVtableIndex(type, name, signature);
    if (!slot)
        throw DexVmError(DexVmErrorReason::unresolved_reference, "guest method is unavailable: " + name + signature);
    args.insert(args.begin(), VmValue::Ref(receiver));
    const auto result = vm.Call(vm.Linker().Class(type).vtable[*slot], args);
    if (result.exception.IsValid())
        throw VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                          result.exception};
    return result.value;
}

inline VmObjectRef NewStringArray(Interpreter& vm, const std::vector<std::string>& strings) {
    const auto element = vm.Linker().ResolveDescriptor("Ljava/lang/String;");
    const auto array = vm.Model().NewObjectArray(vm.Linker().ResolveDescriptor("[Ljava/lang/String;"), element,
                                                 static_cast<JniSize>(strings.size()));
    const auto roots = vm.ProtectReferences(std::array{array});
    for (std::size_t i = 0; i < strings.size(); ++i)
        vm.Model().SetObjectElement(array, static_cast<JniSize>(i), vm.NewStringUtf8(strings[i]));
    return array;
}

[[nodiscard]] inline std::string DottedName(const std::string& descriptor) {
    if (descriptor.starts_with("L") && descriptor.ends_with(";")) {
        std::string name = descriptor.substr(1, descriptor.size() - 2);
        for (auto& character : name) {
            if (character == '/') character = '.';
        }
        return name;
    }
    // Class.getName answers the Java keyword for primitive classes
    // (Float.TYPE.getName() == "float"); array descriptors stay as-is.
    switch (descriptor.size() == 1 ? descriptor[0] : '\0') {
    case 'Z': return "boolean";
    case 'B': return "byte";
    case 'C': return "char";
    case 'S': return "short";
    case 'I': return "int";
    case 'J': return "long";
    case 'F': return "float";
    case 'D': return "double";
    case 'V': return "void";
    default: break;
    }
    return descriptor;
}

[[nodiscard]] inline bool IsPrimitiveDescriptor(const std::string& descriptor) {
    return descriptor.size() == 1 && std::string_view("ZBSCIJFD").find(descriptor[0]) != std::string_view::npos;
}

[[nodiscard]] inline JniPrimitiveKind PrimitiveKindFor(const std::string& element) {
    switch (element[0]) {
    case 'Z': return JniPrimitiveKind::boolean;
    case 'B': return JniPrimitiveKind::byte;
    case 'C': return JniPrimitiveKind::character;
    case 'S': return JniPrimitiveKind::short_integer;
    case 'I': return JniPrimitiveKind::integer;
    case 'J': return JniPrimitiveKind::long_integer;
    case 'F': return JniPrimitiveKind::float_value;
    default: return JniPrimitiveKind::double_value;
    }
}

// Recursive builder for Array.newInstance: each level allocates the real
// typed array so subsequent check-cast and aget/aput see honest classes.
[[nodiscard]] inline VmObjectRef BuildReflectArray(Interpreter& vm, const std::string& element_descriptor,
                                                   const std::span<const std::int32_t> dims) {
    std::string descriptor(dims.size(), '[');
    descriptor += element_descriptor;
    const auto array_class = vm.Linker().ResolveDescriptor(descriptor);
    if (dims.size() == 1 && IsPrimitiveDescriptor(element_descriptor)) {
        return vm.Model().NewPrimitiveArray(array_class, PrimitiveKindFor(element_descriptor), dims[0]);
    }
    const auto element_class = vm.Linker().ResolveDescriptor(descriptor.substr(1));
    const auto array = vm.Model().NewObjectArray(array_class, element_class, dims[0]);
    if (dims.size() > 1) {
        for (std::int32_t index = 0; index < dims[0]; ++index) {
            vm.Model().SetObjectElement(array, index, BuildReflectArray(vm, element_descriptor, dims.subspan(1)));
        }
    }
    return array;
}

[[nodiscard]] inline std::int32_t JavaUtf8Hash(IntrinsicContext& context, const std::string_view value) {
    return context.vm.Model().StringHashCode(context.vm.NewStringUtf8(value));
}

[[nodiscard]] inline std::string ModifierString(const std::uint32_t modifiers) {
    struct Entry final {
        std::uint32_t flag;
        const char* text;
    };

    constexpr Entry entries[]{
        {kAccPublic, "public"},       {kAccProtected, "protected"}, {kAccPrivate, "private"},
        {kAccAbstract, "abstract"},   {kAccStatic, "static"},       {kAccFinal, "final"},
        {kAccTransient, "transient"}, {kAccVolatile, "volatile"},   {kAccSynchronized, "synchronized"},
        {kAccNative, "native"},       {kAccStrict, "strictfp"},     {kAccInterface, "interface"},
    };
    std::string result;
    for (const auto& entry : entries) {
        if ((modifiers & entry.flag) == 0U) continue;
        if (!result.empty()) result.push_back(' ');
        result += entry.text;
    }
    return result;
}

[[nodiscard]] inline std::string PrintableTypeName(IntrinsicContext& context, const DexClassId java_class) {
    std::string_view descriptor = context.vm.Linker().Class(java_class).descriptor;
    std::size_t dimensions{};
    while (descriptor.starts_with("[")) {
        descriptor.remove_prefix(1);
        ++dimensions;
    }
    auto result = ClassNameCodec::ClassGetName(descriptor);
    for (std::size_t index = 0; index < dimensions; ++index)
        result += "[]";
    return result;
}

[[nodiscard]] inline std::string PrintableTypeList(IntrinsicContext& context, const std::span<const DexClassId> types) {
    std::string result;
    for (std::size_t index = 0; index < types.size(); ++index) {
        if (index != 0U) result.push_back(',');
        result += PrintableTypeName(context, types[index]);
    }
    return result;
}

[[nodiscard]] inline std::string Narrow(const std::u16string& value) {
    std::string out;
    out.reserve(value.size());
    for (const auto unit : value) {
        out.push_back(unit < 0x80 ? static_cast<char>(unit) : '?');
    }
    return out;
}

// Boxed-value slot access: boxed intrinsics declare a single "value" field
// at slot 0 (wide values occupy slots 0..1).
[[nodiscard]] inline std::uint64_t BoxedBits(IntrinsicContext& context, const bool wide) {
    const auto slots = context.vm.Model().InstanceSlots(context.receiver);
    if (slots.size() < (wide ? 2U : 1U)) {
        throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "boxed value slot is missing"};
    }
    auto bits = static_cast<std::uint64_t>(slots[0].bits);
    if (wide) bits |= static_cast<std::uint64_t>(slots[1].bits) << 32U;
    return bits;
}

inline void SetBoxedBits(IntrinsicContext& context, const VmObjectRef receiver, const std::uint64_t bits,
                         const bool wide) {
    const auto slots = context.vm.Model().InstanceSlots(receiver);
    slots[0] = {static_cast<std::uint32_t>(bits), wide ? SlotTag::wide_lo : SlotTag::cat1};
    if (wide) {
        slots[1] = {static_cast<std::uint32_t>(bits >> 32U), SlotTag::wide_hi};
    }
}

[[nodiscard]] inline VmValue MakeBoxed(IntrinsicContext& context, const char* descriptor, const std::uint64_t bits,
                                       const bool wide) {
    const auto instance = context.vm.NewIntrinsicInstance(descriptor);
    SetBoxedBits(context, instance, bits, wide);
    return VmValue::Ref(instance);
}

[[nodiscard]] inline std::u16string Utf8DecodeReplace(const std::span<const std::byte> bytes) {
    std::u16string units;
    units.reserve(bytes.size());
    std::size_t index = 0;
    const auto continuation = [&](const std::size_t offset) {
        return offset < bytes.size() && (static_cast<std::uint8_t>(bytes[offset]) & 0xc0U) == 0x80U;
    };
    while (index < bytes.size()) {
        const auto lead = static_cast<std::uint8_t>(bytes[index]);
        if (lead < 0x80U) {
            units.push_back(lead);
            index += 1;
        } else if ((lead & 0xe0U) == 0xc0U && continuation(index + 1)) {
            units.push_back(
                static_cast<char16_t>(((lead & 0x1fU) << 6U) | (static_cast<std::uint8_t>(bytes[index + 1]) & 0x3fU)));
            index += 2;
        } else if ((lead & 0xf0U) == 0xe0U && continuation(index + 1) && continuation(index + 2)) {
            units.push_back(static_cast<char16_t>(((lead & 0x0fU) << 12U) |
                                                  ((static_cast<std::uint8_t>(bytes[index + 1]) & 0x3fU) << 6U) |
                                                  (static_cast<std::uint8_t>(bytes[index + 2]) & 0x3fU)));
            index += 3;
        } else if ((lead & 0xf8U) == 0xf0U && continuation(index + 1) && continuation(index + 2) &&
                   continuation(index + 3)) {
            const auto code_point = ((lead & 0x07U) << 18U) |
                                    ((static_cast<std::uint8_t>(bytes[index + 1]) & 0x3fU) << 12U) |
                                    ((static_cast<std::uint8_t>(bytes[index + 2]) & 0x3fU) << 6U) |
                                    (static_cast<std::uint8_t>(bytes[index + 3]) & 0x3fU);
            if (code_point >= 0x10000U && code_point <= 0x10ffffU) {
                const auto offset = code_point - 0x10000U;
                units.push_back(static_cast<char16_t>(0xd800U | (offset >> 10U)));
                units.push_back(static_cast<char16_t>(0xdc00U | (offset & 0x3ffU)));
            } else {
                units.push_back(u'\ufffd');
            }
            index += 4;
        } else {
            units.push_back(u'\ufffd');
            index += 1;
        }
    }
    return units;
}

[[nodiscard]] inline std::vector<std::byte> Utf8Encode(std::u16string_view value) {
    const auto encoded = *core::Utf16ToUtf8(std::span{value}, core::InvalidUtf16Policy::replace, '?');
    std::vector<std::byte> out(encoded.size());
    std::transform(encoded.begin(), encoded.end(), out.begin(),
                   [](const char byte) { return static_cast<std::byte>(byte); });
    return out;
}

// Shared range check for String(byte[]/char[], offset, count) forms.
inline void CheckRegion(const JniSize array_length, const std::int32_t offset, const std::int32_t count) {
    if (offset < 0 || count < 0 || offset > static_cast<std::int64_t>(array_length) - count) {
        throw VmJavaThrow{"Ljava/lang/StringIndexOutOfBoundsException;",
                          "offset " + std::to_string(offset) + ", count " + std::to_string(count) + ", length " +
                              std::to_string(array_length)};
    }
}

}  // namespace ogplay::runtime::dexvm::intrinsics::detail

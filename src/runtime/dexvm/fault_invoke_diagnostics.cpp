#include "interpreter_internal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/core/encoding.h"
#include "ogplay/core/text.h"

namespace ogplay::runtime::dexvm {
namespace {

constexpr std::size_t kMaximumFaultArguments = 16U;
constexpr JniSize kMaximumFaultStringCodeUnits = 96;
constexpr std::size_t kArgumentTypeColumnWidth = 28U;
constexpr std::string_view kFaultInvokeHeading = "\nDexVM fault invoke:";
thread_local bool g_rendering_fault_to_string = false;

class FaultToStringScope final {
public:
    FaultToStringScope() noexcept { g_rendering_fault_to_string = true; }
    ~FaultToStringScope() { g_rendering_fault_to_string = false; }
    FaultToStringScope(const FaultToStringScope&) = delete;
    FaultToStringScope& operator=(const FaultToStringScope&) = delete;
};

[[nodiscard]] std::string HexWord(const std::uint32_t value) {
    std::string result{"0x00000000"};
    for (std::size_t index = 0; index < 8U; ++index) {
        const auto shift = static_cast<unsigned>((7U - index) * 4U);
        result[index + 2U] =
            core::HexDigit(static_cast<std::uint8_t>(value >> shift),
                           core::HexCase::lower);
    }
    return result;
}

[[nodiscard]] std::string EscapeText(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const auto byte : value) {
        switch (byte) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\r': escaped += "\\r"; break;
            case '\n': escaped += "\\n"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (static_cast<unsigned char>(byte) < 0x20U) {
                    escaped += "\\u00";
                    escaped.push_back(core::HexDigit(
                        static_cast<std::uint8_t>(byte) >> 4U,
                        core::HexCase::lower));
                    escaped.push_back(core::HexDigit(
                        static_cast<std::uint8_t>(byte),
                        core::HexCase::lower));
                } else {
                    escaped.push_back(byte);
                }
                break;
        }
    }
    return escaped;
}

[[nodiscard]] std::string DexStringUtf8(const loader::DexString& value) {
    const auto encoded = core::Utf16ToUtf8(
        std::span{value.value}, core::InvalidUtf16Policy::replace, '?');
    return encoded.value_or("<invalid-utf16>");
}

template <typename Value>
[[nodiscard]] std::string FormatFloating(const Value value) {
    std::array<char, 64> buffer{};
    const auto [end, error] = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value,
        std::chars_format::general, std::numeric_limits<Value>::max_digits10);
    if (error != std::errc{}) return "<unavailable: floating format>";
    return std::string(buffer.data(), end);
}

[[nodiscard]] std::string FormatCat1Bits(const std::uint32_t bits,
                                         const char descriptor) {
    switch (descriptor) {
        case 'Z': return bits == 0U ? "false" : "true";
        case 'B':
            return std::to_string(
                static_cast<std::int32_t>(static_cast<std::int8_t>(bits)));
        case 'S':
            return std::to_string(
                static_cast<std::int32_t>(static_cast<std::int16_t>(bits)));
        case 'C': return std::to_string(static_cast<std::uint16_t>(bits));
        case 'F': return FormatFloating(std::bit_cast<float>(bits));
        default: return std::to_string(static_cast<std::int32_t>(bits));
    }
}

[[nodiscard]] std::string FormatWideBits(const std::uint64_t bits,
                                         const char descriptor) {
    if (descriptor == 'D') return FormatFloating(std::bit_cast<double>(bits));
    return std::to_string(static_cast<std::int64_t>(bits));
}

[[nodiscard]] std::string FormatString(const VmObjectRef ref,
                                       const JavaObjectModel& model,
                                       const JniSize limit) {
    const auto length = model.StringLength(ref);
    if (length < 0) return "<unavailable: invalid string length>";
    const auto shown = std::min(length, limit);
    const auto prefix = model.StringRegion(ref, 0, shown);
    const auto encoded = core::Utf16ToUtf8(
        std::span{prefix}, core::InvalidUtf16Policy::replace, '?');
    std::string result =
        "\"" + EscapeText(encoded.value_or("<invalid-utf16>")) + "\"";
    if (shown != length) {
        result += "... (length=" + std::to_string(length) + ")";
    }
    return result;
}

[[nodiscard]] std::string FormatObjectToString(
    const VmObjectRef ref, const std::string_view declared,
    Interpreter& vm) {
    auto& linker = vm.Linker();
    auto& model = vm.Model();
    if (!ref.IsValid()) return "null";
    if (!model.IsValidRef(ref)) return "<unavailable: invalid guest reference>";

    const auto java_class = model.ObjectClass(ref);
    const auto actual = java_class.IsValid()
                            ? linker.Class(java_class).descriptor
                            : std::string("<unknown-class>");
    if (actual == "Ljava/lang/String;") {
        return FormatString(ref, model, kMaximumFaultStringCodeUnits);
    }

    const auto identity =
        HexWord(static_cast<std::uint32_t>(model.IdentityHashCode(ref)));
    const auto type = actual == declared ? std::string{} : actual + " ";
    if (!java_class.IsValid() || g_rendering_fault_to_string) {
        return type + "<toString unavailable>@" + identity;
    }
    const auto slot = linker.FindVtableIndex(
        java_class, "toString", "()Ljava/lang/String;");
    if (!slot.has_value() || *slot >= linker.Class(java_class).vtable.size()) {
        return type + "<toString unavailable>@" + identity;
    }
    try {
        const FaultToStringScope scope;
        const std::array arguments{VmValue::Ref(ref)};
        const auto outcome = vm.Call(linker.Class(java_class).vtable[*slot],
                                     arguments);
        if (outcome.exception.IsValid()) {
            const auto exception = outcome.exception_class.IsValid()
                                       ? linker.Class(outcome.exception_class)
                                             .descriptor
                                       : std::string("<unknown>");
            return type + "<toString threw " + exception + ">@" + identity;
        }
        if (outcome.value.kind != VmValue::Kind::ref ||
            !outcome.value.ref.IsValid()) {
            return type + "<toString returned non-String>@" + identity;
        }
        return type + FormatString(outcome.value.ref, model,
                                   kMaximumFaultStringCodeUnits);
    } catch (...) {
        return type + "<toString failed>@" + identity;
    }
}

[[nodiscard]] std::string FormatReference(
    const Frame& frame, const std::uint32_t reg,
    const std::string_view declared, Interpreter& vm) {
    if (reg >= frame.regs.size()) return "<unavailable: register out of range>";
    const auto& slot = frame.regs[reg];
    if (slot.tag == SlotTag::cat1 && slot.bits == 0U) return "null";
    if (slot.tag != SlotTag::ref) {
        return "<unavailable: v" + std::to_string(reg) +
               " is not a reference>";
    }
    return FormatObjectToString(VmObjectRef(slot.bits), declared, vm);
}

[[nodiscard]] std::string PaddedType(const std::string_view descriptor) {
    std::string result(descriptor);
    if (result.size() < kArgumentTypeColumnWidth) {
        result.append(kArgumentTypeColumnWidth - result.size(), ' ');
    }
    return result;
}

[[nodiscard]] std::string FormatCat1(const Frame& frame,
                                     const std::uint32_t reg,
                                     const char descriptor) {
    if (reg >= frame.regs.size()) return "<unavailable: register out of range>";
    const auto& slot = frame.regs[reg];
    if (slot.tag != SlotTag::cat1) {
        return "<unavailable: v" + std::to_string(reg) +
               " is not a cat1 value>";
    }
    return FormatCat1Bits(slot.bits, descriptor);
}

[[nodiscard]] std::string FormatWide(const Frame& frame,
                                     const std::uint32_t reg,
                                     const char descriptor) {
    if (reg + 1U >= frame.regs.size()) {
        return "<unavailable: register pair out of range>";
    }
    const auto& lo = frame.regs[reg];
    const auto& hi = frame.regs[reg + 1U];
    if (lo.tag != SlotTag::wide_lo || hi.tag != SlotTag::wide_hi) {
        return "<unavailable: v" + std::to_string(reg) +
               " is not a wide pair>";
    }
    const auto bits = static_cast<std::uint64_t>(lo.bits) |
                      (static_cast<std::uint64_t>(hi.bits) << 32U);
    return FormatWideBits(bits, descriptor);
}

}  // namespace

void AppendFaultInvokeArguments(std::string& rendered, const Frame& frame,
                                Interpreter& vm) noexcept {
    try {
        auto& linker = vm.Linker();
        if (!frame.method->code.has_value() ||
            !frame.method->dex_unit.has_value() ||
            frame.pc >= frame.method->code->instructions.size()) {
            return;
        }
        const auto& units = frame.method->code->instructions;
        const auto unit = units[frame.pc];
        const auto opcode = static_cast<std::uint8_t>(unit & 0xffU);
        if (!((opcode >= 0x6eU && opcode <= 0x72U) ||
              (opcode >= 0x74U && opcode <= 0x78U))) {
            return;
        }
        if (frame.pc + 2U >= units.size()) {
            rendered += std::string(kFaultInvokeHeading) + " <unavailable>";
            return;
        }

        const auto method_index =
            static_cast<std::uint32_t>(units[frame.pc + 1U]);
        const auto& image = linker.Image(*frame.method->dex_unit);
        if (method_index >= image.methods.size()) {
            rendered += std::string(kFaultInvokeHeading) + " <unavailable>";
            return;
        }
        const auto& method = image.methods[method_index];
        if (method.class_type_index >= image.types.size() ||
            method.name_string_index >= image.strings.size() ||
            method.prototype_index >= image.prototypes.size()) {
            rendered += std::string(kFaultInvokeHeading) + " <unavailable>";
            return;
        }
        const auto& prototype = image.prototypes[method.prototype_index];
        if (prototype.return_type_index >= image.types.size()) {
            rendered += std::string(kFaultInvokeHeading) + " <unavailable>";
            return;
        }
        const auto& owner = image.types[method.class_type_index].descriptor;
        const auto name = DexStringUtf8(image.strings[method.name_string_index]);

        std::vector<std::uint32_t> registers;
        const bool is_range = opcode >= 0x74U;
        if (is_range) {
            const auto count = static_cast<std::uint32_t>(unit >> 8U);
            const auto first =
                static_cast<std::uint32_t>(units[frame.pc + 2U]);
            registers.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index) {
                registers.push_back(first + index);
            }
        } else {
            const auto count = static_cast<std::uint32_t>(unit >> 12U);
            const auto listed = units[frame.pc + 2U];
            const std::uint32_t candidates[5] = {
                listed & 0xfU, (listed >> 4U) & 0xfU,
                (listed >> 8U) & 0xfU, (listed >> 12U) & 0xfU,
                (unit >> 8U) & 0xfU};
            registers.assign(candidates, candidates + std::min(count, 5U));
        }

        const auto base = static_cast<std::uint8_t>(
            is_range ? opcode - 0x74U + 0x6eU : opcode);
        const bool is_static = base == 0x71U;
        std::size_t expected_words = is_static ? 0U : 1U;
        for (const auto type_index : prototype.parameter_type_indices) {
            if (type_index >= image.types.size()) {
                rendered += std::string(kFaultInvokeHeading) + " <unavailable>";
                return;
            }
            const auto& descriptor = image.types[type_index].descriptor;
            expected_words += !descriptor.empty() &&
                                      (descriptor.front() == 'J' ||
                                       descriptor.front() == 'D')
                                  ? 2U
                                  : 1U;
        }

        rendered += kFaultInvokeHeading;
        rendered += "\n  target   : " + owner + "->" + name;
        rendered += "\n  returns  : " +
                    image.types[prototype.return_type_index].descriptor;
        if (registers.size() != expected_words) {
            rendered += "\n  values   : <unavailable: encoded words=" +
                        std::to_string(registers.size()) +
                        " expected=" + std::to_string(expected_words) + ">";
            return;
        }

        std::size_t cursor{};
        if (!is_static) {
            rendered += "\n  receiver : " + owner + " = " +
                        FormatReference(frame, registers[cursor], owner, vm);
            ++cursor;
        }
        const auto shown = std::min(prototype.parameter_type_indices.size(),
                                    kMaximumFaultArguments);
        rendered += "\n  arguments (" +
                    std::to_string(prototype.parameter_type_indices.size()) +
                    "):";
        for (std::size_t index = 0; index < shown; ++index) {
            const auto& descriptor =
                image.types[prototype.parameter_type_indices[index]].descriptor;
            const auto kind = descriptor.empty() ? '\0' : descriptor.front();
            std::string value;
            if (kind == 'L' || kind == '[') {
                value =
                    FormatReference(frame, registers[cursor], descriptor, vm);
                ++cursor;
            } else if (kind == 'J' || kind == 'D') {
                value = FormatWide(frame, registers[cursor], kind);
                cursor += 2U;
            } else {
                value = FormatCat1(frame, registers[cursor], kind);
                ++cursor;
            }
            rendered += "\n    [";
            if (index < 10U) rendered += " ";
            rendered += std::to_string(index) + "] " +
                        PaddedType(descriptor) + " = " + value;
        }
        if (prototype.parameter_type_indices.size() > shown) {
            rendered += "\n    ... " +
                        std::to_string(prototype.parameter_type_indices.size() -
                                       shown) +
                        " arguments omitted";
        }
    } catch (...) {
        try {
            rendered += std::string(kFaultInvokeHeading) + " <unavailable>";
        } catch (...) {
        }
    }
}

}  // namespace ogplay::runtime::dexvm

#include "catalog.h"
#include "shared.h"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

#include "ogplay/hal/from_chars.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;
namespace {

constexpr std::string_view kDigits = "0123456789abcdefghijklmnopqrstuvwxyz";

[[noreturn]] void NumberFormat(std::string_view text) {
    throw VmJavaThrow{"Ljava/lang/NumberFormatException;",
                      "invalid number: " + std::string(text)};
}

[[nodiscard]] std::string GuestText(IntrinsicContext& context,
                                    VmObjectRef reference) {
    if (!reference.IsValid()) NumberFormat("null");
    return Narrow(context.vm.Model().StringValue(reference));
}

[[nodiscard]] std::string GuestFloatingText(IntrinsicContext& context,
                                            VmObjectRef reference) {
    if (!reference.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "null floating string"};
    }
    return Narrow(context.vm.Model().StringValue(reference));
}

[[nodiscard]] int DigitValue(char unit) {
    if (unit >= '0' && unit <= '9') return unit - '0';
    if (unit >= 'a' && unit <= 'z') return unit - 'a' + 10;
    if (unit >= 'A' && unit <= 'Z') return unit - 'A' + 10;
    return -1;
}

[[nodiscard]] std::int64_t ParseSignedRadix(std::string_view text, int radix,
                                            std::int64_t minimum,
                                            std::int64_t maximum) {
    if (radix < 2 || radix > 36 || text.empty()) NumberFormat(text);
    std::size_t index = 0;
    bool negative = false;
    if (text[index] == '-' || text[index] == '+') {
        negative = text[index] == '-';
        if (++index == text.size()) NumberFormat(text);
    }
    const auto limit = negative ? minimum : -maximum;
    const auto multiply_limit = limit / radix;
    std::int64_t result = 0;
    for (; index < text.size(); ++index) {
        const auto digit = DigitValue(text[index]);
        if (digit < 0 || digit >= radix || result < multiply_limit) {
            NumberFormat(text);
        }
        result *= radix;
        if (result < limit + digit) NumberFormat(text);
        result -= digit;
    }
    return negative ? result : -result;
}

[[nodiscard]] std::int64_t DecodeIntegral(std::string_view text,
                                          std::int64_t minimum,
                                          std::int64_t maximum) {
    if (text.empty()) NumberFormat(text);
    bool negative = false;
    std::size_t index = 0;
    if (text[index] == '-' || text[index] == '+') {
        negative = text[index] == '-';
        if (++index == text.size()) NumberFormat(text);
    }
    int radix = 10;
    if (text[index] == '0') {
        if (index + 1 == text.size()) return 0;
        if (text[index + 1] == 'x' || text[index + 1] == 'X') {
            radix = 16;
            index += 2;
        } else {
            radix = 8;
            ++index;
        }
    } else if (text[index] == '#') {
        radix = 16;
        ++index;
    }
    if (index == text.size() || text[index] == '-' || text[index] == '+') {
        NumberFormat(text);
    }
    std::string normalized;
    if (negative) normalized.push_back('-');
    normalized.append(text.substr(index));
    return ParseSignedRadix(normalized, radix, minimum, maximum);
}

[[nodiscard]] std::string FormatUnsigned(std::uint64_t value, int radix) {
    std::array<char, 65> buffer{};
    auto cursor = buffer.end();
    do {
        *--cursor = kDigits[value % static_cast<unsigned>(radix)];
        value /= static_cast<unsigned>(radix);
    } while (value != 0);
    return {cursor, buffer.end()};
}

[[nodiscard]] std::string FormatSigned(std::int64_t value, int radix) {
    if (radix < 2 || radix > 36) radix = 10;
    const bool negative = value < 0;
    const auto magnitude = negative
        ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
        : static_cast<std::uint64_t>(value);
    auto result = FormatUnsigned(magnitude, radix);
    if (negative) result.insert(result.begin(), '-');
    return result;
}

template <typename U>
[[nodiscard]] U ByteSwap(U value) {
    if constexpr (sizeof(U) == 2) {
        return static_cast<U>((value << 8U) | (value >> 8U));
    } else if constexpr (sizeof(U) == 4) {
        return static_cast<U>((value << 24U) |
            ((value << 8U) & 0x00ff0000U) |
            ((value >> 8U) & 0x0000ff00U) | (value >> 24U));
    } else {
        return static_cast<U>((value << 56U) |
            ((value << 40U) & 0x00ff000000000000ULL) |
            ((value << 24U) & 0x0000ff0000000000ULL) |
            ((value << 8U) & 0x000000ff00000000ULL) |
            ((value >> 8U) & 0x00000000ff000000ULL) |
            ((value >> 24U) & 0x0000000000ff0000ULL) |
            ((value >> 40U) & 0x000000000000ff00ULL) | (value >> 56U));
    }
}

[[nodiscard]] std::uint64_t ReadBoxed(IntrinsicContext& context,
                                      VmObjectRef object, bool wide) {
    if (!object.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "null wrapper"};
    }
    const auto slots = context.vm.Model().InstanceSlots(object);
    if (slots.size() < (wide ? 2U : 1U)) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "object is not a compatible wrapper"};
    }
    auto bits = static_cast<std::uint64_t>(slots[0].bits);
    if (wide) bits |= static_cast<std::uint64_t>(slots[1].bits) << 32U;
    return bits;
}

[[nodiscard]] bool IsExactClass(IntrinsicContext& context, VmObjectRef object,
                                std::string_view descriptor) {
    return object.IsValid() &&
           context.vm.Linker().Class(context.vm.Model().ObjectClass(object))
                   .descriptor == descriptor;
}

[[nodiscard]] VmObjectRef StaticReference(IntrinsicContext& context,
                                          std::string_view owner,
                                          std::string_view name,
                                          std::string_view descriptor) {
    const auto java_class = context.vm.Linker().FindClass(owner);
    if (!java_class.has_value()) return VmObjectRef{0};
    const auto field = context.vm.Linker().FindFieldRecursive(
        *java_class, std::string(name), std::string(descriptor));
    if (!field.has_value()) return VmObjectRef{0};
    const auto& linked = context.vm.Linker().Field(*field);
    return VmObjectRef{context.vm.Linker().Class(linked.owner)
                           .static_storage[linked.slot]};
}

void InitializeType(IntrinsicContext& context, std::string_view owner,
                    std::string_view primitive) {
    context.vm.SetIntrinsicStaticRef(
        owner, "TYPE", "Ljava/lang/Class;",
        context.vm.Model().ClassObject(
            context.vm.Linker().ResolveDescriptor(primitive)));
}

[[nodiscard]] VmValue Bool(bool value) {
    return VmValue::Int(value ? 1 : 0);
}

template <typename Float, typename Int>
[[nodiscard]] Int FloatToJavaInteger(Float value) {
    if (std::isnan(value)) return 0;
    if (value >= static_cast<Float>(std::numeric_limits<Int>::max())) {
        return std::numeric_limits<Int>::max();
    }
    if (value <= static_cast<Float>(std::numeric_limits<Int>::min())) {
        return std::numeric_limits<Int>::min();
    }
    return static_cast<Int>(value);
}

template <typename Float>
[[nodiscard]] std::string FormatFloating(Float value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return std::signbit(value) ? "-Infinity" : "Infinity";
    if (value == 0) return std::signbit(value) ? "-0.0" : "0.0";

    // API 19 RealToString deliberately prints extra precision for the
    // smallest float subnormals, and special-cases Double.MIN_VALUE.
    if constexpr (sizeof(Float) == sizeof(float)) {
        const auto bits = std::bit_cast<std::uint32_t>(static_cast<float>(value));
        const auto magnitude = bits & 0x7fffffffU;
        if (magnitude >= 1U && magnitude <= 7U) {
            static constexpr std::array<std::string_view, 7> kSmallSubnormals{
                "1.4E-45", "2.8E-45", "4.2E-45", "5.6E-45",
                "7.0E-45", "8.4E-45", "9.8E-45",
            };
            std::string text(kSmallSubnormals[magnitude - 1U]);
            if ((bits & 0x80000000U) != 0) text.insert(text.begin(), '-');
            return text;
        }
    } else {
        const auto bits =
            std::bit_cast<std::uint64_t>(static_cast<double>(value));
        if ((bits & 0x7fffffffffffffffULL) == 1ULL) {
            return (bits & 0x8000000000000000ULL) != 0
                       ? "-4.9E-324"
                       : "4.9E-324";
        }
    }

    std::array<char, 128> buffer{};
    const auto absolute = std::fabs(value);
    const auto format = absolute >= static_cast<Float>(1e-3) &&
                                absolute < static_cast<Float>(1e7)
                            ? std::chars_format::fixed
                            : std::chars_format::scientific;
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                         value, format);
    if (converted.ec != std::errc{}) NumberFormat("floating formatting");
    std::string text(buffer.data(), converted.ptr);
    auto exponent = text.find_first_of("eE");
    if (exponent == std::string::npos) {
        if (text.find('.') == std::string::npos) text += ".0";
        return text;
    }
    text[exponent] = 'E';
    if (text.find('.', 0) == std::string::npos || text.find('.') > exponent) {
        text.insert(exponent, ".0");
        exponent += 2;
    }
    std::size_t sign = exponent + 1;
    if (sign < text.size() && text[sign] == '+') text.erase(sign, 1);
    if (sign < text.size() && text[sign] == '-') ++sign;
    while (sign + 1 < text.size() && text[sign] == '0') text.erase(sign, 1);
    return text;
}

[[nodiscard]] bool FloatingMagnitudeBelowOne(std::string_view text,
                                             bool hex) {
    const auto start = text.starts_with('-') ? 1U : 0U;
    const auto marker = text.find_first_of(hex ? "pP" : "eE", start);
    const auto significand_end =
        marker == std::string_view::npos ? text.size() : marker;

    std::int64_t explicit_exponent = 0;
    if (marker != std::string_view::npos) {
        auto cursor = marker + 1U;
        bool negative = false;
        if (cursor < text.size() &&
            (text[cursor] == '+' || text[cursor] == '-')) {
            negative = text[cursor] == '-';
            ++cursor;
        }
        if (cursor == text.size()) NumberFormat(text);

        std::int64_t magnitude = 0;
        for (; cursor < text.size(); ++cursor) {
            if (text[cursor] < '0' || text[cursor] > '9') NumberFormat(text);
            if (magnitude < 1'000'000'000LL) {
                const auto next =
                    magnitude * 10 + (text[cursor] - '0');
                magnitude =
                    next > 1'000'000'000LL ? 1'000'000'000LL : next;
            }
        }
        explicit_exponent = negative ? -magnitude : magnitude;
    }

    bool seen_point = false;
    std::int64_t digits_before_point = 0;
    std::int64_t digit_index = 0;
    std::int64_t first_nonzero = -1;
    int leading_digit = 0;
    for (auto cursor = start; cursor < significand_end; ++cursor) {
        const auto unit = text[cursor];
        if (unit == '.') {
            if (seen_point) NumberFormat(text);
            seen_point = true;
            continue;
        }
        const auto digit = DigitValue(unit);
        if (digit < 0 || digit >= (hex ? 16 : 10)) NumberFormat(text);
        if (!seen_point) ++digits_before_point;
        if (first_nonzero < 0 && digit != 0) {
            first_nonzero = digit_index;
            leading_digit = digit;
        }
        ++digit_index;
    }
    if (digit_index == 0) NumberFormat(text);
    if (first_nonzero < 0) return true;

    if (!hex) {
        return explicit_exponent + digits_before_point - first_nonzero - 1 < 0;
    }

    const auto leading_bit =
        leading_digit >= 8 ? 3 : leading_digit >= 4 ? 2 :
        leading_digit >= 2 ? 1 : 0;
    return explicit_exponent +
               4 * (digits_before_point - first_nonzero - 1) +
               leading_bit <
           0;
}

template <typename Float>
[[nodiscard]] Float ParseFloating(std::string text) {
    const auto whitespace = [](unsigned char unit) { return unit <= 0x20; };
    while (!text.empty() && whitespace(text.front())) text.erase(text.begin());
    while (!text.empty() && whitespace(text.back())) text.pop_back();
    if (text == "NaN" || text == "+NaN" || text == "-NaN") {
        return std::numeric_limits<Float>::quiet_NaN();
    }
    if (text == "Infinity" || text == "+Infinity") {
        return std::numeric_limits<Float>::infinity();
    }
    if (text == "-Infinity") return -std::numeric_limits<Float>::infinity();
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F' ||
                          text.back() == 'd' || text.back() == 'D')) {
        text.pop_back();
    }
    if (text.empty()) NumberFormat(text);
    if (text.front() == '+') text.erase(text.begin());
    if (text.empty()) NumberFormat(text);
    Float result{};
    const char* begin = text.data();
    auto format = std::chars_format::general;
    bool hex = false;
    if (text.size() > 2 && (text.starts_with("0x") || text.starts_with("0X") ||
                            text.starts_with("-0x") || text.starts_with("-0X"))) {
        const std::size_t prefix = text[0] == '-' ? 1U : 0U;
        if (text.find_first_of("pP", prefix + 2U) == std::string::npos) {
            NumberFormat(text);
        }
        text.erase(prefix, 2);
        begin = text.data();
        format = std::chars_format::hex;
        hex = true;
    }
    const auto parsed = ogplay::hal::FromChars(
        begin, text.data() + text.size(), result, format);
    if (parsed.ec == std::errc::result_out_of_range) {
        const bool underflow = FloatingMagnitudeBelowOne(text, hex);
        const auto sign = text.starts_with('-') ? Float{-1} : Float{1};
        return underflow ? std::copysign(Float{0}, sign)
                         : std::copysign(std::numeric_limits<Float>::infinity(),
                                         sign);
    }
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        NumberFormat(text);
    }
    return result;
}

template <typename Float>
[[nodiscard]] std::string FormatHexFloating(Float value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return std::signbit(value) ? "-Infinity" : "Infinity";

    if constexpr (sizeof(Float) == sizeof(float)) {
        const auto bits = std::bit_cast<std::uint32_t>(static_cast<float>(value));
        const bool negative = (bits & 0x80000000U) != 0;
        const auto exponent = (bits & 0x7f800000U) >> 23U;
        auto significand = (bits & 0x007fffffU) << 1U;
        if (exponent == 0 && significand == 0) {
            return negative ? "-0x0.0p0" : "0x0.0p0";
        }

        std::string text = negative ? "-0x" : "0x";
        const bool subnormal = exponent == 0;
        text += subnormal ? "0." : "1.";

        std::size_t fraction_digits = 6;
        while (significand != 0 && (significand & 0xfU) == 0) {
            significand >>= 4U;
            --fraction_digits;
        }
        const auto fraction = FormatUnsigned(significand, 16);
        if (significand != 0 && fraction_digits > fraction.size()) {
            text.append(fraction_digits - fraction.size(), '0');
        }
        text += fraction;

        if (subnormal) {
            text += "p-126";
        } else {
            text += 'p';
            text += FormatSigned(static_cast<std::int64_t>(exponent) - 127, 10);
        }
        return text;
    } else {
        const auto bits =
            std::bit_cast<std::uint64_t>(static_cast<double>(value));
        const bool negative = (bits & 0x8000000000000000ULL) != 0;
        const auto exponent = (bits & 0x7ff0000000000000ULL) >> 52U;
        auto significand = bits & 0x000fffffffffffffULL;
        if (exponent == 0 && significand == 0) {
            return negative ? "-0x0.0p0" : "0x0.0p0";
        }

        std::string text = negative ? "-0x" : "0x";
        const bool subnormal = exponent == 0;
        text += subnormal ? "0." : "1.";

        std::size_t fraction_digits = 13;
        while (significand != 0 && (significand & 0xfULL) == 0) {
            significand >>= 4U;
            --fraction_digits;
        }
        const auto fraction = FormatUnsigned(significand, 16);
        if (significand != 0 && fraction_digits > fraction.size()) {
            text.append(fraction_digits - fraction.size(), '0');
        }
        text += fraction;

        if (subnormal) {
            text += "p-1022";
        } else {
            text += 'p';
            text += FormatSigned(static_cast<std::int64_t>(exponent) - 1023, 10);
        }
        return text;
    }
}

[[nodiscard]] std::uint32_t CanonicalFloatBits(float value) {
    return std::isnan(value) ? 0x7fc00000U : std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint64_t CanonicalDoubleBits(double value) {
    return std::isnan(value) ? 0x7ff8000000000000ULL
                             : std::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] int JavaFloatCompare(float left, float right) {
    if (left < right) return -1;
    if (left > right) return 1;
    const auto l = static_cast<std::int32_t>(CanonicalFloatBits(left));
    const auto r = static_cast<std::int32_t>(CanonicalFloatBits(right));
    return l == r ? 0 : (l < r ? -1 : 1);
}

[[nodiscard]] int JavaDoubleCompare(double left, double right) {
    if (left < right) return -1;
    if (left > right) return 1;
    const auto l = static_cast<std::int64_t>(CanonicalDoubleBits(left));
    const auto r = static_cast<std::int64_t>(CanonicalDoubleBits(right));
    return l == r ? 0 : (l < r ? -1 : 1);
}

[[nodiscard]] bool CharacterIsWhitespace(std::int32_t code_point) {
    if ((code_point >= 0x1c && code_point <= 0x20) ||
        (code_point >= 0x09 && code_point <= 0x0d)) return true;
    if (code_point == 0x1680 || code_point == 0x180e) return true;
    if (code_point == 0x2007 || code_point == 0x202f) return false;
    return (code_point >= 0x2000 && code_point <= 0x200a) ||
           code_point == 0x2028 || code_point == 0x2029 ||
           code_point == 0x205f || code_point == 0x3000;
}

[[nodiscard]] bool CharacterIsSpace(std::int32_t code_point) {
    return code_point == 0x20 || code_point == 0xa0 ||
           code_point == 0x1680 || code_point == 0x180e ||
           (code_point >= 0x2000 && code_point <= 0x200a) ||
           code_point == 0x2028 || code_point == 0x2029 ||
           code_point == 0x202f || code_point == 0x205f ||
           code_point == 0x3000;
}

void AddTypeField(IntrinsicClassBuilder& builder) {
    builder.StaticField("TYPE", "Ljava/lang/Class;");
}

void AddNumberConversions(IntrinsicClassBuilder& builder, bool wide,
                          char kind) {
    const auto numeric = [wide, kind](IntrinsicContext& context) {
        const auto bits = ReadBoxed(context, context.receiver, wide);
        if (kind == 'F') return static_cast<long double>(
            std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
        if (kind == 'D') return static_cast<long double>(std::bit_cast<double>(bits));
        if (wide) return static_cast<long double>(static_cast<std::int64_t>(bits));
        return static_cast<long double>(static_cast<std::int32_t>(bits));
    };
    builder.FinalMethod("byteValue", "()B", [numeric, kind](IntrinsicContext& c) {
        const auto value = numeric(c);
        const auto integer = (kind == 'F' || kind == 'D')
            ? FloatToJavaInteger<long double, std::int32_t>(value)
            : static_cast<std::int32_t>(value);
        return VmValue::Int(static_cast<std::int8_t>(integer)); });
    builder.FinalMethod("shortValue", "()S", [numeric, kind](IntrinsicContext& c) {
        const auto value = numeric(c);
        const auto integer = (kind == 'F' || kind == 'D')
            ? FloatToJavaInteger<long double, std::int32_t>(value)
            : static_cast<std::int32_t>(value);
        return VmValue::Int(static_cast<std::int16_t>(integer)); });
    builder.FinalMethod("intValue", "()I", [numeric, kind](IntrinsicContext& c) {
        const auto v = numeric(c);
        return VmValue::Int((kind == 'F' || kind == 'D')
            ? FloatToJavaInteger<long double, std::int32_t>(v)
            : static_cast<std::int32_t>(v)); });
    builder.FinalMethod("longValue", "()J", [numeric, kind](IntrinsicContext& c) {
        const auto v = numeric(c);
        return VmValue::Long((kind == 'F' || kind == 'D')
            ? FloatToJavaInteger<long double, std::int64_t>(v)
            : static_cast<std::int64_t>(v)); });
    builder.FinalMethod("floatValue", "()F", [numeric](IntrinsicContext& c) {
        return VmValue::Float(static_cast<float>(numeric(c))); });
    builder.FinalMethod("doubleValue", "()D", [numeric](IntrinsicContext& c) {
        return VmValue::Double(static_cast<double>(numeric(c))); });
}

IntrinsicClassDecl Declare_java_lang_Number() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Number;", "Ljava/lang/Object;", {"Ljava/io/Serializable;"});
    builder.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
    const auto abstract = [](IntrinsicContext&) -> VmValue {
        throw VmJavaThrow{"Ljava/lang/AbstractMethodError;",
                          "abstract Number conversion"};
    };
    builder.VirtualMethod("byteValue", "()B", abstract);
    builder.VirtualMethod("shortValue", "()S", abstract);
    builder.VirtualMethod("intValue", "()I", abstract);
    builder.VirtualMethod("longValue", "()J", abstract);
    builder.VirtualMethod("floatValue", "()F", abstract);
    builder.VirtualMethod("doubleValue", "()D", abstract);
    return std::move(builder).Build();
}

template <typename T>
void AddSmallIntegralCommon(IntrinsicClassBuilder& builder,
                            const char* descriptor, const char* primitive,
                            const char* parse_name, const char* value_of_desc,
                            std::int64_t minimum, std::int64_t maximum) {
    const auto set = [](IntrinsicContext& context, std::int64_t value) {
        SetBoxedBits(context, context.receiver,
                     static_cast<std::uint32_t>(static_cast<std::int32_t>(value)),
                     false);
        return VmValue::Void();
    };
    builder.Constructor(std::string("(") + primitive + ")V",
                    [set](IntrinsicContext& c) { return set(c, c.arguments[0].AsInt()); });
    builder.Constructor("(Ljava/lang/String;)V",
                    [set, minimum, maximum](IntrinsicContext& c) {
        return set(c, ParseSignedRadix(GuestText(c, c.arguments[0].ref), 10,
                                       minimum, maximum)); });
    AddNumberConversions(builder, false, 'I');
    builder.StaticMethod(parse_name, std::string("(Ljava/lang/String;)") + primitive,
                   [minimum, maximum](IntrinsicContext& c) {
        return VmValue::Int(static_cast<std::int32_t>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), 10, minimum, maximum))); });
    builder.StaticMethod(parse_name, std::string("(Ljava/lang/String;I)") + primitive,
                   [minimum, maximum](IntrinsicContext& c) {
        return VmValue::Int(static_cast<std::int32_t>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), c.arguments[1].AsInt(), minimum,
            maximum))); });
    builder.StaticMethod("decode", std::string("(Ljava/lang/String;)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        const auto value = DecodeIntegral(GuestText(c, c.arguments[0].ref),
                                          minimum, maximum);
        return MakeBoxed(c, descriptor, static_cast<std::uint32_t>(value), false);
    });
    builder.StaticMethod("valueOf", value_of_desc,
                   [descriptor](IntrinsicContext& c) {
        return MakeBoxed(c, descriptor,
                         static_cast<std::uint32_t>(c.arguments[0].AsInt()), false);
    });
    builder.StaticMethod("valueOf", std::string("(Ljava/lang/String;)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        const auto value = ParseSignedRadix(GuestText(c, c.arguments[0].ref), 10,
                                            minimum, maximum);
        return MakeBoxed(c, descriptor, static_cast<std::uint32_t>(value), false);
    });
    builder.StaticMethod("valueOf", std::string("(Ljava/lang/String;I)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        const auto value = ParseSignedRadix(GuestText(c, c.arguments[0].ref),
            c.arguments[1].AsInt(), minimum, maximum);
        return MakeBoxed(c, descriptor, static_cast<std::uint32_t>(value), false);
    });
    builder.FinalMethod("compareTo", std::string("(") + descriptor + ")I",
                    [](IntrinsicContext& c) {
        const auto left = static_cast<T>(ReadBoxed(c, c.receiver, false));
        const auto right = static_cast<T>(ReadBoxed(c, c.arguments[0].ref, false));
        return VmValue::Int(left < right ? -1 : (left > right ? 1 : 0)); });
    builder.StaticMethod("compare", std::string("(") + primitive + primitive + ")I",
                   [](IntrinsicContext& c) {
        const auto left = static_cast<T>(c.arguments[0].AsInt());
        const auto right = static_cast<T>(c.arguments[1].AsInt());
        return VmValue::Int(left < right ? -1 : (left > right ? 1 : 0)); });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z",
                    [descriptor](IntrinsicContext& c) {
        return Bool(IsExactClass(c, c.arguments[0].ref, descriptor) &&
                    static_cast<T>(ReadBoxed(c, c.receiver, false)) ==
                    static_cast<T>(ReadBoxed(c, c.arguments[0].ref, false))); });
    builder.FinalMethod("hashCode", "()I", [](IntrinsicContext& c) {
        return VmValue::Int(static_cast<T>(ReadBoxed(c, c.receiver, false))); });
    builder.FinalMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& c) {
        return Make(c, Widen(FormatSigned(
            static_cast<T>(ReadBoxed(c, c.receiver, false)), 10))); });
    builder.StaticMethod("toString", std::string("(") + primitive + ")Ljava/lang/String;",
                   [](IntrinsicContext& c) {
        return Make(c, Widen(FormatSigned(static_cast<T>(c.arguments[0].AsInt()), 10))); });
}

IntrinsicClassDecl Declare_java_lang_Byte() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Byte;", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    builder.InstanceField("value", "B");
    builder.ConstantInt("MAX_VALUE", "B", 127).ConstantInt("MIN_VALUE", "B", -128)
           .ConstantInt("SIZE", "I", 8);
    AddTypeField(builder);
    AddSmallIntegralCommon<std::int8_t>(builder, "Ljava/lang/Byte;", "B",
        "parseByte", "(B)Ljava/lang/Byte;", -128, 127);
    builder.StaticMethod("toHexString", "(BZ)Ljava/lang/String;", [](IntrinsicContext& c) {
        auto text = FormatUnsigned(static_cast<std::uint8_t>(c.arguments[0].AsInt()), 16);
        if (text.size() < 2) text.insert(text.begin(), '0');
        if (c.arguments[1].AsInt() != 0) {
            for (auto& unit : text) if (unit >= 'a') unit -= 32;
        }
        return Make(c, Widen(text));
    });
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Byte;", "B"); return VmValue::Void(); });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_Short() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Short;", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    builder.InstanceField("value", "S");
    builder.ConstantInt("MAX_VALUE", "S", 32767).ConstantInt("MIN_VALUE", "S", -32768)
           .ConstantInt("SIZE", "I", 16);
    AddTypeField(builder);
    AddSmallIntegralCommon<std::int16_t>(builder, "Ljava/lang/Short;", "S",
        "parseShort", "(S)Ljava/lang/Short;", -32768, 32767);
    builder.StaticMethod("reverseBytes", "(S)S", [](IntrinsicContext& c) {
        const auto v = static_cast<std::uint16_t>(c.arguments[0].AsInt());
        return VmValue::Int(static_cast<std::int16_t>(ByteSwap(v))); });
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Short;", "S"); return VmValue::Void(); });
    return std::move(builder).Build();
}

template <typename T, typename U>
void AddWideIntegralCommon(IntrinsicClassBuilder& builder,
                           const char* descriptor, const char* primitive,
                           const char* parse_name, const char* property_name,
                           int width) {
    constexpr bool wide = sizeof(T) == 8;
    const auto argument = [](const VmValue& value) -> T {
        if constexpr (wide) {
            return static_cast<T>(value.AsLong());
        } else {
            return static_cast<T>(value.AsInt());
        }
    };
    const auto answer = [](T value) -> VmValue {
        if constexpr (wide) {
            return VmValue::Long(value);
        } else {
            return VmValue::Int(value);
        }
    };
    const auto minimum = std::numeric_limits<T>::min();
    const auto maximum = std::numeric_limits<T>::max();
    builder.Constructor(std::string("(") + primitive + ")V",
                    [argument](IntrinsicContext& c) {
        const auto value = argument(c.arguments[0]);
        SetBoxedBits(c, c.receiver, static_cast<U>(value), wide);
        return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
                    [minimum, maximum](IntrinsicContext& c) {
        const auto value = static_cast<T>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), 10, minimum, maximum));
        SetBoxedBits(c, c.receiver, static_cast<U>(value), wide);
        return VmValue::Void(); });
    AddNumberConversions(builder, wide, wide ? 'J' : 'I');
    builder.StaticMethod(parse_name, std::string("(Ljava/lang/String;)") + primitive,
                   [answer, minimum, maximum](IntrinsicContext& c) {
        return answer(static_cast<T>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), 10, minimum, maximum))); });
    builder.StaticMethod(parse_name, std::string("(Ljava/lang/String;I)") + primitive,
                   [answer, minimum, maximum](IntrinsicContext& c) {
        return answer(static_cast<T>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), c.arguments[1].AsInt(), minimum,
            maximum))); });
    builder.StaticMethod("decode", std::string("(Ljava/lang/String;)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        const auto value = static_cast<T>(DecodeIntegral(
            GuestText(c, c.arguments[0].ref), minimum, maximum));
        return MakeBoxed(c, descriptor, static_cast<U>(value), wide); });
    builder.StaticMethod("valueOf", std::string("(") + primitive + ")" + descriptor,
                   [descriptor, argument](IntrinsicContext& c) {
        return MakeBoxed(c, descriptor, static_cast<U>(argument(c.arguments[0])), wide); });
    builder.StaticMethod("valueOf", std::string("(Ljava/lang/String;)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        const auto value = static_cast<T>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), 10, minimum, maximum));
        return MakeBoxed(c, descriptor, static_cast<U>(value), wide); });
    builder.StaticMethod("valueOf", std::string("(Ljava/lang/String;I)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        const auto value = static_cast<T>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), c.arguments[1].AsInt(), minimum,
            maximum));
        return MakeBoxed(c, descriptor, static_cast<U>(value), wide); });
    builder.FinalMethod("compareTo", std::string("(") + descriptor + ")I",
                    [](IntrinsicContext& c) {
        const auto left = static_cast<T>(ReadBoxed(c, c.receiver, wide));
        const auto right = static_cast<T>(ReadBoxed(c, c.arguments[0].ref, wide));
        return VmValue::Int(left < right ? -1 : (left > right ? 1 : 0)); });
    builder.StaticMethod("compare", std::string("(") + primitive + primitive + ")I",
                   [argument](IntrinsicContext& c) {
        const auto left = argument(c.arguments[0]);
        const auto right = argument(c.arguments[1]);
        return VmValue::Int(left < right ? -1 : (left > right ? 1 : 0)); });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z", [descriptor](IntrinsicContext& c) {
        return Bool(IsExactClass(c, c.arguments[0].ref, descriptor) &&
                    ReadBoxed(c, c.receiver, wide) == ReadBoxed(c, c.arguments[0].ref, wide)); });
    builder.FinalMethod("hashCode", "()I", [](IntrinsicContext& c) {
        const auto bits = ReadBoxed(c, c.receiver, wide);
        return VmValue::Int(wide ? static_cast<std::int32_t>(bits ^ (bits >> 32U))
                                 : static_cast<std::int32_t>(bits)); });
    builder.FinalMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& c) {
        return Make(c, Widen(FormatSigned(static_cast<T>(ReadBoxed(c, c.receiver, wide)), 10))); });
    builder.StaticMethod("toString", std::string("(") + primitive + ")Ljava/lang/String;",
                   [argument](IntrinsicContext& c) {
        return Make(c, Widen(FormatSigned(argument(c.arguments[0]), 10))); });
    builder.StaticMethod("toString", std::string("(") + primitive + "I)Ljava/lang/String;",
                   [argument](IntrinsicContext& c) {
        return Make(c, Widen(FormatSigned(argument(c.arguments[0]), c.arguments[1].AsInt()))); });
    for (const auto& [name, radix] : std::array<std::pair<const char*, int>, 3>{
             {{"toBinaryString", 2}, {"toHexString", 16}, {"toOctalString", 8}}}) {
        builder.StaticMethod(name, std::string("(") + primitive + ")Ljava/lang/String;",
                       [argument, radix](IntrinsicContext& c) {
            return Make(c, Widen(FormatUnsigned(static_cast<U>(argument(c.arguments[0])), radix))); });
    }
    builder.StaticMethod(property_name, std::string("(Ljava/lang/String;)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid()) return VmValue::Ref(VmObjectRef{0});
        const auto key = GuestText(c, c.arguments[0].ref);
        if (key.empty()) return VmValue::Ref(VmObjectRef{0});
        const auto property = c.vm.GetSystemProperty(key);
        if (!property.has_value()) return VmValue::Ref(VmObjectRef{});
        try {
            const auto value = DecodeIntegral(*property, minimum, maximum);
            return MakeBoxed(c, descriptor, static_cast<U>(static_cast<T>(value)), wide);
        } catch (const VmJavaThrow&) { return VmValue::Ref(VmObjectRef{}); }
    });
    builder.StaticMethod(property_name, std::string("(Ljava/lang/String;") + primitive + ")" + descriptor,
                   [descriptor, argument, minimum, maximum](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid()) {
            return MakeBoxed(c, descriptor,
                static_cast<U>(argument(c.arguments[1])), wide);
        }
        const auto key = GuestText(c, c.arguments[0].ref);
        if (key.empty()) return MakeBoxed(c, descriptor,
            static_cast<U>(argument(c.arguments[1])), wide);
        const auto property = c.vm.GetSystemProperty(key);
        T value = argument(c.arguments[1]);
        if (property.has_value()) {
            try { value = static_cast<T>(DecodeIntegral(*property, minimum, maximum)); }
            catch (const VmJavaThrow&) {}
        }
        return MakeBoxed(c, descriptor, static_cast<U>(value), wide);
    });
    builder.StaticMethod(property_name, std::string("(Ljava/lang/String;") + descriptor + ")" + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid()) return VmValue::Ref(c.arguments[1].ref);
        const auto key = GuestText(c, c.arguments[0].ref);
        if (key.empty()) return VmValue::Ref(c.arguments[1].ref);
        const auto property = c.vm.GetSystemProperty(key);
        if (property.has_value()) {
            try {
                const auto value = static_cast<T>(DecodeIntegral(*property, minimum, maximum));
                return MakeBoxed(c, descriptor, static_cast<U>(value), wide);
            } catch (const VmJavaThrow&) {}
        }
        return VmValue::Ref(c.arguments[1].ref);
    });
    builder.StaticMethod("highestOneBit", std::string("(") + primitive + ")" + primitive,
                   [argument, answer, width](IntrinsicContext& c) {
        const U value = static_cast<U>(argument(c.arguments[0]));
        return answer(static_cast<T>(value == 0 ? 0 : U{1} << (width - 1 - std::countl_zero(value)))); });
    builder.StaticMethod("lowestOneBit", std::string("(") + primitive + ")" + primitive,
                   [argument, answer](IntrinsicContext& c) {
        const U value = static_cast<U>(argument(c.arguments[0]));
        return answer(static_cast<T>(value & (U{0} - value))); });
    builder.StaticMethod("numberOfLeadingZeros", std::string("(") + primitive + ")I",
                   [argument](IntrinsicContext& c) { return VmValue::Int(std::countl_zero(static_cast<U>(argument(c.arguments[0])))); });
    builder.StaticMethod("numberOfTrailingZeros", std::string("(") + primitive + ")I",
                   [argument](IntrinsicContext& c) { return VmValue::Int(std::countr_zero(static_cast<U>(argument(c.arguments[0])))); });
    builder.StaticMethod("bitCount", std::string("(") + primitive + ")I",
                   [argument](IntrinsicContext& c) { return VmValue::Int(std::popcount(static_cast<U>(argument(c.arguments[0])))); });
    builder.StaticMethod("rotateLeft", std::string("(") + primitive + "I)" + primitive,
                   [argument, answer](IntrinsicContext& c) { return answer(static_cast<T>(std::rotl(static_cast<U>(argument(c.arguments[0])), c.arguments[1].AsInt()))); });
    builder.StaticMethod("rotateRight", std::string("(") + primitive + "I)" + primitive,
                   [argument, answer](IntrinsicContext& c) { return answer(static_cast<T>(std::rotr(static_cast<U>(argument(c.arguments[0])), c.arguments[1].AsInt()))); });
    builder.StaticMethod("reverseBytes", std::string("(") + primitive + ")" + primitive,
                   [argument, answer](IntrinsicContext& c) { return answer(static_cast<T>(ByteSwap(static_cast<U>(argument(c.arguments[0]))))); });
    builder.StaticMethod("reverse", std::string("(") + primitive + ")" + primitive,
                   [argument, answer, width](IntrinsicContext& c) {
        U value = static_cast<U>(argument(c.arguments[0])); U reversed = 0;
        for (int i = 0; i < width; ++i) { reversed = static_cast<U>((reversed << 1U) | (value & 1U)); value >>= 1U; }
        return answer(static_cast<T>(reversed)); });
    builder.StaticMethod("signum", std::string("(") + primitive + ")I",
                   [argument](IntrinsicContext& c) { const auto v = argument(c.arguments[0]); return VmValue::Int(v < 0 ? -1 : (v > 0 ? 1 : 0)); });
}

IntrinsicClassDecl Declare_java_lang_Integer() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Integer;", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    builder.InstanceField("value", "I").ConstantInt("MAX_VALUE", "I", 0x7fffffff)
           .ConstantInt("MIN_VALUE", "I", std::numeric_limits<std::int32_t>::min())
           .ConstantInt("SIZE", "I", 32);
    AddTypeField(builder);
    AddWideIntegralCommon<std::int32_t, std::uint32_t>(builder, "Ljava/lang/Integer;", "I", "parseInt", "getInteger", 32);
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Integer;", "I"); return VmValue::Void(); });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_Long() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Long;", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    builder.InstanceField("value", "J").ConstantInt("MAX_VALUE", "J", std::numeric_limits<std::int64_t>::max())
           .ConstantInt("MIN_VALUE", "J", std::numeric_limits<std::int64_t>::min())
           .ConstantInt("SIZE", "I", 64);
    AddTypeField(builder);
    AddWideIntegralCommon<std::int64_t, std::uint64_t>(builder, "Ljava/lang/Long;", "J", "parseLong", "getLong", 64);
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Long;", "J"); return VmValue::Void(); });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_Boolean() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Boolean;", "Ljava/lang/Object;", {"Ljava/io/Serializable;", "Ljava/lang/Comparable;"});
    builder.InstanceField("value", "Z")
           .StaticField("TRUE", "Ljava/lang/Boolean;")
           .StaticField("FALSE", "Ljava/lang/Boolean;");
    AddTypeField(builder);
    const auto parsed = [](IntrinsicContext& c, VmObjectRef text) {
        const auto value = Value(c, text);
        if (value.size() != 4) return false;
        return AsciiLower(value[0]) == u't' && AsciiLower(value[1]) == u'r' &&
               AsciiLower(value[2]) == u'u' && AsciiLower(value[3]) == u'e';
    };
    builder.Constructor("(Z)V", [](IntrinsicContext& c) { SetBoxedBits(c, c.receiver, c.arguments[0].AsInt() != 0, false); return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V", [parsed](IntrinsicContext& c) { SetBoxedBits(c, c.receiver, c.arguments[0].ref.IsValid() && parsed(c, c.arguments[0].ref), false); return VmValue::Void(); });
    builder.FinalMethod("booleanValue", "()Z", [](IntrinsicContext& c) { return Bool(ReadBoxed(c, c.receiver, false) != 0); });
    builder.StaticMethod("parseBoolean", "(Ljava/lang/String;)Z", [parsed](IntrinsicContext& c) { return Bool(c.arguments[0].ref.IsValid() && parsed(c, c.arguments[0].ref)); });
    const auto canonical = [](IntrinsicContext& c, bool value) { return VmValue::Ref(StaticReference(c, "Ljava/lang/Boolean;", value ? "TRUE" : "FALSE", "Ljava/lang/Boolean;")); };
    builder.StaticMethod("valueOf", "(Z)Ljava/lang/Boolean;", [canonical](IntrinsicContext& c) { return canonical(c, c.arguments[0].AsInt() != 0); });
    builder.StaticMethod("valueOf", "(Ljava/lang/String;)Ljava/lang/Boolean;", [canonical, parsed](IntrinsicContext& c) { return canonical(c, c.arguments[0].ref.IsValid() && parsed(c, c.arguments[0].ref)); });
    builder.StaticMethod("compare", "(ZZ)I", [](IntrinsicContext& c) { return VmValue::Int((c.arguments[0].AsInt() != 0) - (c.arguments[1].AsInt() != 0)); });
    builder.FinalMethod("compareTo", "(Ljava/lang/Boolean;)I", [](IntrinsicContext& c) { return VmValue::Int((ReadBoxed(c, c.receiver, false) != 0) - (ReadBoxed(c, c.arguments[0].ref, false) != 0)); });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z", [](IntrinsicContext& c) { return Bool(IsExactClass(c, c.arguments[0].ref, "Ljava/lang/Boolean;") && ReadBoxed(c, c.receiver, false) == ReadBoxed(c, c.arguments[0].ref, false)); });
    builder.FinalMethod("hashCode", "()I", [](IntrinsicContext& c) { return VmValue::Int(ReadBoxed(c, c.receiver, false) != 0 ? 1231 : 1237); });
    builder.FinalMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& c) { return Make(c, ReadBoxed(c, c.receiver, false) != 0 ? u"true" : u"false"); });
    builder.StaticMethod("toString", "(Z)Ljava/lang/String;", [](IntrinsicContext& c) { return Make(c, c.arguments[0].AsInt() != 0 ? u"true" : u"false"); });
    builder.StaticMethod("getBoolean", "(Ljava/lang/String;)Z", [](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid()) return Bool(false);
        const auto property = c.vm.GetSystemProperty(GuestText(c, c.arguments[0].ref));
        if (!property.has_value()) return Bool(false);
        std::string value = *property; for (auto& unit : value) if (unit >= 'A' && unit <= 'Z') unit += 32;
        return Bool(value == "true"); });
    builder.ClassInitializer([](IntrinsicContext& c) {
        InitializeType(c, "Ljava/lang/Boolean;", "Z");
        c.vm.SetIntrinsicStaticRef("Ljava/lang/Boolean;", "TRUE", "Ljava/lang/Boolean;", MakeBoxed(c, "Ljava/lang/Boolean;", 1, false).ref);
        c.vm.SetIntrinsicStaticRef("Ljava/lang/Boolean;", "FALSE", "Ljava/lang/Boolean;", MakeBoxed(c, "Ljava/lang/Boolean;", 0, false).ref);
        return VmValue::Void(); });
    return std::move(builder).Build();
}

template <typename Float, typename Bits>
void AddFloatingCommon(IntrinsicClassBuilder& builder, const char* descriptor,
                       const char* primitive, const char* parse_name,
                       const char* bits_to_name, const char* raw_bits_name,
                       const char* from_bits_name) {
    constexpr bool wide = sizeof(Float) == 8;
    const auto argument = [](const VmValue& value) -> Float { if constexpr (wide) return static_cast<Float>(value.AsDouble()); else return static_cast<Float>(value.AsFloat()); };
    const auto answer = [](Float value) -> VmValue { if constexpr (wide) return VmValue::Double(value); else return VmValue::Float(value); };
    const auto bits = [](Float value) -> Bits { return std::bit_cast<Bits>(value); };
    builder.Constructor(std::string("(") + primitive + ")V", [argument, bits](IntrinsicContext& c) { SetBoxedBits(c, c.receiver, bits(argument(c.arguments[0])), wide); return VmValue::Void(); });
    if constexpr (!wide) builder.Constructor("(D)V", [](IntrinsicContext& c) { SetBoxedBits(c, c.receiver, std::bit_cast<std::uint32_t>(static_cast<float>(c.arguments[0].AsDouble())), false); return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext& c) { const auto v = ParseFloating<Float>(GuestFloatingText(c, c.arguments[0].ref)); SetBoxedBits(c, c.receiver, std::bit_cast<Bits>(v), wide); return VmValue::Void(); });
    AddNumberConversions(builder, wide, wide ? 'D' : 'F');
    builder.StaticMethod(parse_name, std::string("(Ljava/lang/String;)") + primitive, [answer](IntrinsicContext& c) { return answer(ParseFloating<Float>(GuestFloatingText(c, c.arguments[0].ref))); });
    builder.StaticMethod("valueOf", std::string("(") + primitive + ")" + descriptor, [descriptor, argument, bits](IntrinsicContext& c) { return MakeBoxed(c, descriptor, bits(argument(c.arguments[0])), wide); });
    builder.StaticMethod("valueOf", std::string("(Ljava/lang/String;)") + descriptor, [descriptor](IntrinsicContext& c) { const auto v = ParseFloating<Float>(GuestFloatingText(c, c.arguments[0].ref)); return MakeBoxed(c, descriptor, std::bit_cast<Bits>(v), wide); });
    builder.StaticMethod("isNaN", std::string("(") + primitive + ")Z", [argument](IntrinsicContext& c) { return Bool(std::isnan(argument(c.arguments[0]))); });
    builder.FinalMethod("isNaN", "()Z", [](IntrinsicContext& c) { return Bool(std::isnan(std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.receiver, wide))))); });
    builder.StaticMethod("isInfinite", std::string("(") + primitive + ")Z", [argument](IntrinsicContext& c) { return Bool(std::isinf(argument(c.arguments[0]))); });
    builder.FinalMethod("isInfinite", "()Z", [](IntrinsicContext& c) { return Bool(std::isinf(std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.receiver, wide))))); });
    builder.StaticMethod("compare", std::string("(") + primitive + primitive + ")I", [argument](IntrinsicContext& c) { if constexpr (wide) return VmValue::Int(JavaDoubleCompare(argument(c.arguments[0]), argument(c.arguments[1]))); else return VmValue::Int(JavaFloatCompare(argument(c.arguments[0]), argument(c.arguments[1]))); });
    builder.FinalMethod("compareTo", std::string("(") + descriptor + ")I", [](IntrinsicContext& c) { const auto left = std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.receiver, wide))); const auto right = std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.arguments[0].ref, wide))); if constexpr (wide) return VmValue::Int(JavaDoubleCompare(left, right)); else return VmValue::Int(JavaFloatCompare(left, right)); });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z", [descriptor](IntrinsicContext& c) { if (!IsExactClass(c, c.arguments[0].ref, descriptor)) return Bool(false); const auto left = std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.receiver, wide))); const auto right = std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.arguments[0].ref, wide))); if constexpr (wide) return Bool(CanonicalDoubleBits(left) == CanonicalDoubleBits(right)); else return Bool(CanonicalFloatBits(left) == CanonicalFloatBits(right)); });
    builder.FinalMethod("hashCode", "()I", [](IntrinsicContext& c) { const auto value = std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.receiver, wide))); if constexpr (wide) { const auto canonical = CanonicalDoubleBits(value); return VmValue::Int(static_cast<std::int32_t>(canonical ^ (canonical >> 32U))); } else return VmValue::Int(static_cast<std::int32_t>(CanonicalFloatBits(value))); });
    builder.FinalMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& c) { return Make(c, Widen(FormatFloating(std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.receiver, wide)))))); });
    builder.StaticMethod("toString", std::string("(") + primitive + ")Ljava/lang/String;", [argument](IntrinsicContext& c) { return Make(c, Widen(FormatFloating(argument(c.arguments[0])))); });
    builder.StaticMethod("toHexString", std::string("(") + primitive + ")Ljava/lang/String;", [argument](IntrinsicContext& c) { return Make(c, Widen(FormatHexFloating(argument(c.arguments[0])))); });
    builder.StaticMethod(bits_to_name, std::string("(") + primitive + ")" + (wide ? "J" : "I"), [argument](IntrinsicContext& c) { if constexpr (wide) return VmValue::Long(static_cast<std::int64_t>(CanonicalDoubleBits(argument(c.arguments[0])))); else return VmValue::Int(static_cast<std::int32_t>(CanonicalFloatBits(argument(c.arguments[0])))); });
    builder.StaticMethod(raw_bits_name, std::string("(") + primitive + ")" + (wide ? "J" : "I"), [argument, bits](IntrinsicContext& c) { if constexpr (wide) return VmValue::Long(static_cast<std::int64_t>(bits(argument(c.arguments[0])))); else return VmValue::Int(static_cast<std::int32_t>(bits(argument(c.arguments[0])))); });
    builder.StaticMethod(from_bits_name, std::string("(") + (wide ? "J" : "I") + ")" + primitive, [answer](IntrinsicContext& c) { Bits raw; if constexpr (wide) raw = static_cast<Bits>(c.arguments[0].AsLong()); else raw = static_cast<Bits>(c.arguments[0].AsInt()); return answer(std::bit_cast<Float>(raw)); });
}

IntrinsicClassDecl Declare_java_lang_Float() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Float;", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    builder.InstanceField("value", "F");
    AddTypeField(builder);
    builder.ConstantInt("MAX_VALUE", "F", std::bit_cast<std::int32_t>(std::numeric_limits<float>::max()))
           .ConstantInt("MIN_VALUE", "F", std::bit_cast<std::int32_t>(std::numeric_limits<float>::denorm_min()))
           .ConstantInt("NaN", "F", 0x7fc00000).ConstantInt("POSITIVE_INFINITY", "F", 0x7f800000)
           .ConstantInt("NEGATIVE_INFINITY", "F", static_cast<std::int32_t>(0xff800000U))
           .ConstantInt("MIN_NORMAL", "F", std::bit_cast<std::int32_t>(std::numeric_limits<float>::min()))
           .ConstantInt("MAX_EXPONENT", "I", 127).ConstantInt("MIN_EXPONENT", "I", -126)
           .ConstantInt("SIZE", "I", 32);
    AddFloatingCommon<float, std::uint32_t>(builder, "Ljava/lang/Float;", "F", "parseFloat", "floatToIntBits", "floatToRawIntBits", "intBitsToFloat");
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Float;", "F"); return VmValue::Void(); });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_Double() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Double;", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    builder.InstanceField("value", "D");
    AddTypeField(builder);
    builder.ConstantInt("MAX_VALUE", "D", std::bit_cast<std::int64_t>(std::numeric_limits<double>::max()))
           .ConstantInt("MIN_VALUE", "D", std::bit_cast<std::int64_t>(std::numeric_limits<double>::denorm_min()))
           .ConstantInt("NaN", "D", std::bit_cast<std::int64_t>(std::numeric_limits<double>::quiet_NaN()))
           .ConstantInt("POSITIVE_INFINITY", "D", std::bit_cast<std::int64_t>(std::numeric_limits<double>::infinity()))
           .ConstantInt("NEGATIVE_INFINITY", "D", std::bit_cast<std::int64_t>(-std::numeric_limits<double>::infinity()))
           .ConstantInt("MIN_NORMAL", "D", std::bit_cast<std::int64_t>(std::numeric_limits<double>::min()))
           .ConstantInt("MAX_EXPONENT", "I", 1023).ConstantInt("MIN_EXPONENT", "I", -1022)
           .ConstantInt("SIZE", "I", 64);
    AddFloatingCommon<double, std::uint64_t>(builder, "Ljava/lang/Double;", "D", "parseDouble", "doubleToLongBits", "doubleToRawLongBits", "longBitsToDouble");
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Double;", "D"); return VmValue::Void(); });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_Character() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Character;", "Ljava/lang/Object;", {"Ljava/io/Serializable;", "Ljava/lang/Comparable;"});
    builder.InstanceField("value", "C");
    AddTypeField(builder);
    for (const auto& [name, descriptor, value] : std::array<std::tuple<const char*, const char*, int>, 58>{{
        {"MIN_VALUE","C",0},{"MAX_VALUE","C",0xffff},{"MIN_RADIX","I",2},{"MAX_RADIX","I",36},
        {"UNASSIGNED","B",0},{"UPPERCASE_LETTER","B",1},{"LOWERCASE_LETTER","B",2},{"TITLECASE_LETTER","B",3},{"MODIFIER_LETTER","B",4},{"OTHER_LETTER","B",5},{"NON_SPACING_MARK","B",6},{"ENCLOSING_MARK","B",7},{"COMBINING_SPACING_MARK","B",8},{"DECIMAL_DIGIT_NUMBER","B",9},{"LETTER_NUMBER","B",10},{"OTHER_NUMBER","B",11},{"SPACE_SEPARATOR","B",12},{"LINE_SEPARATOR","B",13},{"PARAGRAPH_SEPARATOR","B",14},{"CONTROL","B",15},{"FORMAT","B",16},{"PRIVATE_USE","B",18},{"SURROGATE","B",19},{"DASH_PUNCTUATION","B",20},{"START_PUNCTUATION","B",21},{"END_PUNCTUATION","B",22},{"CONNECTOR_PUNCTUATION","B",23},{"OTHER_PUNCTUATION","B",24},{"MATH_SYMBOL","B",25},{"CURRENCY_SYMBOL","B",26},{"MODIFIER_SYMBOL","B",27},{"OTHER_SYMBOL","B",28},{"INITIAL_QUOTE_PUNCTUATION","B",29},{"FINAL_QUOTE_PUNCTUATION","B",30},
        {"DIRECTIONALITY_UNDEFINED","B",-1},{"DIRECTIONALITY_LEFT_TO_RIGHT","B",0},{"DIRECTIONALITY_RIGHT_TO_LEFT","B",1},{"DIRECTIONALITY_RIGHT_TO_LEFT_ARABIC","B",2},{"DIRECTIONALITY_EUROPEAN_NUMBER","B",3},{"DIRECTIONALITY_EUROPEAN_NUMBER_SEPARATOR","B",4},{"DIRECTIONALITY_EUROPEAN_NUMBER_TERMINATOR","B",5},{"DIRECTIONALITY_ARABIC_NUMBER","B",6},{"DIRECTIONALITY_COMMON_NUMBER_SEPARATOR","B",7},{"DIRECTIONALITY_NONSPACING_MARK","B",8},{"DIRECTIONALITY_BOUNDARY_NEUTRAL","B",9},{"DIRECTIONALITY_PARAGRAPH_SEPARATOR","B",10},{"DIRECTIONALITY_SEGMENT_SEPARATOR","B",11},{"DIRECTIONALITY_WHITESPACE","B",12},{"DIRECTIONALITY_OTHER_NEUTRALS","B",13},{"DIRECTIONALITY_LEFT_TO_RIGHT_EMBEDDING","B",14},{"DIRECTIONALITY_LEFT_TO_RIGHT_OVERRIDE","B",15},{"DIRECTIONALITY_RIGHT_TO_LEFT_EMBEDDING","B",16},{"DIRECTIONALITY_RIGHT_TO_LEFT_OVERRIDE","B",17},{"DIRECTIONALITY_POP_DIRECTIONAL_FORMAT","B",18},
        {"MIN_HIGH_SURROGATE","C",0xd800},{"MAX_HIGH_SURROGATE","C",0xdbff},{"MIN_LOW_SURROGATE","C",0xdc00},{"MAX_LOW_SURROGATE","C",0xdfff}
    }}) builder.ConstantInt(name, descriptor, value);
    builder.ConstantInt("MIN_SURROGATE","C",0xd800).ConstantInt("MAX_SURROGATE","C",0xdfff)
           .ConstantInt("MIN_SUPPLEMENTARY_CODE_POINT","I",0x10000).ConstantInt("MIN_CODE_POINT","I",0)
           .ConstantInt("MAX_CODE_POINT","I",0x10ffff).ConstantInt("SIZE","I",16);
    builder.Constructor("(C)V", [](IntrinsicContext& c) { SetBoxedBits(c, c.receiver, static_cast<std::uint16_t>(c.arguments[0].AsInt()), false); return VmValue::Void(); });
    builder.FinalMethod("charValue", "()C", [](IntrinsicContext& c) { return VmValue::Int(static_cast<std::uint16_t>(ReadBoxed(c, c.receiver, false))); });
    builder.StaticMethod("valueOf", "(C)Ljava/lang/Character;", [](IntrinsicContext& c) { return MakeBoxed(c, "Ljava/lang/Character;", static_cast<std::uint16_t>(c.arguments[0].AsInt()), false); });
    builder.FinalMethod("compareTo", "(Ljava/lang/Character;)I", [](IntrinsicContext& c) { return VmValue::Int(static_cast<int>(static_cast<std::uint16_t>(ReadBoxed(c, c.receiver, false))) - static_cast<int>(static_cast<std::uint16_t>(ReadBoxed(c, c.arguments[0].ref, false)))); });
    builder.StaticMethod("compare", "(CC)I", [](IntrinsicContext& c) { return VmValue::Int(static_cast<std::uint16_t>(c.arguments[0].AsInt()) - static_cast<std::uint16_t>(c.arguments[1].AsInt())); });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z", [](IntrinsicContext& c) { return Bool(IsExactClass(c, c.arguments[0].ref, "Ljava/lang/Character;") && ReadBoxed(c, c.receiver, false) == ReadBoxed(c, c.arguments[0].ref, false)); });
    builder.FinalMethod("hashCode", "()I", [](IntrinsicContext& c) { return VmValue::Int(static_cast<std::uint16_t>(ReadBoxed(c, c.receiver, false))); });
    builder.FinalMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& c) { return Make(c, std::u16string(1, static_cast<char16_t>(ReadBoxed(c, c.receiver, false)))); });
    builder.StaticMethod("toString", "(C)Ljava/lang/String;", [](IntrinsicContext& c) { return Make(c, std::u16string(1, static_cast<char16_t>(c.arguments[0].AsInt()))); });
    const auto digit = [](std::int32_t cp, std::int32_t radix) { const auto value = cp < 128 ? DigitValue(static_cast<char>(cp)) : -1; return radix >= 2 && radix <= 36 && value < radix ? value : -1; };
    builder.StaticMethod("digit", "(CI)I", [digit](IntrinsicContext& c) { return VmValue::Int(digit(static_cast<std::uint16_t>(c.arguments[0].AsInt()), c.arguments[1].AsInt())); });
    builder.StaticMethod("digit", "(II)I", [digit](IntrinsicContext& c) { return VmValue::Int(digit(c.arguments[0].AsInt(), c.arguments[1].AsInt())); });
    builder.StaticMethod("forDigit", "(II)C", [](IntrinsicContext& c) { const auto d=c.arguments[0].AsInt(), r=c.arguments[1].AsInt(); return VmValue::Int(r>=2&&r<=36&&d>=0&&d<r ? kDigits[d] : 0); });
    const auto add_predicate = [&builder](const char* name, auto predicate) { builder.StaticMethod(name, "(C)Z", [predicate](IntrinsicContext& c){return Bool(predicate(static_cast<std::uint16_t>(c.arguments[0].AsInt())));}); builder.StaticMethod(name, "(I)Z", [predicate](IntrinsicContext& c){return Bool(predicate(c.arguments[0].AsInt()));}); };
    add_predicate("isDigit", [](int cp){return cp>='0'&&cp<='9';});
    add_predicate("isLetter", [](int cp){return (cp>='A'&&cp<='Z')||(cp>='a'&&cp<='z');});
    add_predicate("isLetterOrDigit", [](int cp){return (cp>='0'&&cp<='9')||(cp>='A'&&cp<='Z')||(cp>='a'&&cp<='z');});
    add_predicate("isLowerCase", [](int cp){return cp>='a'&&cp<='z';});
    add_predicate("isUpperCase", [](int cp){return cp>='A'&&cp<='Z';});
    add_predicate("isWhitespace", [](int cp){return CharacterIsWhitespace(cp);});
    add_predicate("isSpaceChar", [](int cp){return CharacterIsSpace(cp);});
    add_predicate("isISOControl", [](int cp){return (cp>=0&&cp<=0x1f)||(cp>=0x7f&&cp<=0x9f);});
    builder.StaticMethod("isSpace", "(C)Z", [](IntrinsicContext& c){const auto cp=static_cast<std::uint16_t>(c.arguments[0].AsInt()); return Bool(cp=='\n'||cp=='\t'||cp=='\f'||cp=='\r'||cp==' ');});
    builder.StaticMethod("toLowerCase", "(C)C", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt(); return VmValue::Int(cp>='A'&&cp<='Z'?cp+32:cp);});
    builder.StaticMethod("toLowerCase", "(I)I", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt(); return VmValue::Int(cp>='A'&&cp<='Z'?cp+32:cp);});
    builder.StaticMethod("toUpperCase", "(C)C", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt(); return VmValue::Int(cp>='a'&&cp<='z'?cp-32:cp);});
    builder.StaticMethod("toUpperCase", "(I)I", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt(); return VmValue::Int(cp>='a'&&cp<='z'?cp-32:cp);});
    builder.StaticMethod("isHighSurrogate", "(C)Z", [](IntrinsicContext& c){const auto cp=static_cast<std::uint16_t>(c.arguments[0].AsInt());return Bool(cp>=0xd800&&cp<=0xdbff);});
    builder.StaticMethod("isLowSurrogate", "(C)Z", [](IntrinsicContext& c){const auto cp=static_cast<std::uint16_t>(c.arguments[0].AsInt());return Bool(cp>=0xdc00&&cp<=0xdfff);});
    builder.StaticMethod("isSurrogatePair", "(CC)Z", [](IntrinsicContext& c){const auto h=static_cast<std::uint16_t>(c.arguments[0].AsInt()),l=static_cast<std::uint16_t>(c.arguments[1].AsInt());return Bool(h>=0xd800&&h<=0xdbff&&l>=0xdc00&&l<=0xdfff);});
    builder.StaticMethod("isValidCodePoint", "(I)Z", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt();return Bool(cp>=0&&cp<=0x10ffff);});
    builder.StaticMethod("isBmpCodePoint", "(I)Z", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt();return Bool(cp>=0&&cp<=0xffff);});
    builder.StaticMethod("isSupplementaryCodePoint", "(I)Z", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt();return Bool(cp>=0x10000&&cp<=0x10ffff);});
    builder.StaticMethod("charCount", "(I)I", [](IntrinsicContext& c){return VmValue::Int(c.arguments[0].AsInt()>=0x10000?2:1);});
    builder.StaticMethod("toCodePoint", "(CC)I", [](IntrinsicContext& c){const auto h=static_cast<std::uint16_t>(c.arguments[0].AsInt()),l=static_cast<std::uint16_t>(c.arguments[1].AsInt());return VmValue::Int(((h-0xd800)<<10)+(l-0xdc00)+0x10000);});
    builder.StaticMethod("highSurrogate", "(I)C", [](IntrinsicContext& c){return VmValue::Int(static_cast<std::uint16_t>(((c.arguments[0].AsInt()-0x10000)>>10)+0xd800));});
    builder.StaticMethod("lowSurrogate", "(I)C", [](IntrinsicContext& c){return VmValue::Int(static_cast<std::uint16_t>(((c.arguments[0].AsInt()-0x10000)&0x3ff)+0xdc00));});
    builder.StaticMethod("reverseBytes", "(C)C", [](IntrinsicContext& c){return VmValue::Int(ByteSwap(static_cast<std::uint16_t>(c.arguments[0].AsInt())));});
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Character;", "C"); return VmValue::Void(); });
    return std::move(builder).Build();
}

}  // namespace

void AppendJavaLangPrimitiveWrappers(
    std::vector<IntrinsicClassDecl>& catalog) {
    catalog.push_back(Declare_java_lang_Number());
    catalog.push_back(Declare_java_lang_Byte());
    catalog.push_back(Declare_java_lang_Short());
    catalog.push_back(Declare_java_lang_Integer());
    catalog.push_back(Declare_java_lang_Long());
    catalog.push_back(Declare_java_lang_Float());
    catalog.push_back(Declare_java_lang_Double());
    catalog.push_back(Declare_java_lang_Boolean());
    catalog.push_back(Declare_java_lang_Character());
}

}  // namespace ogplay::runtime::dexvm::intrinsics

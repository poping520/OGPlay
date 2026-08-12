// java.lang.String / StringBuilder intrinsic handlers (P1 batch, split out
// of intrinsics_java.cpp for the 800-line module rule). Pure-library
// behaviour follows the class-library documentation (07 §2); byte<->char
// conversion uses the platform default charset (UTF-8) with REPLACE
// semantics, matching `new String(byte[])` / `String.getBytes()` on device.

#include <cstddef>
#include <regex>
#include <string>

#include "interpreter_internal.h"

namespace ogplay::runtime::dexvm {
namespace {

[[nodiscard]] std::u16string Value(IntrinsicContext& context,
                                   const VmObjectRef ref) {
    if (!ref.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "null string argument"};
    }
    return context.vm.Model().StringValue(ref);
}

[[nodiscard]] VmValue Make(IntrinsicContext& context,
                           const std::u16string& value) {
    return VmValue::Ref(context.vm.Model().NewString(value));
}

[[nodiscard]] std::u16string Widen(const std::string& value) {
    return std::u16string(value.begin(), value.end());
}

[[nodiscard]] char16_t AsciiLower(const char16_t unit) {
    return unit >= u'A' && unit <= u'Z' ? static_cast<char16_t>(unit + 32)
                                        : unit;
}

[[nodiscard]] char16_t AsciiUpper(const char16_t unit) {
    return unit >= u'a' && unit <= u'z' ? static_cast<char16_t>(unit - 32)
                                        : unit;
}

[[nodiscard]] std::int32_t CompareStrings(const std::u16string& lhs,
                                          const std::u16string& rhs,
                                          const bool ignore_case) {
    const auto size = std::min(lhs.size(), rhs.size());
    for (std::size_t index = 0; index < size; ++index) {
        auto left = lhs[index];
        auto right = rhs[index];
        if (ignore_case) {
            left = AsciiLower(left);
            right = AsciiLower(right);
        }
        if (left != right) {
            return static_cast<std::int32_t>(left) -
                   static_cast<std::int32_t>(right);
        }
    }
    return static_cast<std::int32_t>(lhs.size()) -
           static_cast<std::int32_t>(rhs.size());
}

// Platform default charset decode (UTF-8, CodingErrorAction.REPLACE):
// malformed sequences become U+FFFD instead of failing, matching what
// `new String(byte[])` does on device.
[[nodiscard]] std::u16string Utf8DecodeReplace(
    const std::span<const std::byte> bytes) {
    std::u16string units;
    units.reserve(bytes.size());
    std::size_t index = 0;
    const auto continuation = [&](const std::size_t offset) {
        return offset < bytes.size() &&
               (static_cast<std::uint8_t>(bytes[offset]) & 0xc0U) == 0x80U;
    };
    while (index < bytes.size()) {
        const auto lead = static_cast<std::uint8_t>(bytes[index]);
        if (lead < 0x80U) {
            units.push_back(lead);
            index += 1;
        } else if ((lead & 0xe0U) == 0xc0U && continuation(index + 1)) {
            units.push_back(static_cast<char16_t>(
                ((lead & 0x1fU) << 6U) |
                (static_cast<std::uint8_t>(bytes[index + 1]) & 0x3fU)));
            index += 2;
        } else if ((lead & 0xf0U) == 0xe0U && continuation(index + 1) &&
                   continuation(index + 2)) {
            units.push_back(static_cast<char16_t>(
                ((lead & 0x0fU) << 12U) |
                ((static_cast<std::uint8_t>(bytes[index + 1]) & 0x3fU)
                 << 6U) |
                (static_cast<std::uint8_t>(bytes[index + 2]) & 0x3fU)));
            index += 3;
        } else if ((lead & 0xf8U) == 0xf0U && continuation(index + 1) &&
                   continuation(index + 2) && continuation(index + 3)) {
            const auto code_point =
                ((lead & 0x07U) << 18U) |
                ((static_cast<std::uint8_t>(bytes[index + 1]) & 0x3fU)
                 << 12U) |
                ((static_cast<std::uint8_t>(bytes[index + 2]) & 0x3fU)
                 << 6U) |
                (static_cast<std::uint8_t>(bytes[index + 3]) & 0x3fU);
            if (code_point >= 0x10000U && code_point <= 0x10ffffU) {
                const auto offset = code_point - 0x10000U;
                units.push_back(
                    static_cast<char16_t>(0xd800U | (offset >> 10U)));
                units.push_back(
                    static_cast<char16_t>(0xdc00U | (offset & 0x3ffU)));
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

[[nodiscard]] std::vector<std::byte> Utf8Encode(const std::u16string& value) {
    std::vector<std::byte> out;
    out.reserve(value.size());
    const auto push = [&](const std::uint32_t byte) {
        out.push_back(static_cast<std::byte>(byte & 0xffU));
    };
    std::size_t index = 0;
    while (index < value.size()) {
        std::uint32_t code_point = value[index];
        if (code_point >= 0xd800U && code_point <= 0xdbffU &&
            index + 1 < value.size() && value[index + 1] >= 0xdc00U &&
            value[index + 1] <= 0xdfffU) {
            code_point = 0x10000U + ((code_point - 0xd800U) << 10U) +
                         (value[index + 1] - 0xdc00U);
            index += 2;
        } else {
            if (code_point >= 0xd800U && code_point <= 0xdfffU) {
                // Unpaired surrogate: REPLACE semantics emit '?'.
                code_point = '?';
            }
            index += 1;
        }
        if (code_point < 0x80U) {
            push(code_point);
        } else if (code_point < 0x800U) {
            push(0xc0U | (code_point >> 6U));
            push(0x80U | (code_point & 0x3fU));
        } else if (code_point < 0x10000U) {
            push(0xe0U | (code_point >> 12U));
            push(0x80U | ((code_point >> 6U) & 0x3fU));
            push(0x80U | (code_point & 0x3fU));
        } else {
            push(0xf0U | (code_point >> 18U));
            push(0x80U | ((code_point >> 12U) & 0x3fU));
            push(0x80U | ((code_point >> 6U) & 0x3fU));
            push(0x80U | (code_point & 0x3fU));
        }
    }
    return out;
}

// UTF-8 narrow form for regex evaluation (std::regex has no char16_t
// specialisation; patterns games use are ASCII so the round-trip is exact).
[[nodiscard]] std::string ToUtf8(const std::u16string& value) {
    const auto bytes = Utf8Encode(value);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::u16string FromUtf8(const std::string& value) {
    return Utf8DecodeReplace(std::as_bytes(std::span(value)));
}

// Compiles a Java regex with the ECMAScript engine. The subset titles use
// (literals, classes, quantifiers, alternation) is grammar-compatible;
// invalid patterns throw the documented PatternSyntaxException.
[[nodiscard]] std::regex CompileRegex(const std::u16string& pattern) {
    try {
        return std::regex(ToUtf8(pattern));
    } catch (const std::regex_error& error) {
        throw VmJavaThrow{"Ljava/util/regex/PatternSyntaxException;",
                          std::string("invalid pattern: ") + error.what()};
    }
}

// Shared range check for String(byte[]/char[], offset, count) forms.
void CheckRegion(const JniSize array_length, const std::int32_t offset,
                 const std::int32_t count) {
    if (offset < 0 || count < 0 ||
        offset > static_cast<std::int64_t>(array_length) - count) {
        throw VmJavaThrow{
            "Ljava/lang/StringIndexOutOfBoundsException;",
            "offset " + std::to_string(offset) + ", count " +
                std::to_string(count) + ", length " +
                std::to_string(array_length)};
    }
}

[[nodiscard]] VmObjectRef RequireArray(const VmObjectRef ref) {
    if (!ref.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "null array argument"};
    }
    return ref;
}

void RegisterStringConstructorHandlers(IntrinsicRegistry& registry) {
    registry.Register("core.string.init_empty",
                      [](IntrinsicContext& context) {
        context.vm.Model().BindString(context.receiver, {});
        return VmValue::Void();
    });
    registry.Register("core.string.init_copy",
                      [](IntrinsicContext& context) {
        context.vm.Model().BindString(
            context.receiver, Value(context, context.arguments[0].ref));
        return VmValue::Void();
    });
    registry.Register("core.string.init_bytes",
                      [](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto array =
            RequireArray(context.arguments[0].ref);
        const auto bytes =
            model.ReadByteRegion(array, 0, model.ArrayLength(array));
        model.BindString(context.receiver, Utf8DecodeReplace(bytes));
        return VmValue::Void();
    });
    registry.Register("core.string.init_bytes_range",
                      [](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto array =
            RequireArray(context.arguments[0].ref);
        const auto offset = context.arguments[1].AsInt();
        const auto count = context.arguments[2].AsInt();
        CheckRegion(model.ArrayLength(array), offset, count);
        const auto bytes = model.ReadByteRegion(array, offset, count);
        model.BindString(context.receiver, Utf8DecodeReplace(bytes));
        return VmValue::Void();
    });
    const auto chars_value = [](IntrinsicContext& context,
                                const VmObjectRef array,
                                const std::int32_t offset,
                                const std::int32_t count) {
        auto& model = context.vm.Model();
        std::u16string units;
        units.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index = 0; index < count; ++index) {
            units.push_back(static_cast<char16_t>(
                model.GetPrimitiveElement(array, offset + index)));
        }
        return units;
    };
    registry.Register("core.string.init_bytes_charset",
                      [](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto array = RequireArray(context.arguments[0].ref);
        auto charset = Value(context, context.arguments[1].ref);
        for (auto& unit : charset) unit = AsciiUpper(unit);
        const auto bytes =
            model.ReadByteRegion(array, 0, model.ArrayLength(array));
        std::u16string decoded;
        if (charset == u"UTF-8" || charset == u"UTF8") {
            decoded = Utf8DecodeReplace(bytes);
        } else if (charset == u"ISO-8859-1" || charset == u"LATIN1" ||
                   charset == u"ISO8859_1") {
            decoded.reserve(bytes.size());
            for (const auto byte : bytes) {
                decoded.push_back(static_cast<std::uint8_t>(byte));
            }
        } else if (charset == u"US-ASCII" || charset == u"ASCII") {
            decoded.reserve(bytes.size());
            for (const auto byte : bytes) {
                const auto unit = static_cast<std::uint8_t>(byte);
                decoded.push_back(unit < 0x80U ? unit : u'\ufffd');
            }
        } else {
            throw VmJavaThrow{
                "Ljava/io/UnsupportedEncodingException;",
                "charset is not provided: " + ToUtf8(charset)};
        }
        model.BindString(context.receiver, decoded);
        return VmValue::Void();
    });
    registry.Register("core.string.init_chars",
                      [chars_value](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto array =
            RequireArray(context.arguments[0].ref);
        context.vm.Model().BindString(
            context.receiver,
            chars_value(context, array, 0, model.ArrayLength(array)));
        return VmValue::Void();
    });
    registry.Register("core.string.init_chars_range",
                      [chars_value](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto array =
            RequireArray(context.arguments[0].ref);
        const auto offset = context.arguments[1].AsInt();
        const auto count = context.arguments[2].AsInt();
        CheckRegion(model.ArrayLength(array), offset, count);
        model.BindString(context.receiver,
                         chars_value(context, array, offset, count));
        return VmValue::Void();
    });
    registry.Register("core.string.get_bytes",
                      [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto bytes =
            Utf8Encode(Value(context, context.receiver));
        const auto array_class = vm.Linker().ResolveDescriptor("[B");
        const auto array = vm.Model().NewPrimitiveArray(
            array_class, JniPrimitiveKind::byte,
            static_cast<JniSize>(bytes.size()));
        if (!bytes.empty()) {
            vm.Model().WriteByteRegion(array, 0, bytes);
        }
        return VmValue::Ref(array);
    });
}

void RegisterStringExtendedHandlers(IntrinsicRegistry& registry) {
    registry.Register("core.string.index_of_char_from",
                      [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto from = context.arguments[1].AsInt();
        const auto start = from < 0 ? 0 : static_cast<std::size_t>(from);
        if (start >= haystack.size()) return VmValue::Int(-1);
        const auto found = haystack.find(
            static_cast<char16_t>(context.arguments[0].cat1 & 0xffffU),
            start);
        return VmValue::Int(found == std::u16string::npos
                                ? -1
                                : static_cast<std::int32_t>(found));
    });
    registry.Register("core.string.contains",
                      [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto needle = Value(context, context.arguments[0].ref);
        return VmValue::Int(
            haystack.find(needle) != std::u16string::npos ? 1 : 0);
    });
    registry.Register("core.string.get_chars",
                      [](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto value = Value(context, context.receiver);
        const auto src_begin = context.arguments[0].AsInt();
        const auto src_end = context.arguments[1].AsInt();
        const auto target = RequireArray(context.arguments[2].ref);
        const auto dst_begin = context.arguments[3].AsInt();
        if (src_begin < 0 || src_begin > src_end ||
            static_cast<std::size_t>(src_end) > value.size()) {
            throw VmJavaThrow{
                "Ljava/lang/StringIndexOutOfBoundsException;",
                "getChars source range is invalid"};
        }
        const auto count = src_end - src_begin;
        if (dst_begin < 0 ||
            static_cast<std::int64_t>(dst_begin) + count >
                model.ArrayLength(target)) {
            throw VmJavaThrow{
                "Ljava/lang/ArrayIndexOutOfBoundsException;",
                "getChars destination range is invalid"};
        }
        for (std::int32_t index = 0; index < count; ++index) {
            model.SetPrimitiveElement(
                target, dst_begin + index,
                value[static_cast<std::size_t>(src_begin + index)]);
        }
        return VmValue::Void();
    });
    registry.Register("core.string.to_char_array",
                      [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto value = Value(context, context.receiver);
        const auto array_class = vm.Linker().ResolveDescriptor("[C");
        const auto array = vm.Model().NewPrimitiveArray(
            array_class, JniPrimitiveKind::character,
            static_cast<JniSize>(value.size()));
        for (std::size_t index = 0; index < value.size(); ++index) {
            vm.Model().SetPrimitiveElement(
                array, static_cast<JniSize>(index), value[index]);
        }
        return VmValue::Ref(array);
    });
    registry.Register("core.string.replace_seq",
                      [](IntrinsicContext& context) {
        const auto value = Value(context, context.receiver);
        const auto target = Value(context, context.arguments[0].ref);
        const auto replacement = Value(context, context.arguments[1].ref);
        std::u16string out;
        if (target.empty()) {
            // Empty target: the replacement goes between every char.
            out = replacement;
            for (const auto unit : value) {
                out.push_back(unit);
                out += replacement;
            }
        } else {
            std::size_t cursor = 0;
            while (cursor <= value.size()) {
                const auto found = value.find(target, cursor);
                if (found == std::u16string::npos) {
                    out.append(value, cursor, value.size() - cursor);
                    break;
                }
                out.append(value, cursor, found - cursor);
                out += replacement;
                cursor = found + target.size();
            }
        }
        return Make(context, out);
    });
    registry.Register("core.string.replace_all",
                      [](IntrinsicContext& context) {
        const auto value = ToUtf8(Value(context, context.receiver));
        const auto pattern =
            CompileRegex(Value(context, context.arguments[0].ref));
        const auto replacement =
            ToUtf8(Value(context, context.arguments[1].ref));
        return Make(context,
                    FromUtf8(std::regex_replace(value, pattern,
                                                replacement)));
    });
    registry.Register("core.string.split", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto value = ToUtf8(Value(context, context.receiver));
        const auto pattern =
            CompileRegex(Value(context, context.arguments[0].ref));
        // Java limit-0 semantics: split on every match, then drop trailing
        // empty segments.
        std::vector<std::string> segments;
        std::size_t cursor = 0;
        std::smatch match;
        auto remaining = value;
        while (std::regex_search(remaining, match, pattern) &&
               !(match.position(0) == 0 && match.length(0) == 0)) {
            if (match.length(0) == 0) {
                // Zero-width match: split after the next character.
                segments.push_back(
                    remaining.substr(0, match.position(0) + 1));
                remaining = remaining.substr(match.position(0) + 1);
            } else {
                segments.push_back(remaining.substr(0, match.position(0)));
                remaining = remaining.substr(match.position(0) +
                                             match.length(0));
            }
            cursor += 1;
            if (cursor > value.size()) break;  // defensive progress bound
        }
        segments.push_back(remaining);
        while (!segments.empty() && segments.back().empty()) {
            segments.pop_back();
        }
        const auto array_class =
            vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
        const auto element_class =
            vm.Linker().ResolveDescriptor("Ljava/lang/String;");
        const auto array = vm.Model().NewObjectArray(
            array_class, element_class,
            static_cast<JniSize>(segments.size()));
        for (std::size_t index = 0; index < segments.size(); ++index) {
            vm.Model().SetObjectElement(
                array, static_cast<JniSize>(index),
                vm.Model().NewString(FromUtf8(segments[index])));
        }
        return VmValue::Ref(array);
    });
}

void RegisterStringHandlers(IntrinsicRegistry& registry) {
    RegisterStringConstructorHandlers(registry);
    RegisterStringExtendedHandlers(registry);
    registry.Register("core.string.equals_ignore_case",
                      [](IntrinsicContext& context) {
        const auto other = context.arguments[0].ref;
        if (!other.IsValid()) return VmValue::Int(0);
        return VmValue::Int(
            CompareStrings(Value(context, context.receiver),
                           Value(context, other), true) == 0
                ? 1
                : 0);
    });
    registry.Register("core.string.compare_to",
                      [](IntrinsicContext& context) {
        return VmValue::Int(CompareStrings(
            Value(context, context.receiver),
            Value(context, context.arguments[0].ref), false));
    });
    registry.Register("core.string.compare_to_ignore_case",
                      [](IntrinsicContext& context) {
        return VmValue::Int(CompareStrings(
            Value(context, context.receiver),
            Value(context, context.arguments[0].ref), true));
    });
    registry.Register("core.string.concat", [](IntrinsicContext& context) {
        return Make(context, Value(context, context.receiver) +
                                 Value(context, context.arguments[0].ref));
    });
    registry.Register("core.string.starts_with",
                      [](IntrinsicContext& context) {
        return VmValue::Int(
            Value(context, context.receiver)
                    .starts_with(Value(context, context.arguments[0].ref))
                ? 1
                : 0);
    });
    registry.Register("core.string.ends_with",
                      [](IntrinsicContext& context) {
        return VmValue::Int(
            Value(context, context.receiver)
                    .ends_with(Value(context, context.arguments[0].ref))
                ? 1
                : 0);
    });
    registry.Register("core.string.index_of_char",
                      [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto found = haystack.find(static_cast<char16_t>(
            context.arguments[0].cat1 & 0xffffU));
        return VmValue::Int(found == std::u16string::npos
                                ? -1
                                : static_cast<std::int32_t>(found));
    });
    registry.Register("core.string.index_of_string",
                      [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto found =
            haystack.find(Value(context, context.arguments[0].ref));
        return VmValue::Int(found == std::u16string::npos
                                ? -1
                                : static_cast<std::int32_t>(found));
    });
    registry.Register("core.string.last_index_of_char",
                      [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto found = haystack.rfind(static_cast<char16_t>(
            context.arguments[0].cat1 & 0xffffU));
        return VmValue::Int(found == std::u16string::npos
                                ? -1
                                : static_cast<std::int32_t>(found));
    });
    registry.Register("core.string.last_index_of_string",
                      [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto found =
            haystack.rfind(Value(context, context.arguments[0].ref));
        return VmValue::Int(found == std::u16string::npos
                                ? -1
                                : static_cast<std::int32_t>(found));
    });
    const auto substring = [](IntrinsicContext& context,
                              const std::int32_t begin,
                              const std::int32_t end) {
        const auto value = Value(context, context.receiver);
        if (begin < 0 || end < begin ||
            static_cast<std::size_t>(end) > value.size()) {
            throw VmJavaThrow{
                "Ljava/lang/StringIndexOutOfBoundsException;",
                "begin " + std::to_string(begin) + ", end " +
                    std::to_string(end) + ", length " +
                    std::to_string(value.size())};
        }
        return Make(context, value.substr(
                                 static_cast<std::size_t>(begin),
                                 static_cast<std::size_t>(end - begin)));
    };
    registry.Register("core.string.substring_from",
                      [substring](IntrinsicContext& context) {
        const auto value = Value(context, context.receiver);
        return substring(context, context.arguments[0].AsInt(),
                         static_cast<std::int32_t>(value.size()));
    });
    registry.Register("core.string.substring_range",
                      [substring](IntrinsicContext& context) {
        return substring(context, context.arguments[0].AsInt(),
                         context.arguments[1].AsInt());
    });
    registry.Register("core.string.to_lower", [](IntrinsicContext& context) {
        auto value = Value(context, context.receiver);
        for (auto& unit : value) unit = AsciiLower(unit);
        return Make(context, value);
    });
    registry.Register("core.string.to_upper", [](IntrinsicContext& context) {
        auto value = Value(context, context.receiver);
        for (auto& unit : value) unit = AsciiUpper(unit);
        return Make(context, value);
    });
    registry.Register("core.string.trim", [](IntrinsicContext& context) {
        const auto value = Value(context, context.receiver);
        std::size_t begin = 0;
        std::size_t end = value.size();
        while (begin < end && value[begin] <= u' ') ++begin;
        while (end > begin && value[end - 1] <= u' ') --end;
        return Make(context, value.substr(begin, end - begin));
    });
    registry.Register("core.string.is_empty", [](IntrinsicContext& context) {
        return VmValue::Int(
            Value(context, context.receiver).empty() ? 1 : 0);
    });
    registry.Register("core.string.value_of_int",
                      [](IntrinsicContext& context) {
        return Make(context,
                    Widen(std::to_string(context.arguments[0].AsInt())));
    });
    registry.Register("core.string.value_of_long",
                      [](IntrinsicContext& context) {
        return Make(context,
                    Widen(std::to_string(context.arguments[0].AsLong())));
    });
    registry.Register("core.string.value_of_float",
                      [](IntrinsicContext& context) {
        return Make(context,
                    Widen(std::to_string(context.arguments[0].AsFloat())));
    });
    registry.Register("core.string.value_of_double",
                      [](IntrinsicContext& context) {
        return Make(context,
                    Widen(std::to_string(context.arguments[0].AsDouble())));
    });
    registry.Register("core.string.value_of_boolean",
                      [](IntrinsicContext& context) {
        return Make(context, context.arguments[0].AsInt() != 0
                                 ? u"true"
                                 : std::u16string(u"false"));
    });
    registry.Register("core.string.value_of_char",
                      [](IntrinsicContext& context) {
        return Make(context,
                    std::u16string(1, static_cast<char16_t>(
                                          context.arguments[0].cat1 &
                                          0xffffU)));
    });
}

void RegisterBuilderHandlers(IntrinsicRegistry& registry) {
    const auto check_builder_index = [](const std::u16string& buffer,
                                        const std::int32_t index) {
        if (index < 0 || static_cast<std::size_t>(index) >= buffer.size()) {
            throw VmJavaThrow{
                "Ljava/lang/StringIndexOutOfBoundsException;",
                "builder index " + std::to_string(index) + ", length " +
                    std::to_string(buffer.size())};
        }
    };
    registry.Register("core.builder.char_at",
                      [check_builder_index](IntrinsicContext& context) {
        auto& buffer = context.vm.BuilderBuffer(context.receiver);
        const auto index = context.arguments[0].AsInt();
        check_builder_index(buffer, index);
        return VmValue::Int(buffer[static_cast<std::size_t>(index)]);
    });
    registry.Register("core.builder.set_char_at",
                      [check_builder_index](IntrinsicContext& context) {
        auto& buffer = context.vm.BuilderBuffer(context.receiver);
        const auto index = context.arguments[0].AsInt();
        check_builder_index(buffer, index);
        buffer[static_cast<std::size_t>(index)] = static_cast<char16_t>(
            context.arguments[1].cat1 & 0xffffU);
        return VmValue::Void();
    });
    registry.Register("core.builder.delete_char_at",
                      [check_builder_index](IntrinsicContext& context) {
        auto& buffer = context.vm.BuilderBuffer(context.receiver);
        const auto index = context.arguments[0].AsInt();
        check_builder_index(buffer, index);
        buffer.erase(buffer.begin() + index);
        return VmValue::Ref(context.receiver);
    });
    registry.Register("core.builder.insert_char",
                      [](IntrinsicContext& context) {
        auto& buffer = context.vm.BuilderBuffer(context.receiver);
        const auto index = context.arguments[0].AsInt();
        if (index < 0 || static_cast<std::size_t>(index) > buffer.size()) {
            throw VmJavaThrow{
                "Ljava/lang/StringIndexOutOfBoundsException;",
                "insert offset " + std::to_string(index)};
        }
        buffer.insert(buffer.begin() + index,
                      static_cast<char16_t>(context.arguments[1].cat1 &
                                            0xffffU));
        return VmValue::Ref(context.receiver);
    });
    registry.Register("core.builder.init", [](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver).clear();
        return VmValue::Void();
    });
    registry.Register("core.builder.init_capacity",
                      [](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver).clear();
        return VmValue::Void();
    });
    registry.Register("core.builder.init_string",
                      [](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) =
            Value(context, context.arguments[0].ref);
        return VmValue::Void();
    });
    const auto self = [](IntrinsicContext& context) {
        return VmValue::Ref(context.receiver);
    };
    registry.Register("core.builder.append_string",
                      [self](IntrinsicContext& context) {
        const auto argument = context.arguments[0].ref;
        context.vm.BuilderBuffer(context.receiver) +=
            argument.IsValid() ? Value(context, argument)
                               : std::u16string(u"null");
        return self(context);
    });
    registry.Register("core.builder.append_object",
                      [self](IntrinsicContext& context) {
        const auto argument = context.arguments[0].ref;
        auto& buffer = context.vm.BuilderBuffer(context.receiver);
        if (!argument.IsValid()) {
            buffer += u"null";
        } else {
            auto& model = context.vm.Model();
            const auto kind = model.Kind(argument);
            if (kind == VmObjectKind::string ||
                kind == VmObjectKind::external) {
                buffer += Value(context, argument);
            } else {
                buffer += Widen("@" + std::to_string(argument.Value()));
            }
        }
        return self(context);
    });
    registry.Register("core.builder.append_int",
                      [self](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) +=
            Widen(std::to_string(context.arguments[0].AsInt()));
        return self(context);
    });
    registry.Register("core.builder.append_long",
                      [self](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) +=
            Widen(std::to_string(context.arguments[0].AsLong()));
        return self(context);
    });
    registry.Register("core.builder.append_boolean",
                      [self](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) +=
            context.arguments[0].AsInt() != 0 ? u"true"
                                              : std::u16string(u"false");
        return self(context);
    });
    registry.Register("core.builder.append_char",
                      [self](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) += static_cast<char16_t>(
            context.arguments[0].cat1 & 0xffffU);
        return self(context);
    });
    registry.Register("core.builder.append_float",
                      [self](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) +=
            Widen(std::to_string(context.arguments[0].AsFloat()));
        return self(context);
    });
    registry.Register("core.builder.append_double",
                      [self](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) +=
            Widen(std::to_string(context.arguments[0].AsDouble()));
        return self(context);
    });
    registry.Register("core.builder.to_string",
                      [](IntrinsicContext& context) {
        return Make(context, context.vm.BuilderBuffer(context.receiver));
    });
    registry.Register("core.builder.length", [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(
            context.vm.BuilderBuffer(context.receiver).size()));
    });
}

}  // namespace

void RegisterJavaStringBuiltins(IntrinsicRegistry& registry) {
    RegisterStringHandlers(registry);
    RegisterBuilderHandlers(registry);
}

}  // namespace ogplay::runtime::dexvm

#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_String() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/String;", "Ljava/lang/Object;", {"Ljava/lang/CharSequence;", "Ljava/lang/Comparable;", "Ljava/io/Serializable;"});
    builder.Constructor("()V",
        [](IntrinsicContext& context) {
                context.vm.Model().BindString(context.receiver, {});
                return VmValue::Void();
            });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                context.vm.Model().BindString(
                    context.receiver, Value(context, context.arguments[0].ref));
                return VmValue::Void();
            });
    builder.Constructor("([B)V",
        [](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto array =
                    RequireArray(context.arguments[0].ref);
                const auto bytes =
                    model.ReadByteRegion(array, 0, model.ArrayLength(array));
                model.BindString(context.receiver, Utf8DecodeReplace(bytes));
                return VmValue::Void();
            });
    builder.Constructor("([BII)V",
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
    builder.Constructor("([BLjava/lang/String;)V",
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
    builder.Constructor("([C)V",
        [](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto array =
                    RequireArray(context.arguments[0].ref);
                context.vm.Model().BindString(
                    context.receiver,
                    CharsValue(context, array, 0, model.ArrayLength(array)));
                return VmValue::Void();
            });
    builder.Constructor("([CII)V",
        [](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto array =
                    RequireArray(context.arguments[0].ref);
                const auto offset = context.arguments[1].AsInt();
                const auto count = context.arguments[2].AsInt();
                CheckRegion(model.ArrayLength(array), offset, count);
                model.BindString(context.receiver,
                                 CharsValue(context, array, offset, count));
                return VmValue::Void();
            });
    builder.FinalMethod("getBytes", "()[B",
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
    builder.FinalMethod("length", "()I",
        [](IntrinsicContext& context) {
                return VmValue::Int(static_cast<std::int32_t>(
                    context.vm.Model().StringValue(context.receiver).size()));
            });
    builder.FinalMethod("charAt", "(I)C",
        [](IntrinsicContext& context) {
            const auto value = context.vm.Model().StringValue(context.receiver);
                const auto index = context.arguments[0].AsInt();
                if (index < 0 || static_cast<std::size_t>(index) >= value.size()) {
              throw VmJavaThrow{"Ljava/lang/StringIndexOutOfBoundsException;",
                        "index " + std::to_string(index)};
                }
                return VmValue::Int(value[static_cast<std::size_t>(index)]);
            });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
                const auto other = context.arguments[0].ref;
            if (!other.IsValid())
              return VmValue::Int(0);
                auto& model = context.vm.Model();
                const auto other_kind = model.Kind(other);
                if (other_kind != VmObjectKind::string &&
                    other_kind != VmObjectKind::external) {
                    return VmValue::Int(0);
                }
            return VmValue::Int(
                model.StringValue(context.receiver) == model.StringValue(other) ? 1
                                        : 0);
            });
    builder.FinalMethod("equalsIgnoreCase", "(Ljava/lang/String;)Z",
        [](IntrinsicContext& context) {
                const auto other = context.arguments[0].ref;
                if (!other.IsValid()) return VmValue::Int(0);
                return VmValue::Int(
                    CompareStrings(Value(context, context.receiver),
                                   Value(context, other), true) == 0
                        ? 1
                        : 0);
            });
    builder.FinalMethod("hashCode", "()I",
        [](IntrinsicContext& context) {
            return VmValue::Int(
                JavaStringHash(context.vm.Model().StringValue(context.receiver)));
            });
    builder.FinalMethod("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(context.receiver);
            });
    builder.FinalMethod("compareTo", "(Ljava/lang/String;)I",
        [](IntrinsicContext& context) {
                return VmValue::Int(CompareStrings(
                    Value(context, context.receiver),
                    Value(context, context.arguments[0].ref), false));
            });
    builder.FinalMethod("compareToIgnoreCase", "(Ljava/lang/String;)I",
        [](IntrinsicContext& context) {
                return VmValue::Int(CompareStrings(
                    Value(context, context.receiver),
                    Value(context, context.arguments[0].ref), true));
            });
    builder.FinalMethod("concat", "(Ljava/lang/String;)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context, Value(context, context.receiver) +
                                         Value(context, context.arguments[0].ref));
            });
    builder.FinalMethod("startsWith", "(Ljava/lang/String;)Z",
        [](IntrinsicContext& context) {
                return VmValue::Int(
                    Value(context, context.receiver)
                            .starts_with(Value(context, context.arguments[0].ref))
                        ? 1
                        : 0);
            });
    builder.FinalMethod("endsWith", "(Ljava/lang/String;)Z",
        [](IntrinsicContext& context) {
                return VmValue::Int(
                    Value(context, context.receiver)
                            .ends_with(Value(context, context.arguments[0].ref))
                        ? 1
                        : 0);
            });
    builder.FinalMethod("indexOf", "(I)I",
        [](IntrinsicContext& context) {
                const auto haystack = Value(context, context.receiver);
                const auto found = haystack.find(static_cast<char16_t>(
                    context.arguments[0].cat1 & 0xffffU));
                return VmValue::Int(found == std::u16string::npos
                                        ? -1
                                        : static_cast<std::int32_t>(found));
            });
    builder.FinalMethod("indexOf", "(II)I",
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
    builder.FinalMethod("contains", "(Ljava/lang/CharSequence;)Z",
        [](IntrinsicContext& context) {
                const auto haystack = Value(context, context.receiver);
                const auto needle = Value(context, context.arguments[0].ref);
                return VmValue::Int(
                    haystack.find(needle) != std::u16string::npos ? 1 : 0);
            });
    builder.FinalMethod("getChars", "(II[CI)V",
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
    builder.FinalMethod("toCharArray", "()[C",
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
    builder.FinalMethod("replace", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Ljava/lang/String;",
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
    builder.FinalMethod("replaceAll", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
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
    builder.FinalMethod("split", "(Ljava/lang/String;)[Ljava/lang/String;",
        [](IntrinsicContext& context) {
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
                    const auto position =
                        static_cast<std::string::size_type>(match.position(0));
                    const auto length =
                        static_cast<std::string::size_type>(match.length(0));
                    if (match.length(0) == 0) {
                        // Zero-width match: split after the next character.
                        segments.push_back(remaining.substr(0, position + 1));
                        remaining = remaining.substr(position + 1);
                    } else {
                        segments.push_back(remaining.substr(0, position));
                        remaining = remaining.substr(position + length);
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
    builder.FinalMethod("indexOf", "(Ljava/lang/String;)I",
        [](IntrinsicContext& context) {
                const auto haystack = Value(context, context.receiver);
                const auto found =
                    haystack.find(Value(context, context.arguments[0].ref));
                return VmValue::Int(found == std::u16string::npos
                                        ? -1
                                        : static_cast<std::int32_t>(found));
            });
    builder.FinalMethod("lastIndexOf", "(I)I",
        [](IntrinsicContext& context) {
                const auto haystack = Value(context, context.receiver);
                const auto found = haystack.rfind(static_cast<char16_t>(
                    context.arguments[0].cat1 & 0xffffU));
                return VmValue::Int(found == std::u16string::npos
                                        ? -1
                                        : static_cast<std::int32_t>(found));
            });
    builder.FinalMethod("lastIndexOf", "(Ljava/lang/String;)I",
        [](IntrinsicContext& context) {
                const auto haystack = Value(context, context.receiver);
                const auto found =
                    haystack.rfind(Value(context, context.arguments[0].ref));
                return VmValue::Int(found == std::u16string::npos
                                        ? -1
                                        : static_cast<std::int32_t>(found));
            });
    builder.FinalMethod("substring", "(I)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                const auto value = Value(context, context.receiver);
                return Substring(context, context.arguments[0].AsInt(),
                                 static_cast<std::int32_t>(value.size()));
            });
    builder.FinalMethod("substring", "(II)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Substring(context, context.arguments[0].AsInt(),
                                 context.arguments[1].AsInt());
            });
    builder.FinalMethod("toLowerCase", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                auto value = Value(context, context.receiver);
                for (auto& unit : value) unit = AsciiLower(unit);
                return Make(context, value);
            });
    builder.FinalMethod("toUpperCase", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                auto value = Value(context, context.receiver);
                for (auto& unit : value) unit = AsciiUpper(unit);
                return Make(context, value);
            });
    builder.FinalMethod("trim", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                const auto value = Value(context, context.receiver);
                std::size_t begin = 0;
                std::size_t end = value.size();
                while (begin < end && value[begin] <= u' ') ++begin;
                while (end > begin && value[end - 1] <= u' ') --end;
                return Make(context, value.substr(begin, end - begin));
            });
    builder.FinalMethod("isEmpty", "()Z",
        [](IntrinsicContext& context) {
                return VmValue::Int(
                    Value(context, context.receiver).empty() ? 1 : 0);
            });
    builder.StaticMethod("valueOf", "(I)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context,
                            Widen(std::to_string(context.arguments[0].AsInt())));
            });
    builder.StaticMethod("valueOf", "(J)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context,
                            Widen(std::to_string(context.arguments[0].AsLong())));
            });
    builder.StaticMethod("valueOf", "(F)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context,
                            Widen(std::to_string(context.arguments[0].AsFloat())));
            });
    builder.StaticMethod("valueOf", "(D)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context,
                            Widen(std::to_string(context.arguments[0].AsDouble())));
            });
    builder.StaticMethod("valueOf", "(Z)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context, context.arguments[0].AsInt() != 0
                                         ? u"true"
                                         : std::u16string(u"false"));
            });
    builder.StaticMethod("valueOf", "(C)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context,
                            std::u16string(1, static_cast<char16_t>(
                                                  context.arguments[0].cat1 &
                                                  0xffffU)));
            });
    builder.StaticMethod("valueOf", "(Ljava/lang/Object;)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                const auto argument = context.arguments[0].ref;
                if (!argument.IsValid()) {
                    return Make(context, std::u16string(u"null"));
                }
                auto& model = context.vm.Model();
                const auto kind = model.Kind(argument);
                if (kind == VmObjectKind::string || kind == VmObjectKind::external) {
                    return Make(context, Value(context, argument));
                }
                return Make(context,
                            Widen("@" + std::to_string(argument.Value())));
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

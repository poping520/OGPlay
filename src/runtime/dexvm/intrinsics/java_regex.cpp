// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_util_regex_PatternSyntaxException.cpp ----
#include "catalog.h"
#include "shared.h"

#include <regex>
#include <string>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_util_regex_PatternSyntaxException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/regex/PatternSyntaxException;", "Ljava/lang/IllegalArgumentException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- DVM-87 bounded API 19 Pattern/Matcher ----

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

struct Dvm87PatternFields final {
    IntrinsicFieldHandle expression;
    IntrinsicFieldHandle flags;
};

struct Dvm87MatcherFields final {
    IntrinsicFieldHandle pattern;
    IntrinsicFieldHandle input;
    IntrinsicFieldHandle search;
    IntrinsicFieldHandle start;
    IntrinsicFieldHandle end;
    IntrinsicFieldHandle matched;
    IntrinsicFieldHandle group;
};

[[nodiscard]] std::regex Dvm87Regex(IntrinsicContext& context,
                                    const VmObjectRef pattern) {
    const auto slots = context.vm.Model().InstanceSlots(pattern);
    const auto source = VmObjectRef(slots[0].bits);
    auto options = std::regex_constants::ECMAScript;
    if ((static_cast<std::int32_t>(slots[1].bits) & 2) != 0)
        options |= std::regex_constants::icase;
    try {
        return std::regex(context.vm.StringUtf8(source), options);
    } catch (const std::regex_error& error) {
        throw VmJavaThrow{"Ljava/util/regex/PatternSyntaxException;",
                          std::string("invalid pattern: ") + error.what()};
    }
}

[[nodiscard]] VmObjectRef Dvm87NewPattern(IntrinsicContext& context,
                                          const VmObjectRef expression,
                                          const std::int32_t flags,
                                          const Dvm87PatternFields& fields) {
    if (!expression.IsValid())
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "regex == null"};
    constexpr std::int32_t supported = 0x02;
    if ((flags & ~supported) != 0)
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "unsupported regex flags"};
    const auto pattern = context.vm.NewIntrinsicInstance("Ljava/util/regex/Pattern;");
    IntrinsicCall call(context);
    call.SetRef(fields.expression, pattern, expression);
    call.SetInt(fields.flags, pattern, flags);
    static_cast<void>(Dvm87Regex(context, pattern));
    return pattern;
}

[[nodiscard]] bool Dvm87Match(IntrinsicContext& context,
                              const Dvm87MatcherFields& matcher_fields,
                              const bool full, const bool advance) {
    IntrinsicCall call(context);
    const auto input_ref = call.GetRef(matcher_fields.input);
    const auto pattern = call.GetRef(matcher_fields.pattern);
    const auto input = context.vm.StringUtf8(input_ref);
    auto search = advance ? call.GetInt(matcher_fields.search) : 0;
    if (search < 0 || static_cast<std::size_t>(search) > input.size()) return false;
    const auto regex = Dvm87Regex(context, pattern);
    std::smatch match;
    const auto begin = input.cbegin() + search;
    const auto found = full
        ? std::regex_match(begin, input.cend(), match, regex)
        : std::regex_search(begin, input.cend(), match, regex);
    call.SetInt(matcher_fields.matched, found ? 1 : 0);
    if (!found) {
        call.SetInt(matcher_fields.start, -1);
        call.SetInt(matcher_fields.end, -1);
        call.SetRef(matcher_fields.group, VmObjectRef{});
        return false;
    }
    const auto start = search + static_cast<std::int32_t>(match.position());
    const auto end = start + static_cast<std::int32_t>(match.length());
    call.SetInt(matcher_fields.start, start);
    call.SetInt(matcher_fields.end, end);
    call.SetInt(matcher_fields.search, end == start ? end + 1 : end);
    call.SetRef(matcher_fields.group, context.vm.NewStringUtf8(match.str()));
    return true;
}

IntrinsicClassDecl Dvm87DeclarePattern(
    const Dvm87MatcherFields matcher_fields) {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/util/regex/Pattern;", "Ljava/lang/Object;",
        {"Ljava/io/Serializable;"});
    builder.ConstantInt("UNIX_LINES", "I", 0x01)
        .ConstantInt("CASE_INSENSITIVE", "I", 0x02)
        .ConstantInt("COMMENTS", "I", 0x04)
        .ConstantInt("MULTILINE", "I", 0x08)
        .ConstantInt("LITERAL", "I", 0x10)
        .ConstantInt("DOTALL", "I", 0x20)
        .ConstantInt("UNICODE_CASE", "I", 0x40);
    const Dvm87PatternFields fields{
        builder.BoundInstanceField("expression", "Ljava/lang/String;"),
        builder.BoundInstanceField("flags", "I")};
    builder.StaticMethod("compile", "(Ljava/lang/String;)Ljava/util/regex/Pattern;",
        [fields](IntrinsicContext& context) {
            return VmValue::Ref(Dvm87NewPattern(
                context, context.arguments[0].ref, 0, fields));
        });
    builder.StaticMethod("compile", "(Ljava/lang/String;I)Ljava/util/regex/Pattern;",
        [fields](IntrinsicContext& context) {
            return VmValue::Ref(Dvm87NewPattern(
                context, context.arguments[0].ref,
                context.arguments[1].AsInt(), fields));
        });
    builder.StaticMethod("matches", "(Ljava/lang/String;Ljava/lang/CharSequence;)Z",
        [fields, matcher_fields](IntrinsicContext& context) {
            const auto pattern = Dvm87NewPattern(
                context, context.arguments[0].ref, 0, fields);
            const auto matcher = context.vm.NewIntrinsicInstance("Ljava/util/regex/Matcher;");
            IntrinsicCall call(context);
            call.SetRef(matcher_fields.pattern, matcher, pattern);
            call.SetRef(matcher_fields.input, matcher,
                        call.NonNullRef(1, "input"));
            call.SetInt(matcher_fields.search, matcher, 0);
            const auto saved = context.receiver;
            context.receiver = matcher;
            const auto result = Dvm87Match(context, matcher_fields, true, false);
            context.receiver = saved;
            return VmValue::Int(result ? 1 : 0);
        });
    builder.FinalMethod("matcher", "(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",
        [matcher_fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto matcher = call.Vm().NewIntrinsicInstance("Ljava/util/regex/Matcher;");
            call.SetRef(matcher_fields.pattern, matcher, context.receiver);
            call.SetRef(matcher_fields.input, matcher, call.NonNullRef(0, "input"));
            call.SetInt(matcher_fields.search, matcher, 0);
            call.SetInt(matcher_fields.start, matcher, -1);
            call.SetInt(matcher_fields.end, matcher, -1);
            return VmValue::Ref(matcher);
        });
    builder.FinalMethod("pattern", "()Ljava/lang/String;",
        [fields](IntrinsicContext& context) {
            return VmValue::Ref(IntrinsicCall(context).GetRef(fields.expression));
        });
    builder.FinalMethod("flags", "()I", [fields](IntrinsicContext& context) {
        return VmValue::Int(IntrinsicCall(context).GetInt(fields.flags));
    });
    builder.StaticMethod("quote", "(Ljava/lang/String;)Ljava/lang/String;",
        [](IntrinsicContext& context) {
            const auto source = context.vm.StringUtf8(
                IntrinsicCall(context).NonNullRef(0, "source"));
            std::string escaped;
            for (const auto ch : source) {
                if (std::string_view(R"(\.^$|()[]{}*+?)").find(ch) !=
                    std::string_view::npos) escaped.push_back('\\');
                escaped.push_back(ch);
            }
            return VmValue::Ref(context.vm.NewStringUtf8(escaped));
        });
    return std::move(builder).Build();
}

struct Dvm87MatcherDeclaration final {
    IntrinsicClassDecl declaration;
    Dvm87MatcherFields fields;
};

Dvm87MatcherDeclaration Dvm87DeclareMatcher() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/regex/Matcher;");
    const Dvm87MatcherFields fields{
        builder.BoundInstanceField("pattern", "Ljava/util/regex/Pattern;"),
        builder.BoundInstanceField("input", "Ljava/lang/CharSequence;"),
        builder.BoundInstanceField("search", "I"),
        builder.BoundInstanceField("start", "I"),
        builder.BoundInstanceField("end", "I"),
        builder.BoundInstanceField("matched", "Z"),
        builder.BoundInstanceField("group", "Ljava/lang/String;")};
    builder.FinalMethod("matches", "()Z",
        [fields](IntrinsicContext& context) {
            return VmValue::Int(Dvm87Match(
                context, fields, true, false) ? 1 : 0);
        });
    builder.FinalMethod("find", "()Z",
        [fields](IntrinsicContext& context) {
            return VmValue::Int(Dvm87Match(
                context, fields, false, true) ? 1 : 0);
        });
    builder.FinalMethod("reset", "()Ljava/util/regex/Matcher;",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            call.SetInt(fields.search, 0);
            call.SetInt(fields.matched, 0);
            call.SetInt(fields.start, -1);
            call.SetInt(fields.end, -1);
            call.SetRef(fields.group, VmObjectRef{});
            return VmValue::Ref(context.receiver);
        });
    builder.FinalMethod("reset", "(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            call.SetRef(fields.input, call.NonNullRef(0, "input"));
            call.SetInt(fields.search, 0);
            call.SetInt(fields.matched, 0);
            return VmValue::Ref(context.receiver);
        });
    const auto require_match = [fields](IntrinsicCall& call) {
        if (call.GetInt(fields.matched) == 0)
            throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "no match available"};
    };
    builder.FinalMethod("start", "()I",
        [fields, require_match](IntrinsicContext& context) {
            IntrinsicCall call(context); require_match(call);
            return VmValue::Int(call.GetInt(fields.start));
        });
    builder.FinalMethod("end", "()I",
        [fields, require_match](IntrinsicContext& context) {
            IntrinsicCall call(context); require_match(call);
            return VmValue::Int(call.GetInt(fields.end));
        });
    builder.FinalMethod("group", "()Ljava/lang/String;",
        [fields, require_match](IntrinsicContext& context) {
            IntrinsicCall call(context); require_match(call);
            return VmValue::Ref(call.GetRef(fields.group));
        });
    const auto replace = [fields](IntrinsicContext& context,
                                                  const bool first) {
        IntrinsicCall call(context);
        const auto source = context.vm.StringUtf8(call.GetRef(fields.input));
        const auto replacement = context.vm.StringUtf8(call.NonNullRef(0, "replacement"));
        const auto regex = Dvm87Regex(context, call.GetRef(fields.pattern));
        const auto flags = first ? std::regex_constants::format_first_only
                                 : std::regex_constants::match_default;
        return VmValue::Ref(context.vm.NewStringUtf8(
            std::regex_replace(source, regex, replacement, flags)));
    };
    builder.FinalMethod("replaceAll", "(Ljava/lang/String;)Ljava/lang/String;",
        [replace](IntrinsicContext& context) { return replace(context, false); });
    builder.FinalMethod("replaceFirst", "(Ljava/lang/String;)Ljava/lang/String;",
        [replace](IntrinsicContext& context) { return replace(context, true); });
    return {std::move(builder).Build(), fields};
}

}  // namespace

void AppendJavaRegex(std::vector<IntrinsicClassDecl>& catalog) {
    auto matcher = Dvm87DeclareMatcher();
    catalog.push_back(Dvm87DeclarePattern(matcher.fields));
    catalog.push_back(std::move(matcher.declaration));
}

}  // namespace ogplay::runtime::dexvm::intrinsics

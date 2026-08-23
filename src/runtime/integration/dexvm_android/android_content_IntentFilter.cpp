#include "catalog.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>

namespace {

std::int32_t ParseJavaInt(const std::string_view text) {
    const auto fail = [&]() -> void {
        throw ogplay::runtime::dexvm::VmJavaThrow{
            "Ljava/lang/NumberFormatException;",
            "invalid IntentFilter authority port: " + std::string(text)};
    };
    if (text.empty()) fail();
    std::size_t cursor{};
    bool negative{};
    if (text[cursor] == '+' || text[cursor] == '-') {
        negative = text[cursor++] == '-';
        if (cursor == text.size()) fail();
    }
    constexpr std::uint64_t kPositiveLimit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    constexpr std::uint64_t kNegativeLimit = kPositiveLimit + 1U;
    const auto limit = negative ? kNegativeLimit : kPositiveLimit;
    std::uint64_t value{};
    for (; cursor < text.size(); ++cursor) {
        const auto ch = text[cursor];
        if (ch < '0' || ch > '9') fail();
        const auto digit = static_cast<std::uint64_t>(ch - '0');
        if (value > (limit - digit) / 10U) fail();
        value = value * 10U + digit;
    }
    const auto signed_value = negative ? -static_cast<std::int64_t>(value)
                                       : static_cast<std::int64_t>(value);
    return static_cast<std::int32_t>(signed_value);
}

}  // namespace

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_IntentFilter(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/IntentFilter;", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            context->intent_filter_schemes.erase(call.receiver.Value());
            context->intent_filter_authorities.erase(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.Constructor("()V",
        [context](dx::IntrinsicContext& call) {
            context->intent_filter_schemes.erase(call.receiver.Value());
            context->intent_filter_authorities.erase(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addAction", "(Ljava/lang/String;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("addDataScheme", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "IntentFilter data scheme is null"};
            }
            const auto scheme = call.vm.StringUtf8(call.arguments[0].ref);
            auto& schemes =
                context->intent_filter_schemes[call.receiver.Value()];
            if (std::find(schemes.begin(), schemes.end(), scheme) ==
                schemes.end()) {
                schemes.push_back(scheme);
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addDataAuthority",
        "(Ljava/lang/String;Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "IntentFilter authority host is null"};
            }
            auto host = call.vm.StringUtf8(call.arguments[0].ref);
            const auto port = call.arguments[1].ref.IsValid()
                ? ParseJavaInt(call.vm.StringUtf8(call.arguments[1].ref))
                : -1;
            const auto wildcard = !host.empty() && host.front() == '*';
            context->intent_filter_authorities[call.receiver.Value()].push_back(
                DexVmAndroidContext::IntentFilterAuthority{
                    .original_host = host,
                    .match_host = wildcard ? host.substr(1) : std::move(host),
                    .wildcard = wildcard,
                    .port = port,
                });
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

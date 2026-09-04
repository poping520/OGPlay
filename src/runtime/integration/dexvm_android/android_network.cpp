// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_net_ConnectivityManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_ConnectivityManager(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/ConnectivityManager;", "Ljava/lang/Object;");
    builder.FinalMethod("getActiveNetworkInfo", "()Landroid/net/NetworkInfo;",
        [](dx::IntrinsicContext&) {
            // Truthful offline fact: no active network (documented null).
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("getNetworkInfo", "(I)Landroid/net/NetworkInfo;",
        [](dx::IntrinsicContext&) {
            // No network of any type is connected on this platform.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_NetworkInfo_State.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_NetworkInfo_State(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/NetworkInfo$State;", "Ljava/lang/Object;");
    builder.StaticField("CONNECTED", "Landroid/net/NetworkInfo$State;");
    builder.ClassInitializer([](dx::IntrinsicContext& call) {
        call.vm.SetIntrinsicStaticRef(
            "Landroid/net/NetworkInfo$State;", "CONNECTED",
            "Landroid/net/NetworkInfo$State;",
            call.vm.NewIntrinsicInstance(
                "Landroid/net/NetworkInfo$State;"));
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_NetworkInfo.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_NetworkInfo(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/NetworkInfo;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_Uri.cpp ----
#include "catalog.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <string_view>

namespace ogplay::runtime::android_intrinsics {

namespace {

struct ParsedUri final {
    std::string scheme;
    std::string host;
    std::string path;
    std::int32_t port{-1};
};

[[nodiscard]] bool IsScheme(const std::string_view text) {
    if (text.empty() ||
        std::isalpha(static_cast<unsigned char>(text.front())) == 0) {
        return false;
    }
    return std::ranges::all_of(text.substr(1), [](const char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) != 0 ||
               ch == '+' || ch == '-' || ch == '.';
    });
}

[[nodiscard]] ParsedUri ParseUri(const std::string_view raw) {
    ParsedUri parsed;
    const auto colon = raw.find(':');
    const auto first_delimiter = raw.find_first_of("/?#");
    std::size_t hierarchy{};
    if (colon != std::string_view::npos &&
        (first_delimiter == std::string_view::npos || colon < first_delimiter) &&
        IsScheme(raw.substr(0, colon))) {
        parsed.scheme = std::string(raw.substr(0, colon));
        hierarchy = colon + 1U;
    }

    if (raw.substr(hierarchy).starts_with("//")) {
        const auto authority_begin = hierarchy + 2U;
        const auto authority_end = raw.find_first_of("/?#", authority_begin);
        auto authority = raw.substr(
            authority_begin, authority_end == std::string_view::npos
                                 ? raw.size() - authority_begin
                                 : authority_end - authority_begin);
        if (const auto user = authority.rfind('@');
            user != std::string_view::npos) {
            authority.remove_prefix(user + 1U);
        }
        if (authority.starts_with("[")) {
            if (const auto close = authority.find(']');
                close != std::string_view::npos) {
                parsed.host = std::string(authority.substr(1, close - 1U));
                if (close + 1U < authority.size() &&
                    authority[close + 1U] == ':') {
                    const auto port = authority.substr(close + 2U);
                    std::int32_t value{};
                    const auto [end, error] = std::from_chars(
                        port.data(), port.data() + port.size(), value);
                    if (error == std::errc{} &&
                        end == port.data() + port.size() && value >= 0) {
                        parsed.port = value;
                    }
                }
            }
        } else {
            const auto separator = authority.rfind(':');
            auto host = authority;
            if (separator != std::string_view::npos) {
                const auto port = authority.substr(separator + 1U);
                std::int32_t value{};
                const auto [end, error] = std::from_chars(
                    port.data(), port.data() + port.size(), value);
                if (error == std::errc{} &&
                    end == port.data() + port.size() && value >= 0) {
                    parsed.port = value;
                    host = authority.substr(0, separator);
                }
            }
            parsed.host = std::string(host);
        }
        hierarchy = authority_end == std::string_view::npos ? raw.size()
                                                             : authority_end;
    }

    if (hierarchy < raw.size() && raw[hierarchy] == '/') {
        const auto path_end = raw.find_first_of("?#", hierarchy);
        parsed.path = std::string(raw.substr(
            hierarchy, path_end == std::string_view::npos
                           ? raw.size() - hierarchy
                           : path_end - hierarchy));
    }
    return parsed;
}

}  // namespace

Decl Declare_android_net_Uri(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/Uri;", "Ljava/lang/Object;");
    const auto raw = builder.BoundInstanceField(
        "mOgplayRaw", "Ljava/lang/String;", dx::kAccPrivate);
    const auto scheme = builder.BoundInstanceField(
        "mOgplayScheme", "Ljava/lang/String;", dx::kAccPrivate);
    const auto host = builder.BoundInstanceField(
        "mOgplayHost", "Ljava/lang/String;", dx::kAccPrivate);
    const auto path = builder.BoundInstanceField(
        "mOgplayPath", "Ljava/lang/String;", dx::kAccPrivate);
    const auto port = builder.BoundInstanceField(
        "mOgplayPort", "I", dx::kAccPrivate);
    builder.StaticMethod("parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        [raw, scheme, host, path, port](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "Uri string is null"};
            }
            const auto uri = call.vm.NewIntrinsicInstance("Landroid/net/Uri;");
            const std::array uri_roots{uri};
            [[maybe_unused]] const auto roots =
                call.vm.ProtectReferences(uri_roots);
            const auto parsed = ParseUri(call.vm.StringUtf8(call.arguments[0].ref));
            dx::IntrinsicCall fields(call);
            fields.SetRef(raw, uri, call.arguments[0].ref);
            if (!parsed.scheme.empty()) {
                fields.SetRef(scheme, uri,
                              call.vm.NewStringUtf8(parsed.scheme));
            }
            if (!parsed.host.empty()) {
                fields.SetRef(host, uri, call.vm.NewStringUtf8(parsed.host));
            }
            if (!parsed.path.empty()) {
                fields.SetRef(path, uri, call.vm.NewStringUtf8(parsed.path));
            }
            fields.SetInt(port, uri, parsed.port);
            return dx::VmValue::Ref(uri);
        });
    builder.FinalMethod("getScheme", "()Ljava/lang/String;",
        [scheme](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(dx::IntrinsicCall(call).GetRef(scheme));
        });
    builder.FinalMethod("getHost", "()Ljava/lang/String;",
        [host](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(dx::IntrinsicCall(call).GetRef(host));
        });
    builder.FinalMethod("getPort", "()I",
        [port](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(dx::IntrinsicCall(call).GetInt(port));
        });
    builder.FinalMethod("getPath", "()Ljava/lang/String;",
        [path](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(dx::IntrinsicCall(call).GetRef(path));
        });
    builder.FinalOverrideMethod("toString", "()Ljava/lang/String;",
        [raw](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(dx::IntrinsicCall(call).GetRef(raw));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_wifi_WifiInfo.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiInfo(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/wifi/WifiInfo;", "Ljava/lang/Object;");
    builder.FinalMethod("getMacAddress", "()Ljava/lang/String;",
        [](dx::IntrinsicContext&) {
            // AOSP returns the connection record's stored address. OGPlay has
            // no Wi-Fi radio or connection record, so the honest value is
            // the field default: null. Never expose a host adapter address.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_wifi_WifiManager_WifiLock.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiManager_WifiLock(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/wifi/WifiManager$WifiLock;", "Ljava/lang/Object;");
    builder.FinalMethod("acquire", "()V", GraphicsNoopHandler());
    builder.FinalMethod("release", "()V", GraphicsNoopHandler());
    builder.FinalMethod("isHeld", "()Z", TelephonyFalseHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_net_wifi_WifiManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_wifi_WifiManager(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/wifi/WifiManager;", "Ljava/lang/Object;");
    builder.FinalMethod("isWifiEnabled", "()Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.FinalMethod("getWifiState", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);  // WIFI_STATE_DISABLED
    });
    builder.FinalMethod("setWifiEnabled", "(Z)Z", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);  // There is no radio to enable.
    });
    builder.FinalMethod("getConnectionInfo", "()Landroid/net/wifi/WifiInfo;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("createWifiLock", "(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(call.vm.NewIntrinsicInstance(
                "Landroid/net/wifi/WifiManager$WifiLock;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// DVM-80: API-family translation unit. DVM-86 adds stateful API 19 utility
// primitives to the same family.

#include <cmath>
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// ---- migrated from android_util_Log.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_util_Log(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/util/Log;");
    builder.MergesBootClass();
    builder.StaticMethod(
        "isLoggable", "(Ljava/lang/String;I)Z", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); },
        dx::kAccPublic | dx::kAccStatic | dx::kAccNative);
    builder.StaticMethod(
        "println_native", "(IILjava/lang/String;Ljava/lang/String;)I",
        [](dx::IntrinsicContext& call) {
            const auto buffer = call.arguments[0].AsInt(), priority = call.arguments[1].AsInt();
            if (buffer != 0 && buffer != 1 && buffer != 3) {
                if (auto* ledger = call.vm.Ledger())
                    ledger->RecordUnimplemented("android.util.Log.println_native.buffer." + std::to_string(buffer), 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "log buffer is not supported"};
            }
            if (priority < 2 || priority > 7)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "log priority is out of range"};
            if (!call.arguments[3].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "log message is null"};
            const auto tag =
                call.arguments[2].ref.IsValid() ? call.vm.StringUtf8(call.arguments[2].ref) : std::string{};
            const auto message = call.vm.StringUtf8(call.arguments[3].ref);
            if (priority == 5 && tag == "TextureView" &&
                message == "A TextureView or a subclass can only be used with hardware acceleration enabled.")
                RecordUiDegraded(call.vm, "TextureView.hardwareAcceleration");
            GuestLog(call,
                     priority >= 6   ? core::LogLevel::error
                     : priority == 5 ? core::LogLevel::warn
                     : priority == 4 ? core::LogLevel::info
                                     : core::LogLevel::debug,
                     tag + ": " + message);
            return dx::VmValue::Int(static_cast<std::int32_t>(tag.size() + message.size() + 3U));
        },
        dx::kAccPublic | dx::kAccStatic | dx::kAccNative);
    return std::move(builder).Build();
}

namespace {

constexpr std::size_t kEventPayloadMaximum = 4076 - sizeof(std::int32_t);

[[nodiscard]] std::string EventText(const std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const char c : value) {
        switch (c) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(c); break;
        }
    }
    result.push_back('"');
    return result;
}

void LogEvent(dx::IntrinsicContext& call, const std::int32_t tag, const std::string& payload) {
    GuestLog(call, core::LogLevel::info, "EventLog tag=" + std::to_string(tag) + " payload=" + payload);
}

[[nodiscard]] std::size_t EventStringSize(std::string_view value) {
    constexpr std::size_t overhead = 2 + sizeof(std::int32_t);
    const auto maximum = kEventPayloadMaximum - overhead;
    return overhead + std::min(value.size(), maximum);
}

void TruncateEventString(std::string& value) {
    constexpr std::size_t overhead = 2 + sizeof(std::int32_t);
    value.resize(std::min(value.size(), kEventPayloadMaximum - overhead));
}

}  // namespace

Decl Declare_android_util_EventLog(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/util/EventLog;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    builder.StaticMethod("writeEvent", "(II)I", [](dx::IntrinsicContext& call) {
        const auto tag = call.arguments[0].AsInt();
        const auto value = call.arguments[1].AsInt();
        LogEvent(call, tag, "int(" + std::to_string(value) + ")");
        return dx::VmValue::Int(1 + sizeof(std::int32_t));
    });
    builder.StaticMethod("writeEvent", "(IJ)I", [](dx::IntrinsicContext& call) {
        const auto tag = call.arguments[0].AsInt();
        const auto value = call.arguments[1].AsLong();
        LogEvent(call, tag, "long(" + std::to_string(value) + ")");
        return dx::VmValue::Int(1 + sizeof(std::int64_t));
    });
    builder.StaticMethod("writeEvent", "(ILjava/lang/String;)I", [](dx::IntrinsicContext& call) {
        const auto tag = call.arguments[0].AsInt();
        auto value = call.arguments[1].ref.IsValid() ? call.vm.StringUtf8(call.arguments[1].ref) : std::string("NULL");
        TruncateEventString(value);
        LogEvent(call, tag, "string(" + EventText(value) + ")");
        return dx::VmValue::Int(static_cast<std::int32_t>(EventStringSize(value)));
    });
    builder.StaticMethod("writeEvent", "(I[Ljava/lang/Object;)I", [](dx::IntrinsicContext& call) -> dx::VmValue {
        const auto tag = call.arguments[0].AsInt();
        const auto array = call.arguments[1].ref;
        if (!array.IsValid()) {
            const std::string value = "NULL";
            LogEvent(call, tag, "string(" + EventText(value) + ")");
            return dx::VmValue::Int(static_cast<std::int32_t>(EventStringSize(value)));
        }

        auto& model = call.vm.Model();
        auto size = std::size_t{2};
        std::ostringstream payload;
        payload << "list[";
        const auto count = std::min<std::size_t>(static_cast<std::size_t>(model.ArrayLength(array)), 255);
        std::size_t copied = 0;
        for (; copied < count; ++copied) {
            const auto item = model.GetObjectElement(array, static_cast<JniSize>(copied));
            std::string rendered;
            std::size_t item_size{};
            if (!item.IsValid()) {
                rendered = "string(\"NULL\")";
                item_size = 1 + sizeof(std::int32_t) + 4;
            } else {
                const auto descriptor = call.vm.Linker().Class(model.ObjectClass(item)).descriptor;
                if (descriptor == "Ljava/lang/String;") {
                    auto value = call.vm.StringUtf8(item);
                    const auto available = kEventPayloadMaximum - 1 - size;
                    if (available <= 1 + sizeof(std::int32_t)) break;
                    value.resize(std::min(value.size(), available - 1 - sizeof(std::int32_t)));
                    rendered = "string(" + EventText(value) + ")";
                    item_size = 1 + sizeof(std::int32_t) + value.size();
                } else if (descriptor == "Ljava/lang/Integer;") {
                    const auto value = CallAndroidMethod(call.vm, item, "intValue", "()I").AsInt();
                    rendered = "int(" + std::to_string(value) + ")";
                    item_size = 1 + sizeof(std::int32_t);
                } else if (descriptor == "Ljava/lang/Long;") {
                    const auto value = CallAndroidMethod(call.vm, item, "longValue", "()J").AsLong();
                    rendered = "long(" + std::to_string(value) + ")";
                    item_size = 1 + sizeof(std::int64_t);
                } else {
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Invalid payload item type"};
                }
            }
            if (size + item_size > kEventPayloadMaximum - 1) break;
            if (copied != 0) payload << ", ";
            payload << rendered;
            size += item_size;
        }
        payload << ']';
        LogEvent(call, tag, payload.str());
        return dx::VmValue::Int(static_cast<std::int32_t>(size + 1));
    });
    builder.StaticMethod("readEvents", "([ILjava/util/Collection;)V", [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("android.event_log.read", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "EventLog.readEvents requires the Android event log service"};
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

// FloatMath natives are float wrappers over libm, exactly as the API 19 JNI does.
Decl Declare_android_util_FloatMath(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/util/FloatMath;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    const auto unary = [&builder](const char* name, float (*function)(float)) {
        builder.StaticMethod(name, "(F)F", [function](dx::IntrinsicContext& call) {
            return dx::VmValue::Float(function(call.arguments[0].AsFloat()));
        });
    };
    unary("floor", [](float value) { return std::floor(value); });
    unary("ceil", [](float value) { return std::ceil(value); });
    unary("sin", [](float value) { return std::sin(value); });
    unary("cos", [](float value) { return std::cos(value); });
    unary("sqrt", [](float value) { return std::sqrt(value); });
    unary("exp", [](float value) { return std::exp(value); });
    const auto binary = [&builder](const char* name, float (*function)(float, float)) {
        builder.StaticMethod(name, "(FF)F", [function](dx::IntrinsicContext& call) {
            return dx::VmValue::Float(function(call.arguments[0].AsFloat(), call.arguments[1].AsFloat()));
        });
    };
    binary("pow", [](float x, float y) { return std::pow(x, y); });
    binary("hypot", [](float x, float y) { return std::hypot(x, y); });
    return std::move(builder).Build();
}

Decl Declare_android_util_AttributeSet(const Context& context) {
    static_cast<void>(context);
    return std::move(dx::IntrinsicClassBuilder::Interface("Landroid/util/AttributeSet;").MergesBootClass()).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

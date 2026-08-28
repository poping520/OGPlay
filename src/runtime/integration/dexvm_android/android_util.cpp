// DVM-80: API-family translation unit. DVM-86 adds stateful API 19 utility
// primitives to the same family.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ---- migrated from android_util_Log.cpp ----
#include "catalog.h"

#include "ogplay/core/encoding.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_util_Log {

Decl Declare_android_util_Log(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/util/Log;", "Ljava/lang/Object;");
    const auto log = [](const core::LogLevel level) {
        return dx::IntrinsicHandler([level](dx::IntrinsicContext& call) {
            GuestLog(call, level,
                     call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                         call.vm.StringUtf8(call.arguments[1].ref));
            return dx::VmValue::Int(0);
        });
    };
    const auto debug = log(core::LogLevel::debug);
    const auto error = log(core::LogLevel::error);
    builder.StaticMethod("d", "(Ljava/lang/String;Ljava/lang/String;)I", debug);
    builder.StaticMethod("e", "(Ljava/lang/String;Ljava/lang/String;)I", error);
    builder.StaticMethod("i", "(Ljava/lang/String;Ljava/lang/String;)I", log(core::LogLevel::info));
    builder.StaticMethod("w", "(Ljava/lang/String;Ljava/lang/String;)I", log(core::LogLevel::warn));
    builder.StaticMethod("v", "(Ljava/lang/String;Ljava/lang/String;)I", debug);
    builder.StaticMethod("e", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", error);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_util_Log(const Context& context) {
    return dvm80_android_util_Log::Declare_android_util_Log(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_util_Pair.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_util_Pair {

Decl Declare_android_util_Pair(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/util/Pair;", "Ljava/lang/Object;");
    builder.InstanceField("first", "Ljava/lang/Object;");
    builder.InstanceField("second", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/lang/Object;Ljava/lang/Object;)V",
        [](dx::IntrinsicContext& call) {
            const auto slots = call.vm.Model().InstanceSlots(call.receiver);
            slots[0] = {call.arguments[0].ref.Value(), dx::SlotTag::ref};
            slots[1] = {call.arguments[1].ref.Value(), dx::SlotTag::ref};
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_util_Pair(const Context& context) {
    return dvm80_android_util_Pair::Declare_android_util_Pair(context);
}
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
namespace {

constexpr std::int32_t kBase64NoPadding = 1;
constexpr std::int32_t kBase64NoWrap = 2;
constexpr std::int32_t kBase64CrLf = 4;
constexpr std::int32_t kBase64UrlSafe = 8;

[[nodiscard]] std::vector<std::byte> ByteWindow(
    dx::IntrinsicContext& call, const dx::VmObjectRef array,
    const std::int32_t offset, const std::int32_t length) {
    if (!array.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "Base64 input is null"};
    }
    const auto size = static_cast<std::int32_t>(call.vm.Model().ArrayLength(array));
    if (offset < 0 || length < 0 || offset > size - length) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "Base64 input range is invalid"};
    }
    return call.vm.Model().ReadByteRegion(array, offset, length);
}

[[nodiscard]] dx::VmObjectRef NewByteArray(dx::IntrinsicContext& call,
                                            const std::vector<std::byte>& data) {
    auto& model = call.vm.Model();
    const auto result = model.NewPrimitiveArray(
        call.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
        static_cast<JniSize>(data.size()));
    model.WriteByteRegion(result, 0, data);
    return result;
}

[[nodiscard]] std::string EncodeBase64(const std::vector<std::byte>& input,
                                       const std::int32_t flags) {
    return core::EncodeBase64(
        input,
        {.alphabet = (flags & kBase64UrlSafe) != 0
                         ? core::Base64Alphabet::url_safe
                         : core::Base64Alphabet::standard,
         .padding = (flags & kBase64NoPadding) == 0,
         .line_length = (flags & kBase64NoWrap) != 0 ? 0U : 76U,
         .newline = (flags & kBase64CrLf) != 0 ? "\r\n" : "\n"});
}

[[nodiscard]] std::vector<std::byte> DecodeBase64(const std::string_view input,
                                                   const std::int32_t flags) {
    auto output = core::DecodeBase64(
        input, {.alphabet = (flags & kBase64UrlSafe) != 0
                                ? core::Base64Alphabet::url_safe
                                : core::Base64Alphabet::standard});
    if (!output.has_value()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "bad base-64"};
    }
    return std::move(*output);
}

template <typename Entry>
[[nodiscard]] auto SparseLowerBound(std::vector<Entry>& entries,
                                    const std::int32_t key) {
    return std::lower_bound(entries.begin(), entries.end(), key,
        [](const Entry& entry, const std::int32_t value) {
            return entry.key < value;
        });
}

[[nodiscard]] auto IntSparseLowerBound(
    std::vector<std::pair<std::int32_t, std::int32_t>>& entries,
    const std::int32_t key) {
    return std::lower_bound(entries.begin(), entries.end(), key,
        [](const auto& entry, const std::int32_t value) {
            return entry.first < value;
        });
}

}  // namespace

Decl Declare_android_util_Base64(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/util/Base64;", "Ljava/lang/Object;");
    builder.ConstantInt("DEFAULT", "I", 0, 0x0019U)
        .ConstantInt("NO_PADDING", "I", 1, 0x0019U)
        .ConstantInt("NO_WRAP", "I", 2, 0x0019U)
        .ConstantInt("CRLF", "I", 4, 0x0019U)
        .ConstantInt("URL_SAFE", "I", 8, 0x0019U);
    builder.StaticMethod("encode", "([BI)[B", [](dx::IntrinsicContext& call) {
        const auto input = ByteWindow(call, call.arguments[0].ref, 0,
                                      call.vm.Model().ArrayLength(call.arguments[0].ref));
        const auto text = EncodeBase64(input, call.arguments[1].AsInt());
        std::vector<std::byte> bytes(text.size());
        std::transform(text.begin(), text.end(), bytes.begin(),
                       [](const char value) { return static_cast<std::byte>(value); });
        return dx::VmValue::Ref(NewByteArray(call, bytes));
    });
    builder.StaticMethod("encode", "([BIII)[B", [](dx::IntrinsicContext& call) {
        const auto input = ByteWindow(call, call.arguments[0].ref,
                                      call.arguments[1].AsInt(),
                                      call.arguments[2].AsInt());
        const auto text = EncodeBase64(input, call.arguments[3].AsInt());
        std::vector<std::byte> bytes(text.size());
        std::transform(text.begin(), text.end(), bytes.begin(),
                       [](const char value) { return static_cast<std::byte>(value); });
        return dx::VmValue::Ref(NewByteArray(call, bytes));
    });
    builder.StaticMethod("encodeToString", "([BI)Ljava/lang/String;",
        [](dx::IntrinsicContext& call) {
            const auto input = ByteWindow(call, call.arguments[0].ref, 0,
                call.vm.Model().ArrayLength(call.arguments[0].ref));
            return MakeString(call, EncodeBase64(input, call.arguments[1].AsInt()));
        });
    builder.StaticMethod("encodeToString", "([BIII)Ljava/lang/String;",
        [](dx::IntrinsicContext& call) {
            return MakeString(call, EncodeBase64(
                ByteWindow(call, call.arguments[0].ref,
                           call.arguments[1].AsInt(), call.arguments[2].AsInt()),
                call.arguments[3].AsInt()));
        });
    builder.StaticMethod("decode", "(Ljava/lang/String;I)[B",
        [](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "Base64 input is null"};
            }
            return dx::VmValue::Ref(NewByteArray(call, DecodeBase64(
                call.vm.StringUtf8(call.arguments[0].ref),
                call.arguments[1].AsInt())));
        });
    builder.StaticMethod("decode", "([BI)[B", [](dx::IntrinsicContext& call) {
        const auto bytes = ByteWindow(call, call.arguments[0].ref, 0,
            call.vm.Model().ArrayLength(call.arguments[0].ref));
        std::string text(bytes.size(), '\0');
        std::transform(bytes.begin(), bytes.end(), text.begin(),
            [](const std::byte value) { return static_cast<char>(value); });
        return dx::VmValue::Ref(NewByteArray(
            call, DecodeBase64(text, call.arguments[1].AsInt())));
    });
    builder.StaticMethod("decode", "([BIII)[B", [](dx::IntrinsicContext& call) {
        const auto bytes = ByteWindow(call, call.arguments[0].ref,
            call.arguments[1].AsInt(), call.arguments[2].AsInt());
        std::string text(bytes.size(), '\0');
        std::transform(bytes.begin(), bytes.end(), text.begin(),
            [](const std::byte value) { return static_cast<char>(value); });
        return dx::VmValue::Ref(NewByteArray(
            call, DecodeBase64(text, call.arguments[3].AsInt())));
    });
    return std::move(builder).Build();
}

Decl Declare_android_util_SparseArray(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/util/SparseArray;", "Ljava/lang/Object;", {}, 0x0001U);
    const auto init = [context](dx::IntrinsicContext& call) {
        context->sparse_arrays.try_emplace(call.receiver.Value());
        return dx::VmValue::Void();
    };
    builder.Constructor("()V", init).Constructor("(I)V", init);
    const auto get = [context](dx::IntrinsicContext& call) {
        auto& entries = context->sparse_arrays[call.receiver.Value()];
        const auto found = SparseLowerBound(entries, call.arguments[0].AsInt());
        if (found != entries.end() && found->key == call.arguments[0].AsInt())
            return dx::VmValue::Ref(found->value);
        return dx::VmValue::Ref(call.arguments.size() > 1U
                                    ? call.arguments[1].ref
                                    : dx::VmObjectRef{});
    };
    builder.FinalMethod("get", "(I)Ljava/lang/Object;", get)
        .FinalMethod("get", "(ILjava/lang/Object;)Ljava/lang/Object;", get);
    const auto put = [context](dx::IntrinsicContext& call) {
        auto& entries = context->sparse_arrays[call.receiver.Value()];
        const auto key = call.arguments[0].AsInt();
        const auto found = SparseLowerBound(entries, key);
        if (found != entries.end() && found->key == key) found->value = call.arguments[1].ref;
        else entries.insert(found, {key, call.arguments[1].ref});
        return dx::VmValue::Void();
    };
    builder.FinalMethod("put", "(ILjava/lang/Object;)V", put)
        .FinalMethod("append", "(ILjava/lang/Object;)V", put);
    const auto erase = [context](dx::IntrinsicContext& call) {
        auto& entries = context->sparse_arrays[call.receiver.Value()];
        const auto key = call.arguments[0].AsInt();
        const auto found = SparseLowerBound(entries, key);
        if (found != entries.end() && found->key == key) entries.erase(found);
        return dx::VmValue::Void();
    };
    builder.FinalMethod("delete", "(I)V", erase).FinalMethod("remove", "(I)V", erase);
    builder.FinalMethod("size", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            context->sparse_arrays[call.receiver.Value()].size()));
    });
    builder.FinalMethod("keyAt", "(I)I", [context](dx::IntrinsicContext& call) {
        const auto index = call.arguments[0].AsInt();
        const auto& entries = context->sparse_arrays[call.receiver.Value()];
        if (index < 0 || static_cast<std::size_t>(index) >= entries.size())
            throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "SparseArray index"};
        return dx::VmValue::Int(entries[static_cast<std::size_t>(index)].key);
    });
    builder.FinalMethod("valueAt", "(I)Ljava/lang/Object;", [context](dx::IntrinsicContext& call) {
        const auto index = call.arguments[0].AsInt();
        const auto& entries = context->sparse_arrays[call.receiver.Value()];
        if (index < 0 || static_cast<std::size_t>(index) >= entries.size())
            throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "SparseArray index"};
        return dx::VmValue::Ref(entries[static_cast<std::size_t>(index)].value);
    });
    builder.FinalMethod("indexOfKey", "(I)I", [context](dx::IntrinsicContext& call) {
        auto& entries = context->sparse_arrays[call.receiver.Value()];
        const auto found = SparseLowerBound(entries, call.arguments[0].AsInt());
        return dx::VmValue::Int(found != entries.end() && found->key == call.arguments[0].AsInt()
            ? static_cast<std::int32_t>(found - entries.begin()) : -1);
    });
    builder.FinalMethod("clear", "()V", [context](dx::IntrinsicContext& call) {
        context->sparse_arrays[call.receiver.Value()].clear();
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_util_SparseIntArray(const Context& context,
                                         const std::string& descriptor,
                                         const bool boolean_values) {
    auto builder = dx::IntrinsicClassBuilder::Class(descriptor, "Ljava/lang/Object;");
    const auto init = [context](dx::IntrinsicContext& call) {
        context->sparse_int_arrays.try_emplace(call.receiver.Value());
        return dx::VmValue::Void();
    };
    builder.Constructor("()V", init).Constructor("(I)V", init);
    const auto value_descriptor = boolean_values ? "Z" : "I";
    builder.FinalMethod("get", std::string("(I)") + value_descriptor,
        [context](dx::IntrinsicContext& call) {
            auto& entries = context->sparse_int_arrays[call.receiver.Value()];
            const auto found = IntSparseLowerBound(entries, call.arguments[0].AsInt());
            return dx::VmValue::Int(found != entries.end() && found->first == call.arguments[0].AsInt()
                                        ? found->second : 0);
        });
    builder.FinalMethod("get", std::string("(I") + value_descriptor + ")" + value_descriptor,
        [context](dx::IntrinsicContext& call) {
            auto& entries = context->sparse_int_arrays[call.receiver.Value()];
            const auto found = IntSparseLowerBound(entries, call.arguments[0].AsInt());
            return dx::VmValue::Int(found != entries.end() && found->first == call.arguments[0].AsInt()
                                        ? found->second : call.arguments[1].AsInt());
        });
    const auto put = [context, boolean_values](dx::IntrinsicContext& call) {
        auto& entries = context->sparse_int_arrays[call.receiver.Value()];
        const auto key = call.arguments[0].AsInt();
        const auto value = boolean_values ? (call.arguments[1].AsInt() != 0 ? 1 : 0)
                                          : call.arguments[1].AsInt();
        const auto found = IntSparseLowerBound(entries, key);
        if (found != entries.end() && found->first == key) found->second = value;
        else entries.insert(found, {key, value});
        return dx::VmValue::Void();
    };
    builder.FinalMethod("put", std::string("(I") + value_descriptor + ")V", put)
        .FinalMethod("append", std::string("(I") + value_descriptor + ")V", put);
    builder.FinalMethod("delete", "(I)V", [context](dx::IntrinsicContext& call) {
        auto& entries = context->sparse_int_arrays[call.receiver.Value()];
        const auto found = IntSparseLowerBound(entries, call.arguments[0].AsInt());
        if (found != entries.end() && found->first == call.arguments[0].AsInt()) entries.erase(found);
        return dx::VmValue::Void();
    });
    builder.FinalMethod("size", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            context->sparse_int_arrays[call.receiver.Value()].size()));
    });
    builder.FinalMethod("keyAt", "(I)I", [context](dx::IntrinsicContext& call) {
        const auto index = call.arguments[0].AsInt();
        const auto& entries = context->sparse_int_arrays[call.receiver.Value()];
        if (index < 0 || static_cast<std::size_t>(index) >= entries.size())
            throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "Sparse index"};
        return dx::VmValue::Int(entries[static_cast<std::size_t>(index)].first);
    });
    builder.FinalMethod("valueAt", std::string("(I)") + value_descriptor,
        [context](dx::IntrinsicContext& call) {
            const auto index = call.arguments[0].AsInt();
            const auto& entries = context->sparse_int_arrays[call.receiver.Value()];
            if (index < 0 || static_cast<std::size_t>(index) >= entries.size())
                throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "Sparse index"};
            return dx::VmValue::Int(entries[static_cast<std::size_t>(index)].second);
        });
    builder.FinalMethod("clear", "()V", [context](dx::IntrinsicContext& call) {
        context->sparse_int_arrays[call.receiver.Value()].clear();
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_util_SparseBooleanArray(const Context& context) {
    return Declare_android_util_SparseIntArray(
        context, "Landroid/util/SparseBooleanArray;", true);
}

Decl Declare_android_util_SparseIntArray(const Context& context) {
    return Declare_android_util_SparseIntArray(
        context, "Landroid/util/SparseIntArray;", false);
}

Decl Declare_android_util_TypedValue(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/util/TypedValue;", "Ljava/lang/Object;");
    for (const auto [name, value] : std::array{
             std::pair{"TYPE_NULL", 0}, std::pair{"TYPE_REFERENCE", 1},
             std::pair{"TYPE_ATTRIBUTE", 2}, std::pair{"TYPE_STRING", 3}, std::pair{"TYPE_FLOAT", 4},
             std::pair{"TYPE_DIMENSION", 5}, std::pair{"TYPE_INT_DEC", 16},
             std::pair{"TYPE_INT_HEX", 17}, std::pair{"TYPE_INT_BOOLEAN", 18},
             std::pair{"TYPE_FRACTION", 6}, std::pair{"TYPE_FIRST_INT", 16},
             std::pair{"TYPE_FIRST_COLOR_INT", 28}, std::pair{"TYPE_INT_COLOR_ARGB8", 28},
             std::pair{"TYPE_INT_COLOR_RGB8", 29}, std::pair{"TYPE_INT_COLOR_ARGB4", 30},
             std::pair{"TYPE_INT_COLOR_RGB4", 31}, std::pair{"TYPE_LAST_COLOR_INT", 31},
             std::pair{"TYPE_LAST_INT", 31}, std::pair{"COMPLEX_UNIT_SHIFT", 0},
             std::pair{"COMPLEX_UNIT_MASK", 15},
             std::pair{"COMPLEX_UNIT_PX", 0}, std::pair{"COMPLEX_UNIT_DIP", 1},
             std::pair{"COMPLEX_UNIT_SP", 2}, std::pair{"COMPLEX_UNIT_PT", 3},
             std::pair{"COMPLEX_UNIT_IN", 4}, std::pair{"COMPLEX_UNIT_MM", 5}}) {
        builder.ConstantInt(name, "I", value, 0x0019U);
    }
    builder.InstanceField("type", "I").InstanceField("string", "Ljava/lang/CharSequence;")
        .InstanceField("data", "I").InstanceField("assetCookie", "I")
        .InstanceField("resourceId", "I").InstanceField("changingConfigurations", "I")
        .InstanceField("density", "I");
    builder.Constructor("()V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.StaticMethod("applyDimension", "(IFLandroid/util/DisplayMetrics;)F",
        [](dx::IntrinsicContext& call) {
            const auto unit = call.arguments[0].AsInt();
            const auto value = call.arguments[1].AsFloat();
            const auto metrics = call.arguments[2].ref;
            if (!metrics.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "metrics"};
            const auto slots = call.vm.Model().InstanceSlots(metrics);
            const auto density = std::bit_cast<float>(slots[2].bits);
            const auto scaled = std::bit_cast<float>(slots[4].bits);
            const auto xdpi = std::bit_cast<float>(slots[5].bits);
            float result = value;
            if (unit == 1) result *= density;
            else if (unit == 2) result *= scaled;
            else if (unit == 3) result *= xdpi * (1.0F / 72.0F);
            else if (unit == 4) result *= xdpi;
            else if (unit == 5) result *= xdpi * (1.0F / 25.4F);
            else if (unit != 0) return dx::VmValue::Float(0.0F);
            return dx::VmValue::Float(result);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

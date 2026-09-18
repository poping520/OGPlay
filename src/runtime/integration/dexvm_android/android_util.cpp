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

namespace ogplay::runtime::android_intrinsics {
namespace {

[[nodiscard]] dx::VmObjectRef
ConstructGuestObject(dx::Interpreter &vm, const char *descriptor,
                     const char *signature,
                     std::vector<dx::VmValue> arguments = {}) {
  const auto object = vm.NewIntrinsicInstance(descriptor);
  const auto object_root = vm.ProtectReferences(std::array{object});
  const auto constructor = vm.Linker().FindDirectMethod(
      vm.Model().ObjectClass(object), "<init>", signature);
  if (!constructor) {
    throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                         "log writer constructor is not linked");
  }
  arguments.insert(arguments.begin(), dx::VmValue::Ref(object));
  const auto outcome = vm.Call(*constructor, arguments);
  if (outcome.exception.IsValid()) {
    throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor,
                          outcome.exception_message, outcome.exception};
  }
  return object;
}

[[nodiscard]] bool CauseChainContainsUnknownHost(dx::Interpreter &vm,
                                                 dx::VmObjectRef throwable) {
  const auto unknown_host =
      vm.Linker().ResolveDescriptor("Ljava/net/UnknownHostException;");
  auto current = throwable;
  while (current.IsValid()) {
    const auto current_root = vm.ProtectReferences(std::array{current});
    if (vm.Linker().IsAssignable(unknown_host,
                                 vm.Model().ObjectClass(current))) {
      return true;
    }
    current =
        CallAndroidMethod(vm, current, "getCause", "()Ljava/lang/Throwable;")
            .ref;
  }
  return false;
}

[[nodiscard]] dx::VmObjectRef FormatGuestStackTrace(dx::Interpreter &vm,
                                                    dx::VmObjectRef throwable) {
  const auto throwable_root = vm.ProtectReferences(std::array{throwable});
  const auto text = ConstructGuestObject(vm, "Ljava/io/StringWriter;", "()V");
  const auto text_root = vm.ProtectReferences(std::array{text});
  const auto writer =
      ConstructGuestObject(vm, "Ljava/io/PrintWriter;", "(Ljava/io/Writer;)V",
                           {dx::VmValue::Ref(text)});
  const auto writer_root = vm.ProtectReferences(std::array{text, writer});
  static_cast<void>(CallAndroidMethod(vm, throwable, "printStackTrace",
                                      "(Ljava/io/PrintWriter;)V",
                                      {dx::VmValue::Ref(writer)}));
  static_cast<void>(CallAndroidMethod(vm, writer, "flush", "()V"));
  return CallAndroidMethod(vm, text, "toString", "()Ljava/lang/String;").ref;
}

[[nodiscard]] dx::VmObjectRef
LogGetStackTraceString(dx::Interpreter &vm, dx::VmObjectRef throwable) {
  if (!throwable.IsValid()) {
    return vm.NewStringUtf8("");
  }
  if (CauseChainContainsUnknownHost(vm, throwable)) {
    return vm.NewStringUtf8("");
  }
  return FormatGuestStackTrace(vm, throwable);
}

} // namespace

Decl Declare_android_util_Log(const Context &context) {
  static_cast<void>(context);
  auto builder = dx::IntrinsicClassBuilder::Class("Landroid/util/Log;",
                                                  "Ljava/lang/Object;");
  const auto log = [](const core::LogLevel level) {
    return dx::IntrinsicHandler([level](dx::IntrinsicContext &call) {
      auto message = call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref);
      if (call.arguments.size() == 3) {
        const auto trace =
            LogGetStackTraceString(call.vm, call.arguments[2].ref);
        const auto trace_root = call.vm.ProtectReferences(std::array{trace});
        message += '\n' + call.vm.StringUtf8(trace);
      }
      GuestLog(call, level, message);
      return dx::VmValue::Int(0);
    });
  };
  const auto debug = log(core::LogLevel::debug);
  const auto error = log(core::LogLevel::error);
  builder.StaticMethod("d", "(Ljava/lang/String;Ljava/lang/String;)I", debug);
  builder.StaticMethod(
      "d", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I",
      debug);
  builder.StaticMethod("e", "(Ljava/lang/String;Ljava/lang/String;)I", error);
  builder.StaticMethod("i", "(Ljava/lang/String;Ljava/lang/String;)I",
                       log(core::LogLevel::info));
  builder.StaticMethod("w", "(Ljava/lang/String;Ljava/lang/String;)I",
                       log(core::LogLevel::warn));
  builder.StaticMethod(
      "w", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I",
      log(core::LogLevel::warn));
  builder.StaticMethod("v", "(Ljava/lang/String;Ljava/lang/String;)I", debug);
  builder.StaticMethod(
      "isLoggable", "(Ljava/lang/String;I)Z",
      [](dx::IntrinsicContext &) { return dx::VmValue::Int(0); });
  builder.StaticMethod(
      "e", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I",
      error);
  builder.StaticMethod("getStackTraceString",
                       "(Ljava/lang/Throwable;)Ljava/lang/String;",
                       [](dx::IntrinsicContext &call) {
                         return dx::VmValue::Ref(LogGetStackTraceString(
                             call.vm, call.arguments[0].ref));
                       });
  return std::move(builder).Build();
}

} // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_util_AttributeSet(const Context &context) {
  static_cast<void>(context);
  return std::move(dx::IntrinsicClassBuilder::Interface(
                       "Landroid/util/AttributeSet;"))
      .Build();
}

namespace {

constexpr std::int32_t kBase64NoPadding = 1;
constexpr std::int32_t kBase64NoWrap = 2;
constexpr std::int32_t kBase64CrLf = 4;
constexpr std::int32_t kBase64UrlSafe = 8;

[[nodiscard]] std::vector<std::byte> ByteWindow(dx::IntrinsicContext &call,
                                                const dx::VmObjectRef array,
                                                const std::int32_t offset,
                                                const std::int32_t length) {
  if (!array.IsValid()) {
    throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "Base64 input is null"};
  }
  const auto size =
      static_cast<std::int32_t>(call.vm.Model().ArrayLength(array));
  if (offset < 0 || length < 0 || offset > size - length) {
    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "Base64 input range is invalid"};
  }
  return call.vm.Model().ReadByteRegion(array, offset, length);
}

[[nodiscard]] dx::VmObjectRef NewByteArray(dx::IntrinsicContext &call,
                                           const std::vector<std::byte> &data) {
  auto &model = call.vm.Model();
  const auto result = model.NewPrimitiveArray(
      call.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
      static_cast<JniSize>(data.size()));
  model.WriteByteRegion(result, 0, data);
  return result;
}

[[nodiscard]] std::string EncodeBase64(const std::vector<std::byte> &input,
                                       const std::int32_t flags) {
  return core::EncodeBase64(
      input, {.alphabet = (flags & kBase64UrlSafe) != 0
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

} // namespace

Decl Declare_android_util_Base64(const Context &context) {
  static_cast<void>(context);
  auto builder = dx::IntrinsicClassBuilder::Class("Landroid/util/Base64;",
                                                  "Ljava/lang/Object;");
  constexpr auto constant_access =
      dx::kAccPublic | dx::kAccStatic | dx::kAccFinal;
  builder.ConstantInt("DEFAULT", "I", 0, constant_access)
      .ConstantInt("NO_PADDING", "I", 1, constant_access)
      .ConstantInt("NO_WRAP", "I", 2, constant_access)
      .ConstantInt("CRLF", "I", 4, constant_access)
      .ConstantInt("URL_SAFE", "I", 8, constant_access);
  builder.StaticMethod("encode", "([BI)[B", [](dx::IntrinsicContext &call) {
    const auto input =
        ByteWindow(call, call.arguments[0].ref, 0,
                   call.vm.Model().ArrayLength(call.arguments[0].ref));
    const auto text = EncodeBase64(input, call.arguments[1].AsInt());
    std::vector<std::byte> bytes(text.size());
    std::transform(
        text.begin(), text.end(), bytes.begin(),
        [](const char value) { return static_cast<std::byte>(value); });
    return dx::VmValue::Ref(NewByteArray(call, bytes));
  });
  builder.StaticMethod("encode", "([BIII)[B", [](dx::IntrinsicContext &call) {
    const auto input =
        ByteWindow(call, call.arguments[0].ref, call.arguments[1].AsInt(),
                   call.arguments[2].AsInt());
    const auto text = EncodeBase64(input, call.arguments[3].AsInt());
    std::vector<std::byte> bytes(text.size());
    std::transform(
        text.begin(), text.end(), bytes.begin(),
        [](const char value) { return static_cast<std::byte>(value); });
    return dx::VmValue::Ref(NewByteArray(call, bytes));
  });
  builder.StaticMethod(
      "encodeToString", "([BI)Ljava/lang/String;",
      [](dx::IntrinsicContext &call) {
        const auto input =
            ByteWindow(call, call.arguments[0].ref, 0,
                       call.vm.Model().ArrayLength(call.arguments[0].ref));
        return MakeString(call, EncodeBase64(input, call.arguments[1].AsInt()));
      });
  builder.StaticMethod(
      "encodeToString", "([BIII)Ljava/lang/String;",
      [](dx::IntrinsicContext &call) {
        return MakeString(call,
                          EncodeBase64(ByteWindow(call, call.arguments[0].ref,
                                                  call.arguments[1].AsInt(),
                                                  call.arguments[2].AsInt()),
                                       call.arguments[3].AsInt()));
      });
  builder.StaticMethod(
      "decode", "(Ljava/lang/String;I)[B", [](dx::IntrinsicContext &call) {
        if (!call.arguments[0].ref.IsValid()) {
          throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                "Base64 input is null"};
        }
        return dx::VmValue::Ref(NewByteArray(
            call, DecodeBase64(call.vm.StringUtf8(call.arguments[0].ref),
                               call.arguments[1].AsInt())));
      });
  builder.StaticMethod("decode", "([BI)[B", [](dx::IntrinsicContext &call) {
    const auto bytes =
        ByteWindow(call, call.arguments[0].ref, 0,
                   call.vm.Model().ArrayLength(call.arguments[0].ref));
    std::string text(bytes.size(), '\0');
    std::transform(
        bytes.begin(), bytes.end(), text.begin(),
        [](const std::byte value) { return static_cast<char>(value); });
    return dx::VmValue::Ref(
        NewByteArray(call, DecodeBase64(text, call.arguments[1].AsInt())));
  });
  builder.StaticMethod("decode", "([BIII)[B", [](dx::IntrinsicContext &call) {
    const auto bytes =
        ByteWindow(call, call.arguments[0].ref, call.arguments[1].AsInt(),
                   call.arguments[2].AsInt());
    std::string text(bytes.size(), '\0');
    std::transform(
        bytes.begin(), bytes.end(), text.begin(),
        [](const std::byte value) { return static_cast<char>(value); });
    return dx::VmValue::Ref(
        NewByteArray(call, DecodeBase64(text, call.arguments[3].AsInt())));
  });
  return std::move(builder).Build();
}

} // namespace ogplay::runtime::android_intrinsics

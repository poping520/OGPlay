// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_io_EOFException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_io_EOFException {
using namespace detail;

IntrinsicClassDecl Declare_java_io_EOFException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/io/EOFException;", "Ljava/io/IOException;");
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

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_io_EOFException() {
    return dvm80_java_io_EOFException::Declare_java_io_EOFException();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_io_FileNotFoundException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_io_FileNotFoundException {
using namespace detail;

IntrinsicClassDecl Declare_java_io_FileNotFoundException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/io/FileNotFoundException;", "Ljava/io/IOException;");
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

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_io_FileNotFoundException() {
    return dvm80_java_io_FileNotFoundException::Declare_java_io_FileNotFoundException();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_io_files.cpp ----
#include "catalog.h"
#include "shared.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_io_files {
namespace {

[[nodiscard]] std::string FilePath(IntrinsicContext &call,
                                   const VmObjectRef file) {
  const auto slots = call.vm.Model().InstanceSlots(file);
  return call.vm.StringUtf8(VmObjectRef(slots[0].bits));
}

[[noreturn]] void IoFailure(const IoRuntimeError &error) {
  throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
}

[[nodiscard]] std::string FixFileSlashes(const std::string_view path) {
  std::string fixed;
  fixed.reserve(path.size());
  bool last_was_slash{};
  for (const auto character : path) {
    if (character == '/') {
      if (!last_was_slash)
        fixed.push_back('/');
      last_was_slash = true;
    } else {
      fixed.push_back(character);
      last_was_slash = false;
    }
  }
  if (last_was_slash && fixed.size() > 1U)
    fixed.pop_back();
  return fixed;
}

[[nodiscard]] std::string ChildFilePath(
    const std::optional<std::string_view> directory,
    const std::string_view name) {
  if (!directory.has_value() || directory->empty())
    return FixFileSlashes(name);
  if (name.empty())
    return FixFileSlashes(*directory);
  std::string joined(*directory);
  if (joined.back() != '/' && name.front() != '/')
    joined.push_back('/');
  joined += name;
  return FixFileSlashes(joined);
}

void SetFilePath(IntrinsicContext& call, const std::string_view path) {
  call.vm.Model().InstanceSlots(call.receiver)[0] = {
      call.vm.NewStringUtf8(FixFileSlashes(path)).Value(), SlotTag::ref};
}

[[nodiscard]] std::optional<VmValue> InvokeVirtual(
    IntrinsicContext& context, const VmObjectRef receiver,
    const std::string_view name, const std::string_view descriptor) {
  if (!receiver.IsValid()) {
    throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                      "virtual receiver == null"};
  }
  auto& linker = context.vm.Linker();
  const auto java_class = context.vm.Model().ObjectClass(receiver);
  const auto index = linker.FindVtableIndex(
      java_class, std::string(name), std::string(descriptor));
  if (!index.has_value()) {
    throw VmJavaThrow{"Ljava/lang/AbstractMethodError;",
                      std::string(name) + std::string(descriptor)};
  }
  const std::array arguments{VmValue::Ref(receiver)};
  const auto outcome = context.vm.Call(
      linker.Class(java_class).vtable[*index], arguments);
  if (outcome.exception.IsValid()) {
    context.vm.SetPendingException(outcome.exception);
    return std::nullopt;
  }
  return outcome.value;
}

IntrinsicHandler OpenInputFromPath(const bool file_argument) {
  return [file_argument](IntrinsicContext &call) {
    const auto path = file_argument ? FilePath(call, call.arguments[0].ref)
                                    : call.vm.StringUtf8(call.arguments[0].ref);
    const auto bytes = call.vm.IO().ReadFile(path);
    if (!bytes.has_value()) {
      throw VmJavaThrow{"Ljava/io/FileNotFoundException;",
                        "file not found: " + path};
    }
    call.vm.IO().SetInput(call.receiver, {*bytes, 0, false});
    const auto descriptor =
        call.vm.NewIntrinsicInstance("Ljava/io/FileDescriptor;");
    call.vm.IO().SetDescriptor(
        descriptor,
        {IoRuntime::DescriptorKind::vfs_path, path, 0, false});
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(call.receiver), "fd",
        "Ljava/io/FileDescriptor;");
    if (!field.has_value()) {
      throw DexVmError(DexVmErrorReason::internal_invariant,
                       "FileInputStream.fd field is unavailable");
    }
    call.vm.Model().InstanceSlots(call.receiver)
        [call.vm.Linker().Field(*field).slot] = {descriptor.Value(),
                                                 SlotTag::ref};
    return VmValue::Void();
  };
}

IntrinsicHandler OpenOutputFromPath(const bool file_argument) {
  return [file_argument](IntrinsicContext &call) {
    const auto path = file_argument ? FilePath(call, call.arguments[0].ref)
                                    : call.vm.StringUtf8(call.arguments[0].ref);
    call.vm.IO().SetOutput(call.receiver, {path, {}, false});
    return VmValue::Void();
  };
}

IntrinsicHandler WriteBytes() {
  return [](IntrinsicContext &call) {
    IoRuntime::OutputState *output{};
    try {
      output = &call.vm.IO().Output(call.receiver);
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
    const auto array = call.arguments[0].ref;
    const auto bytes = call.vm.Model().ReadByteRegion(
        array, 0, call.vm.Model().ArrayLength(array));
    output->bytes.insert(output->bytes.end(), bytes.begin(), bytes.end());
    return VmValue::Void();
  };
}

IntrinsicHandler Flush(const bool close) {
  return [close](IntrinsicContext &call) {
    try {
      call.vm.IO().FlushOutput(call.receiver, close);
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
    return VmValue::Void();
  };
}

IntrinsicClassDecl DeclareFile() {
  auto builder =
      IntrinsicClassBuilder::Class("Ljava/io/File;", "Ljava/lang/Object;");
  builder.ConstantInt("separatorChar", "C", '/', 0x0019U);
  builder.ConstantString("separator", "/", 0x0019U);
  builder.ConstantInt("pathSeparatorChar", "C", ':', 0x0019U);
  builder.ConstantString("pathSeparator", ":", 0x0019U);
  builder.InstanceField("path", "Ljava/lang/String;");
  builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext &call) {
    if (!call.arguments[0].ref.IsValid()) {
      throw VmJavaThrow{"Ljava/lang/NullPointerException;", "path == null"};
    }
    SetFilePath(call, call.vm.StringUtf8(call.arguments[0].ref));
    return VmValue::Void();
  });
  builder.Constructor("(Ljava/lang/String;Ljava/lang/String;)V",
                      [](IntrinsicContext &call) {
                        if (!call.arguments[1].ref.IsValid()) {
                          throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                            "name == null"};
                        }
                        const auto name =
                            call.vm.StringUtf8(call.arguments[1].ref);
                        const auto directory = call.arguments[0].ref.IsValid()
                            ? std::optional<std::string>(
                                  call.vm.StringUtf8(call.arguments[0].ref))
                            : std::nullopt;
                        SetFilePath(call, ChildFilePath(
                            directory.has_value()
                                ? std::optional<std::string_view>(*directory)
                                : std::nullopt,
                            name));
                        return VmValue::Void();
                      });
  builder.Constructor("(Ljava/io/File;Ljava/lang/String;)V",
                      [](IntrinsicContext& call) {
                        if (!call.arguments[1].ref.IsValid()) {
                          throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                            "name == null"};
                        }
                        const auto name =
                            call.vm.StringUtf8(call.arguments[1].ref);
                        const auto directory = call.arguments[0].ref.IsValid()
                            ? std::optional<std::string>(
                                  FilePath(call, call.arguments[0].ref))
                            : std::nullopt;
                        SetFilePath(call, ChildFilePath(
                            directory.has_value()
                                ? std::optional<std::string_view>(*directory)
                                : std::nullopt,
                            name));
                        return VmValue::Void();
                      });
  builder.Constructor("(Ljava/net/URI;)V", [](IntrinsicContext& call) {
    const auto uri = call.arguments[0].ref;
    if (!uri.IsValid()) {
      throw VmJavaThrow{"Ljava/lang/NullPointerException;", "uri == null"};
    }
    const auto absolute = InvokeVirtual(call, uri, "isAbsolute", "()Z");
    if (!absolute.has_value()) return VmValue::Void();
    if (absolute->AsInt() == 0) {
      throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                        "URI is not absolute"};
    }
    const auto raw_ssp = InvokeVirtual(
        call, uri, "getRawSchemeSpecificPart", "()Ljava/lang/String;");
    if (!raw_ssp.has_value()) return VmValue::Void();
    if (!raw_ssp->ref.IsValid() ||
        !call.vm.StringUtf8(raw_ssp->ref).starts_with('/')) {
      throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                        "URI is not hierarchical"};
    }
    const auto scheme = InvokeVirtual(
        call, uri, "getScheme", "()Ljava/lang/String;");
    if (!scheme.has_value()) return VmValue::Void();
    if (!scheme->ref.IsValid() || call.vm.StringUtf8(scheme->ref) != "file") {
      throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                        "Expected file scheme in URI"};
    }
    const auto raw_path = InvokeVirtual(
        call, uri, "getRawPath", "()Ljava/lang/String;");
    if (!raw_path.has_value()) return VmValue::Void();
    if (!raw_path->ref.IsValid() ||
        call.vm.StringUtf8(raw_path->ref).empty()) {
      throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                        "Expected non-empty path in URI"};
    }
    for (const auto [name, descriptor] : {
             std::pair{"getRawAuthority", "()Ljava/lang/String;"},
             std::pair{"getRawQuery", "()Ljava/lang/String;"},
             std::pair{"getRawFragment", "()Ljava/lang/String;"}}) {
      const auto component = InvokeVirtual(call, uri, name, descriptor);
      if (!component.has_value()) return VmValue::Void();
      if (component->ref.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          std::string("Unexpected URI component: ") + name};
      }
    }
    const auto path = InvokeVirtual(
        call, uri, "getPath", "()Ljava/lang/String;");
    if (!path.has_value()) return VmValue::Void();
    SetFilePath(call, call.vm.StringUtf8(path->ref));
    return VmValue::Void();
  });
  builder.FinalMethod("exists", "()Z", [](IntrinsicContext &call) {
    return VmValue::Int(
        call.vm.IO().Stat(FilePath(call, call.receiver)).has_value());
  });
  builder.FinalMethod("length", "()J", [](IntrinsicContext &call) {
    const auto info = call.vm.IO().Stat(FilePath(call, call.receiver));
    return VmValue::Long(
        info.has_value() ? static_cast<std::int64_t>(info->size) : 0);
  });
  const auto get_path = [](IntrinsicContext &call) {
    return VmValue::Ref(call.vm.NewStringUtf8(FilePath(call, call.receiver)));
  };
  builder.FinalMethod("getPath", "()Ljava/lang/String;", get_path);
  builder.FinalMethod("getAbsolutePath", "()Ljava/lang/String;", get_path);
  const auto make_directories = [](IntrinsicContext &call) {
    return VmValue::Int(
        call.vm.IO().MakeDirectories(FilePath(call, call.receiver)));
  };
  builder.FinalMethod("mkdir", "()Z", make_directories);
  builder.FinalMethod("mkdirs", "()Z", make_directories);
  builder.FinalMethod("createNewFile", "()Z", [](IntrinsicContext &call) {
    try {
      return VmValue::Int(
          call.vm.IO().CreateFile(FilePath(call, call.receiver)));
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
  });
  builder.FinalMethod("delete", "()Z", [](IntrinsicContext &call) {
    return VmValue::Int(call.vm.IO().Delete(FilePath(call, call.receiver)));
  });
  builder.FinalMethod("isDirectory", "()Z", [](IntrinsicContext &call) {
    const auto info = call.vm.IO().Stat(FilePath(call, call.receiver));
    return VmValue::Int(info.has_value() && info->is_directory);
  });
  builder.FinalMethod(
      "list", "()[Ljava/lang/String;", [](IntrinsicContext &call) {
        const auto names = call.vm.IO().List(FilePath(call, call.receiver));
        if (!names.has_value())
          return VmValue::Ref(VmObjectRef{});
        const auto array = call.vm.Model().NewObjectArray(
            call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;"),
            call.vm.Linker().ResolveDescriptor("Ljava/lang/String;"),
            static_cast<JniSize>(names->size()));
        for (std::size_t index = 0; index < names->size(); ++index) {
          call.vm.Model().SetObjectElement(
              array, static_cast<JniSize>(index),
              call.vm.NewStringUtf8((*names)[index]));
        }
        return VmValue::Ref(array);
      });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareFileInputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/FileInputStream;",
                                              "Ljava/io/InputStream;");
  builder.InstanceField("fd", "Ljava/io/FileDescriptor;", 0x0012U);
  builder.Constructor("(Ljava/io/File;)V", OpenInputFromPath(true));
  builder.Constructor("(Ljava/lang/String;)V", OpenInputFromPath(false));
  builder.FinalMethod("getFD", "()Ljava/io/FileDescriptor;",
                      [](IntrinsicContext &call) {
                        const auto field =
                            call.vm.Linker().FindFieldRecursive(
                                call.vm.Model().ObjectClass(call.receiver),
                                "fd", "Ljava/io/FileDescriptor;");
                        if (!field.has_value()) {
                          throw DexVmError(
                              DexVmErrorReason::internal_invariant,
                              "FileInputStream.fd field is unavailable");
                        }
                        const auto slot = call.vm.Linker().Field(*field).slot;
                        const auto descriptor = VmObjectRef(
                            call.vm.Model().InstanceSlots(call.receiver)[slot]
                                .bits);
                        if (!descriptor.IsValid()) {
                          throw VmJavaThrow{"Ljava/io/IOException;",
                                            "stream has no file descriptor"};
                        }
                        return VmValue::Ref(descriptor);
                      });
  builder.FinalMethod("close", "()V", [](IntrinsicContext &call) {
    call.vm.IO().CloseInput(call.receiver);
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(call.receiver), "fd",
        "Ljava/io/FileDescriptor;");
    if (field.has_value()) {
      const auto descriptor = VmObjectRef(
          call.vm.Model().InstanceSlots(call.receiver)
              [call.vm.Linker().Field(*field).slot]
                  .bits);
      if (descriptor.IsValid()) call.vm.IO().CloseDescriptor(descriptor);
    }
    return VmValue::Void();
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareFileDescriptor() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/FileDescriptor;",
                                              "Ljava/lang/Object;");
  builder.Constructor("()V", [](IntrinsicContext &) {
    return VmValue::Void();
  });
  builder.FinalMethod("valid", "()Z", [](IntrinsicContext &call) {
    const auto *descriptor = call.vm.IO().FindDescriptor(call.receiver);
    return VmValue::Int(descriptor != nullptr && !descriptor->closed);
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareFileReader() {
  auto builder =
      IntrinsicClassBuilder::Class("Ljava/io/FileReader;", "Ljava/io/Reader;");
  builder.Constructor("(Ljava/io/File;)V", OpenInputFromPath(true));
  builder.Constructor("(Ljava/lang/String;)V", OpenInputFromPath(false));
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareFileOutputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/FileOutputStream;",
                                              "Ljava/io/OutputStream;");
  builder.Constructor("(Ljava/io/File;)V", OpenOutputFromPath(true));
  builder.Constructor("(Ljava/lang/String;)V", OpenOutputFromPath(false));
  builder.FinalMethod("write", "([B)V", WriteBytes());
  builder.FinalMethod("flush", "()V", Flush(false));
  builder.FinalMethod("close", "()V", Flush(true));
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareFileWriter() {
  auto builder =
      IntrinsicClassBuilder::Class("Ljava/io/FileWriter;", "Ljava/io/Writer;");
  builder.Constructor("(Ljava/io/File;Z)V", [](IntrinsicContext &call) {
    const auto path = FilePath(call, call.arguments[0].ref);
    IoRuntime::OutputState output{path, {}, false};
    if (call.arguments[1].AsInt() != 0) {
      if (const auto existing = call.vm.IO().ReadFile(path)) {
        output.bytes = *existing;
      }
    }
    call.vm.IO().SetOutput(call.receiver, std::move(output));
    return VmValue::Void();
  });
  builder.FinalMethod(
      "append", "(C)Ljava/io/Writer;", [](IntrinsicContext &call) {
        IoRuntime::OutputState *output{};
        try {
          output = &call.vm.IO().Output(call.receiver);
        } catch (const IoRuntimeError &error) {
          IoFailure(error);
        }
        const auto unit =
            static_cast<char16_t>(call.arguments[0].cat1 & 0xffffU);
        const auto encoded = unit < 0x80U
                                 ? std::string(1, static_cast<char>(unit))
                                 : call.vm.StringUtf8(call.vm.Model().NewString(
                                       std::u16string(1, unit)));
        for (const auto value : encoded) {
          output->bytes.push_back(static_cast<std::byte>(value));
        }
        return VmValue::Ref(call.receiver);
      });
  builder.FinalMethod(
      "append", "(Ljava/lang/CharSequence;)Ljava/io/Writer;",
      [](IntrinsicContext &call) {
        IoRuntime::OutputState *output{};
        try {
          output = &call.vm.IO().Output(call.receiver);
        } catch (const IoRuntimeError &error) {
          IoFailure(error);
        }
        const auto value = call.arguments[0].ref;
        const auto text =
            value.IsValid() ? call.vm.StringUtf8(value) : std::string("null");
        for (const auto character : text) {
          output->bytes.push_back(static_cast<std::byte>(character));
        }
        return VmValue::Ref(call.receiver);
      });
  builder.FinalMethod("flush", "()V", Flush(false));
  builder.FinalMethod("close", "()V", Flush(true));
  return std::move(builder).Build();
}

} // namespace

void AppendJavaIoFiles(std::vector<IntrinsicClassDecl> &catalog) {
  catalog.push_back(DeclareFile());
  catalog.push_back(DeclareFileDescriptor());
  catalog.push_back(DeclareFileInputStream());
  catalog.push_back(DeclareFileOutputStream());
  catalog.push_back(DeclareFileReader());
  catalog.push_back(DeclareFileWriter());
}

} // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
void AppendJavaIoFiles(std::vector<IntrinsicClassDecl>& catalog) {
    dvm80_java_io_files::AppendJavaIoFiles(catalog);
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_io_IOException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_io_IOException {
using namespace detail;

IntrinsicClassDecl Declare_java_io_IOException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/io/IOException;", "Ljava/lang/Exception;");
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

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_io_IOException() {
    return dvm80_java_io_IOException::Declare_java_io_IOException();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_io_PrintStream.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_io_PrintStream {
using namespace detail;

IntrinsicClassDecl Declare_java_io_PrintStream() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/io/PrintStream;", "Ljava/lang/Object;");
    builder.FinalMethod("println", "(Ljava/lang/String;)V",
        [](IntrinsicContext &context) {
                const auto argument = context.arguments[0].ref;
                GuestLine(context, argument.IsValid() ? Narrow(Value(context, argument))
                                       : std::string("null"));
                return VmValue::Void();
            });
    builder.FinalMethod("println", "(I)V",
        [](IntrinsicContext &context) {
                GuestLine(context, std::to_string(context.arguments[0].AsInt()));
                return VmValue::Void();
            });
    builder.FinalMethod("println", "()V",
        [](IntrinsicContext& context) {
                GuestLine(context, "");
                return VmValue::Void();
            });
    builder.FinalMethod("print", "(Ljava/lang/String;)V",
        [](IntrinsicContext &context) {
                const auto argument = context.arguments[0].ref;
                GuestLine(context, argument.IsValid() ? Narrow(Value(context, argument))
                                       : std::string("null"));
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_io_PrintStream() {
    return dvm80_java_io_PrintStream::Declare_java_io_PrintStream();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_io_Serializable.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_io_Serializable {
using namespace detail;

IntrinsicClassDecl Declare_java_io_Serializable() {
    auto builder = IntrinsicClassBuilder::Interface("Ljava/io/Serializable;");
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_io_Serializable() {
    return dvm80_java_io_Serializable::Declare_java_io_Serializable();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_io_streams.cpp ----
#include "catalog.h"
#include "shared.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_io_streams {
namespace {

[[noreturn]] void IoFailure(const IoRuntimeError &error) {
  throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
}

[[nodiscard]] IoRuntime::InputState &Input(IntrinsicContext &call) {
  try {
    return call.vm.IO().Input(call.receiver);
  } catch (const IoRuntimeError &error) {
    IoFailure(error);
  }
}

[[nodiscard]] IoRuntime::OutputState &Output(IntrinsicContext &call) {
  try {
    return call.vm.IO().Output(call.receiver);
  } catch (const IoRuntimeError &error) {
    IoFailure(error);
  }
}

IntrinsicHandler AdoptInput() {
  return [](IntrinsicContext &call) {
    try {
      call.vm.IO().AdoptInput(call.arguments[0].ref, call.receiver);
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
    return VmValue::Void();
  };
}

IntrinsicHandler AdoptOutput() {
  return [](IntrinsicContext &call) {
    try {
      call.vm.IO().AdoptOutput(call.arguments[0].ref, call.receiver);
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
    return VmValue::Void();
  };
}

IntrinsicHandler WriteRange() {
  return [](IntrinsicContext &call) {
    auto &output = Output(call);
    const auto array = call.arguments[0].ref;
    const auto offset = call.arguments[1].AsInt();
    const auto length = call.arguments[2].AsInt();
    if (offset < 0 || length < 0 ||
        static_cast<std::int64_t>(offset) + length >
            call.vm.Model().ArrayLength(array)) {
      throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                        "write range exceeds the source array"};
    }
    const auto bytes = call.vm.Model().ReadByteRegion(array, offset, length);
    output.bytes.insert(output.bytes.end(), bytes.begin(), bytes.end());
    return VmValue::Void();
  };
}

IntrinsicHandler WriteBytes() {
  return [](IntrinsicContext &call) {
    auto &output = Output(call);
    const auto array = call.arguments[0].ref;
    const auto bytes = call.vm.Model().ReadByteRegion(
        array, 0, call.vm.Model().ArrayLength(array));
    output.bytes.insert(output.bytes.end(), bytes.begin(), bytes.end());
    return VmValue::Void();
  };
}

IntrinsicHandler FlushOutput(const bool close) {
  return [close](IntrinsicContext &call) {
    try {
      call.vm.IO().FlushOutput(call.receiver, close);
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
    return VmValue::Void();
  };
}

IntrinsicClassDecl DeclareInputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/InputStream;",
                                              "Ljava/lang/Object;");
  builder.VirtualMethod("read", "([BII)I", [](IntrinsicContext &call) {
    auto &input = Input(call);
    const auto offset = call.arguments[1].AsInt();
    const auto length = call.arguments[2].AsInt();
    const auto array = call.arguments[0].ref;
    if (offset < 0 || length < 0 ||
        static_cast<std::int64_t>(offset) + length >
            call.vm.Model().ArrayLength(array)) {
      throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                        "invalid stream read range"};
    }
    if (length == 0)
      return VmValue::Int(0);
    const auto remaining = input.bytes.size() - input.cursor;
    if (remaining == 0)
      return VmValue::Int(-1);
    const auto amount = std::min<std::size_t>(length, remaining);
    call.vm.Model().WriteByteRegion(
        array, offset, std::span(input.bytes).subspan(input.cursor, amount));
    input.cursor += amount;
    return VmValue::Int(static_cast<std::int32_t>(amount));
  });
  builder.VirtualMethod("read", "([B)I", [](IntrinsicContext &call) {
    auto &input = Input(call);
    const auto array = call.arguments[0].ref;
    const auto capacity = call.vm.Model().ArrayLength(array);
    if (capacity == 0)
      return VmValue::Int(0);
    const auto remaining = input.bytes.size() - input.cursor;
    if (remaining == 0)
      return VmValue::Int(-1);
    const auto amount = std::min<std::size_t>(capacity, remaining);
    call.vm.Model().WriteByteRegion(
        array, 0, std::span(input.bytes).subspan(input.cursor, amount));
    input.cursor += amount;
    return VmValue::Int(static_cast<std::int32_t>(amount));
  });
  builder.VirtualMethod("read", "()I", [](IntrinsicContext &call) {
    auto &input = Input(call);
    if (input.cursor >= input.bytes.size())
      return VmValue::Int(-1);
    return VmValue::Int(static_cast<std::uint8_t>(input.bytes[input.cursor++]));
  });
  builder.VirtualMethod("available", "()I", [](IntrinsicContext &call) {
    const auto &input = Input(call);
    return VmValue::Int(
        static_cast<std::int32_t>(input.bytes.size() - input.cursor));
  });
  builder.VirtualMethod("skip", "(J)J", [](IntrinsicContext &call) {
    auto &input = Input(call);
    const auto remaining =
        static_cast<std::int64_t>(input.bytes.size() - input.cursor);
    const auto amount = std::max<std::int64_t>(
        0, std::min(call.arguments[0].AsLong(), remaining));
    input.cursor += static_cast<std::size_t>(amount);
    return VmValue::Long(amount);
  });
  builder.VirtualMethod("close", "()V", [](IntrinsicContext &call) {
    call.vm.IO().CloseInput(call.receiver);
    return VmValue::Void();
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareOutputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/OutputStream;",
                                              "Ljava/lang/Object;");
  builder.VirtualMethod("write", "([BII)V", WriteRange());
  builder.VirtualMethod("write", "([B)V", WriteBytes());
  builder.VirtualMethod("write", "(I)V", [](IntrinsicContext &call) {
    Output(call).bytes.push_back(
        static_cast<std::byte>(call.arguments[0].AsInt() & 0xff));
    return VmValue::Void();
  });
  builder.VirtualMethod("flush", "()V", FlushOutput(false));
  builder.VirtualMethod("close", "()V", FlushOutput(true));
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareByteArrayInputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/ByteArrayInputStream;",
                                              "Ljava/io/InputStream;");
  builder.Constructor("([B)V", [](IntrinsicContext &call) {
    const auto array = call.arguments[0].ref;
    call.vm.IO().SetInput(call.receiver,
                          {call.vm.Model().ReadByteRegion(
                               array, 0, call.vm.Model().ArrayLength(array)),
                           0, false});
    return VmValue::Void();
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareByteArrayOutputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/ByteArrayOutputStream;",
                                              "Ljava/io/OutputStream;");
  builder.Constructor("()V", [](IntrinsicContext &call) {
    call.vm.IO().SetOutput(call.receiver, {{}, {}, false});
    return VmValue::Void();
  });
  builder.FinalMethod("write", "([BII)V", WriteRange());
  builder.FinalMethod("write", "([B)V", WriteBytes());
  builder.FinalMethod("toByteArray", "()[B", [](IntrinsicContext &call) {
    const auto &bytes = Output(call).bytes;
    const auto array_class = call.vm.Linker().ResolveDescriptor("[B");
    const auto array =
        call.vm.Model().NewPrimitiveArray(array_class, JniPrimitiveKind::byte,
                                          static_cast<JniSize>(bytes.size()));
    if (!bytes.empty())
      call.vm.Model().WriteByteRegion(array, 0, bytes);
    return VmValue::Ref(array);
  });
  builder.FinalMethod("size", "()I", [](IntrinsicContext &call) {
    return VmValue::Int(static_cast<std::int32_t>(Output(call).bytes.size()));
  });
  builder.FinalMethod(
      "toString", "()Ljava/lang/String;", [](IntrinsicContext &call) {
        const auto &bytes = Output(call).bytes;
        return VmValue::Ref(call.vm.NewStringUtf8(std::string(
            reinterpret_cast<const char *>(bytes.data()), bytes.size())));
      });
  builder.FinalMethod("close", "()V",
                      [](IntrinsicContext &) { return VmValue::Void(); });
  return std::move(builder).Build();
}

IntrinsicClassDecl BuildWrapper(std::string descriptor, std::string superclass,
                                std::string argument, const bool output,
                                const bool capacity = false) {
  auto builder = IntrinsicClassBuilder::Class(std::move(descriptor),
                                              std::move(superclass));
  const auto handler = output ? AdoptOutput() : AdoptInput();
  builder.Constructor("(" + argument + ")V", handler);
  if (capacity)
    builder.Constructor("(" + argument + "I)V", handler);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareReader() {
  return std::move(IntrinsicClassBuilder::Class("Ljava/io/Reader;",
                                                "Ljava/lang/Object;"))
      .Build();
}

IntrinsicClassDecl DeclareWriter() {
  return std::move(IntrinsicClassBuilder::Class("Ljava/io/Writer;",
                                                "Ljava/lang/Object;"))
      .Build();
}

IntrinsicClassDecl DeclareFilterInputStream() {
  return BuildWrapper("Ljava/io/FilterInputStream;", "Ljava/io/InputStream;",
                      "Ljava/io/InputStream;", false);
}

IntrinsicClassDecl DeclareFilterOutputStream() {
  return BuildWrapper("Ljava/io/FilterOutputStream;", "Ljava/io/OutputStream;",
                      "Ljava/io/OutputStream;", true);
}

IntrinsicClassDecl DeclareBufferedInputStream() {
  return BuildWrapper("Ljava/io/BufferedInputStream;",
                      "Ljava/io/FilterInputStream;", "Ljava/io/InputStream;",
                      false, true);
}

IntrinsicClassDecl DeclareBufferedOutputStream() {
  return BuildWrapper("Ljava/io/BufferedOutputStream;",
                      "Ljava/io/FilterOutputStream;", "Ljava/io/OutputStream;",
                      true, true);
}

IntrinsicClassDecl DeclareInputStreamReader() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/InputStreamReader;",
                                              "Ljava/io/Reader;");
  const auto handler = AdoptInput();
  builder.Constructor("(Ljava/io/InputStream;)V", handler);
  builder.Constructor("(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V",
                      handler);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareBufferedReader() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/BufferedReader;",
                                              "Ljava/io/Reader;");
  builder.Constructor("(Ljava/io/Reader;)V", AdoptInput());
  builder.FinalMethod(
      "readLine", "()Ljava/lang/String;", [](IntrinsicContext &call) {
        auto &input = Input(call);
        if (input.cursor >= input.bytes.size()) {
          return VmValue::Ref(VmObjectRef{});
        }
        std::string line;
        while (input.cursor < input.bytes.size()) {
          const auto byte = static_cast<char>(input.bytes[input.cursor++]);
          if (byte == '\n')
            break;
          if (byte == '\r') {
            if (input.cursor < input.bytes.size() &&
                static_cast<char>(input.bytes[input.cursor]) == '\n') {
              ++input.cursor;
            }
            break;
          }
          line.push_back(byte);
        }
        return VmValue::Ref(call.vm.NewStringUtf8(line));
      });
  builder.FinalMethod("ready", "()Z", [](IntrinsicContext &call) {
    const auto &input = Input(call);
    return VmValue::Int(input.cursor < input.bytes.size());
  });
  builder.FinalMethod("close", "()V", [](IntrinsicContext &call) {
    call.vm.IO().CloseInput(call.receiver);
    return VmValue::Void();
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareDataInputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/DataInputStream;",
                                              "Ljava/io/InputStream;");
  builder.Constructor("(Ljava/io/InputStream;)V", AdoptInput());
  const auto take = [](IntrinsicContext &call, const std::size_t count) {
    auto &input = Input(call);
    if (input.bytes.size() - input.cursor < count) {
      throw VmJavaThrow{"Ljava/io/EOFException;", "end of stream"};
    }
    const auto begin = input.cursor;
    input.cursor += count;
    return std::span(input.bytes).subspan(begin, count);
  };
  builder.FinalMethod("readFully", "([B)V", [take](IntrinsicContext &call) {
    const auto array = call.arguments[0].ref;
    const auto count =
        static_cast<std::size_t>(call.vm.Model().ArrayLength(array));
    call.vm.Model().WriteByteRegion(array, 0, take(call, count));
    return VmValue::Void();
  });
  builder.FinalMethod("skipBytes", "(I)I", [](IntrinsicContext &call) {
    auto &input = Input(call);
    const auto requested = call.arguments[0].AsInt();
    const auto amount = std::min<std::size_t>(
        requested > 0 ? static_cast<std::size_t>(requested) : 0,
        input.bytes.size() - input.cursor);
    input.cursor += amount;
    return VmValue::Int(static_cast<std::int32_t>(amount));
  });
  builder.FinalMethod("readInt", "()I", [take](IntrinsicContext &call) {
    std::uint32_t value{};
    for (const auto byte : take(call, 4)) {
      value = (value << 8U) | static_cast<std::uint8_t>(byte);
    }
    return VmValue::Int(static_cast<std::int32_t>(value));
  });
  builder.FinalMethod("readLong", "()J", [take](IntrinsicContext &call) {
    std::uint64_t value{};
    for (const auto byte : take(call, 8)) {
      value = (value << 8U) | static_cast<std::uint8_t>(byte);
    }
    return VmValue::Long(static_cast<std::int64_t>(value));
  });
  builder.FinalMethod(
      "readUTF", "()Ljava/lang/String;", [take](IntrinsicContext &call) {
        const auto length_bytes = take(call, 2);
        const auto length = static_cast<std::size_t>(
            (static_cast<std::uint8_t>(length_bytes[0]) << 8U) |
            static_cast<std::uint8_t>(length_bytes[1]));
        const auto bytes = take(call, length);
        return VmValue::Ref(call.vm.NewStringUtf8(std::string(
            reinterpret_cast<const char *>(bytes.data()), bytes.size())));
      });
  builder.FinalMethod("close", "()V", [](IntrinsicContext &call) {
    call.vm.IO().CloseInput(call.receiver);
    return VmValue::Void();
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareDataOutputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/DataOutputStream;",
                                              "Ljava/io/OutputStream;");
  builder.Constructor("(Ljava/io/OutputStream;)V", AdoptOutput());
  builder.FinalMethod(
      "writeUTF", "(Ljava/lang/String;)V", [](IntrinsicContext &call) {
        auto &bytes = Output(call).bytes;
        const auto text = call.vm.StringUtf8(call.arguments[0].ref);
        bytes.push_back(static_cast<std::byte>((text.size() >> 8U) & 0xffU));
        bytes.push_back(static_cast<std::byte>(text.size() & 0xffU));
        for (const auto character : text) {
          bytes.push_back(static_cast<std::byte>(character));
        }
        return VmValue::Void();
      });
  builder.FinalMethod("close", "()V", FlushOutput(true));
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareObjectInputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/ObjectInputStream;",
                                              "Ljava/io/InputStream;");
  builder.Constructor("()V",
                      [](IntrinsicContext &) { return VmValue::Void(); });
  return std::move(builder).Build();
}

} // namespace

void AppendJavaIoStreams(std::vector<IntrinsicClassDecl> &catalog) {
  auto closeable = IntrinsicClassBuilder::Interface(
      "Ljava/io/Closeable;", {"Ljava/lang/AutoCloseable;"});
  closeable.UnimplementedVirtual("close", "()V");
  catalog.push_back(std::move(closeable).Build());
  catalog.push_back(DeclareInputStream());
  catalog.push_back(DeclareOutputStream());
  catalog.push_back(DeclareReader());
  catalog.push_back(DeclareWriter());
  catalog.push_back(DeclareByteArrayInputStream());
  catalog.push_back(DeclareByteArrayOutputStream());
  catalog.push_back(DeclareFilterInputStream());
  catalog.push_back(DeclareFilterOutputStream());
  catalog.push_back(DeclareBufferedInputStream());
  catalog.push_back(DeclareBufferedOutputStream());
  catalog.push_back(DeclareBufferedReader());
  catalog.push_back(DeclareInputStreamReader());
  catalog.push_back(DeclareDataInputStream());
  catalog.push_back(DeclareDataOutputStream());
  catalog.push_back(DeclareObjectInputStream());
}

} // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
void AppendJavaIoStreams(std::vector<IntrinsicClassDecl>& catalog) {
    dvm80_java_io_streams::AppendJavaIoStreams(catalog);
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_io_UnsupportedEncodingException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_io_UnsupportedEncodingException {
using namespace detail;

IntrinsicClassDecl Declare_java_io_UnsupportedEncodingException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/io/UnsupportedEncodingException;", "Ljava/io/IOException;");
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

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_io_UnsupportedEncodingException() {
    return dvm80_java_io_UnsupportedEncodingException::Declare_java_io_UnsupportedEncodingException();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

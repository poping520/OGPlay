// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_io_EOFException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_io_EOFException {
using namespace detail;

IntrinsicClassDecl Declare_java_io_EOFException() {
    return DeclareSimpleThrowable("Ljava/io/EOFException;", "Ljava/io/IOException;");
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
    return DeclareSimpleThrowable("Ljava/io/FileNotFoundException;", "Ljava/io/IOException;");
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
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_io_files {
namespace {

[[nodiscard]] VmObjectRef FilePathRef(IntrinsicContext &call,
                                     const VmObjectRef file) {
  const auto slots = call.vm.Model().InstanceSlots(file);
  return VmObjectRef(slots[0].bits);
}

[[nodiscard]] std::string FilePath(IntrinsicContext &call,
                                   const VmObjectRef file) {
  return call.vm.StringUtf8(FilePathRef(call, file));
}

[[nodiscard]] bool IsAbsoluteFilePath(const std::string_view path) {
  return path.starts_with('/');
}

[[nodiscard]] std::string ChildFilePath(
    std::optional<std::string_view> directory, std::string_view name);

[[nodiscard]] std::optional<std::string> ParentFilePath(
    const std::string_view path) {
  const auto slash = path.rfind('/');
  if (slash == std::string_view::npos)
    return std::nullopt;
  if (slash == 0U)
    return path.size() == 1U ? std::nullopt : std::optional<std::string>{"/"};
  return std::string(path.substr(0, slash));
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
    const std::string_view name, const std::string_view descriptor,
    const std::span<const VmValue> arguments = {}) {
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
  std::vector<VmValue> invoke_arguments;
  invoke_arguments.reserve(arguments.size() + 1U);
  invoke_arguments.push_back(VmValue::Ref(receiver));
  invoke_arguments.insert(invoke_arguments.end(), arguments.begin(),
                          arguments.end());
  const auto outcome = context.vm.Call(
      linker.Class(java_class).vtable[*index], invoke_arguments);
  if (outcome.exception.IsValid()) {
    context.vm.SetPendingException(outcome.exception);
    return std::nullopt;
  }
  return outcome.value;
}

[[nodiscard]] std::optional<VmObjectRef> NewFile(
    IntrinsicContext &call, const std::string_view path) {
  const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
  const auto java_class = call.vm.Model().ObjectClass(file);
  const auto constructor = call.vm.Linker().FindDirectMethod(
      java_class, "<init>", "(Ljava/lang/String;)V");
  if (!constructor.has_value()) {
    throw DexVmError(DexVmErrorReason::internal_invariant,
                     "File(String) constructor is unavailable");
  }
  const std::array arguments{VmValue::Ref(file),
                             VmValue::Ref(call.vm.NewStringUtf8(path))};
  const auto outcome = call.vm.Call(*constructor, arguments);
  if (outcome.exception.IsValid()) {
    call.vm.SetPendingException(outcome.exception);
    return std::nullopt;
  }
  return file;
}

[[nodiscard]] std::string EncodeFileUriPath(const std::string_view path) {
  constexpr char kHex[] = "0123456789ABCDEF";
  std::string encoded;
  for (const auto character : path) {
    const auto byte = static_cast<unsigned char>(character);
    const bool safe = (byte >= 'a' && byte <= 'z') ||
                      (byte >= 'A' && byte <= 'Z') ||
                      (byte >= '0' && byte <= '9') || byte >= 0x80U ||
                      std::string_view{"_-!.~'()*,;:$&+=/@"}.find(
                          static_cast<char>(byte)) != std::string_view::npos;
    if (safe) {
      encoded.push_back(static_cast<char>(byte));
    } else {
      encoded.push_back('%');
      encoded.push_back(kHex[byte >> 4U]);
      encoded.push_back(kHex[byte & 0x0FU]);
    }
  }
  return encoded;
}

[[nodiscard]] std::optional<VmObjectRef> NewUri(
    IntrinsicContext &call, const std::string_view spec) {
  const auto uri = call.vm.NewIntrinsicInstance("Ljava/net/URI;");
  const auto java_class = call.vm.Model().ObjectClass(uri);
  const auto constructor = call.vm.Linker().FindDirectMethod(
      java_class, "<init>", "(Ljava/lang/String;)V");
  if (!constructor.has_value()) {
    throw DexVmError(DexVmErrorReason::internal_invariant,
                     "URI(String) constructor is unavailable");
  }
  const std::array arguments{VmValue::Ref(uri),
                             VmValue::Ref(call.vm.NewStringUtf8(spec))};
  const auto outcome = call.vm.Call(*constructor, arguments);
  if (outcome.exception.IsValid()) {
    call.vm.SetPendingException(outcome.exception);
    return std::nullopt;
  }
  return uri;
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
  auto builder = IntrinsicClassBuilder::Class(
      "Ljava/io/File;", "Ljava/lang/Object;",
      {"Ljava/io/Serializable;", "Ljava/lang/Comparable;"});
  builder.ConstantInt("separatorChar", "C", '/', 0x0019U);
  builder.ConstantString("separator", "/", 0x0019U);
  builder.ConstantInt("pathSeparatorChar", "C", ':', 0x0019U);
  builder.ConstantString("pathSeparator", ":", 0x0019U);
  builder.InstanceField("path", "Ljava/lang/String;", 0x0002U);
  // 使用字符串路径创建 File 对象。
  builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext &call) {
    if (!call.arguments[0].ref.IsValid()) {
      throw VmJavaThrow{"Ljava/lang/NullPointerException;", "path == null"};
    }
    SetFilePath(call, call.vm.StringUtf8(call.arguments[0].ref));
    return VmValue::Void();
  });
  // 使用父路径字符串和子名称创建 File 对象。
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
  // 使用父 File 对象和子名称创建 File 对象。
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
  // 将合法的 file URI 转换为 File 对象。
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
  // 判断路径在 guest 文件系统中是否存在。
  builder.VirtualMethod("exists", "()Z", [](IntrinsicContext &call) {
    return VmValue::Int(
        call.vm.IO().Stat(FilePath(call, call.receiver)).has_value());
  });
  // 返回文件长度，不存在或读取失败时返回零。
  builder.VirtualMethod("length", "()J", [](IntrinsicContext &call) {
    const auto info = call.vm.IO().Stat(FilePath(call, call.receiver));
    return VmValue::Long(
        info.has_value() ? static_cast<std::int64_t>(info->size) : 0);
  });
  const auto get_path = [](IntrinsicContext &call) {
    return VmValue::Ref(FilePathRef(call, call.receiver));
  };
  // 返回创建 File 时保存的路径字符串。
  builder.VirtualMethod("getPath", "()Ljava/lang/String;", get_path);
  // 返回基于 guest 工作目录解析的绝对路径。
  builder.VirtualMethod("getAbsolutePath", "()Ljava/lang/String;",
                        [](IntrinsicContext &call) {
    const auto path = FilePath(call, call.receiver);
    const auto absolute = InvokeVirtual(
        call, call.receiver, "isAbsolute", "()Z");
    if (!absolute.has_value())
      return VmValue::Ref(VmObjectRef{});
    if (absolute->AsInt() != 0)
      return VmValue::Ref(call.vm.NewStringUtf8(path));
    const auto working_directory = call.vm.IO().WorkingDirectory();
    if (!working_directory.has_value()) {
      throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                        "guest working directory is unavailable"};
    }
    return VmValue::Ref(call.vm.NewStringUtf8(
        ChildFilePath(*working_directory, path)));
  });
  const auto make_directories = [](IntrinsicContext &call) {
    return VmValue::Int(
        call.vm.IO().MakeDirectories(FilePath(call, call.receiver)));
  };
  // 创建单级目录，要求父目录已经存在。
  builder.VirtualMethod("mkdir", "()Z", [](IntrinsicContext &call) {
    return VmValue::Int(
        call.vm.IO().MakeDirectory(FilePath(call, call.receiver)));
  });
  // 创建目录及其缺失的父目录。
  builder.VirtualMethod("mkdirs", "()Z", make_directories);
  // 原子创建空文件，并报告是否实际创建。
  builder.VirtualMethod("createNewFile", "()Z", [](IntrinsicContext &call) {
    try {
      return VmValue::Int(
          call.vm.IO().CreateFile(FilePath(call, call.receiver)));
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
  });
  // 删除文件或空目录，并报告是否成功。
  builder.VirtualMethod("delete", "()Z", [](IntrinsicContext &call) {
    return VmValue::Int(call.vm.IO().Delete(FilePath(call, call.receiver)));
  });
  // 判断路径是否表示 guest 文件系统中的目录。
  builder.VirtualMethod("isDirectory", "()Z", [](IntrinsicContext &call) {
    const auto info = call.vm.IO().Stat(FilePath(call, call.receiver));
    return VmValue::Int(info.has_value() && info->is_directory);
  });
  // 返回目录中的直接子项名称，非目录时返回 null。
  builder.VirtualMethod(
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
  // 返回路径中的文件名部分。
  builder.VirtualMethod("getName", "()Ljava/lang/String;",
                        [](IntrinsicContext &call) {
    const auto path = FilePath(call, call.receiver);
    const auto slash = path.rfind('/');
    const auto name = slash == std::string::npos
                          ? std::string_view(path)
                          : std::string_view(path).substr(slash + 1U);
    return VmValue::Ref(call.vm.NewStringUtf8(name));
  });
  // 返回路径中的父路径字符串，无父路径时返回 null。
  builder.VirtualMethod("getParent", "()Ljava/lang/String;",
                        [](IntrinsicContext &call) {
    const auto parent = ParentFilePath(FilePath(call, call.receiver));
    return VmValue::Ref(parent.has_value()
                            ? call.vm.NewStringUtf8(*parent)
                            : VmObjectRef{});
  });
  // 返回表示父路径的 File 对象，无父路径时返回 null。
  builder.VirtualMethod("getParentFile", "()Ljava/io/File;",
                        [](IntrinsicContext &call) {
    const auto parent = InvokeVirtual(
        call, call.receiver, "getParent", "()Ljava/lang/String;");
    if (!parent.has_value() || !parent->ref.IsValid())
      return VmValue::Ref(VmObjectRef{});
    const auto file = NewFile(call, call.vm.StringUtf8(parent->ref));
    return VmValue::Ref(file.value_or(VmObjectRef{}));
  });
  // 判断路径是否以 Android 根目录分隔符开头。
  builder.VirtualMethod("isAbsolute", "()Z", [](IntrinsicContext &call) {
    return VmValue::Int(IsAbsoluteFilePath(FilePath(call, call.receiver)));
  });
  // 返回使用绝对路径创建的新 File 对象。
  builder.VirtualMethod("getAbsoluteFile", "()Ljava/io/File;",
                        [](IntrinsicContext &call) {
    const auto path = InvokeVirtual(
        call, call.receiver, "getAbsolutePath", "()Ljava/lang/String;");
    if (!path.has_value())
      return VmValue::Ref(VmObjectRef{});
    const auto file = NewFile(call, call.vm.StringUtf8(path->ref));
    return VmValue::Ref(file.value_or(VmObjectRef{}));
  });
  // 判断文件名是否按 Android/Unix 规则以点开头。
  builder.VirtualMethod("isHidden", "()Z", [](IntrinsicContext &call) {
    if (FilePath(call, call.receiver).empty())
      return VmValue::Int(0);
    const auto name = InvokeVirtual(
        call, call.receiver, "getName", "()Ljava/lang/String;");
    if (!name.has_value())
      return VmValue::Int(0);
    const auto value = call.vm.StringUtf8(name->ref);
    return VmValue::Int(!value.empty() && value.front() == '.');
  });
  // 判断另一个 File 是否具有相同路径。
  builder.OverrideMethod("equals", "(Ljava/lang/Object;)Z",
                         [](IntrinsicContext &call) {
    const auto other = call.arguments[0].ref;
    if (!other.IsValid())
      return VmValue::Int(0);
    const auto file_class = call.vm.Linker().ResolveDescriptor("Ljava/io/File;");
    if (!call.vm.Linker().IsAssignable(
            file_class, call.vm.Model().ObjectClass(other))) {
      return VmValue::Int(0);
    }
    const auto right = InvokeVirtual(
        call, other, "getPath", "()Ljava/lang/String;");
    if (!right.has_value())
      return VmValue::Int(0);
    return VmValue::Int(FilePath(call, call.receiver) ==
                        call.vm.StringUtf8(right->ref));
  });
  // 返回与路径相等语义一致的哈希值。
  builder.OverrideMethod("hashCode", "()I", [](IntrinsicContext &call) {
    const auto path = InvokeVirtual(
        call, call.receiver, "getPath", "()Ljava/lang/String;");
    if (!path.has_value())
      return VmValue::Int(0);
    const auto hash = InvokeVirtual(call, path->ref, "hashCode", "()I");
    if (!hash.has_value())
      return VmValue::Int(0);
    return VmValue::Int(hash->AsInt() ^ 1234321);
  });
  const auto compare_to_file = [](IntrinsicContext &call) {
    const auto left = InvokeVirtual(
        call, call.receiver, "getPath", "()Ljava/lang/String;");
    if (!left.has_value())
      return VmValue::Int(0);
    const auto other_path = InvokeVirtual(
        call, call.arguments[0].ref, "getPath", "()Ljava/lang/String;");
    if (!other_path.has_value())
      return VmValue::Int(0);
    const std::array arguments{VmValue::Ref(other_path->ref)};
    const auto comparison = InvokeVirtual(
        call, left->ref, "compareTo", "(Ljava/lang/String;)I", arguments);
    return comparison.value_or(VmValue::Int(0));
  };
  // 按路径字符串的字典序比较两个 File。
  builder.VirtualMethod("compareTo", "(Ljava/io/File;)I", compare_to_file);
  // 为 Comparable 接口提供 Object 参数的编译器桥接方法。
  builder.VirtualMethod(
      "compareTo", "(Ljava/lang/Object;)I",
      [](IntrinsicContext &call) {
        const auto other = call.arguments[0].ref;
        if (other.IsValid()) {
          const auto file_class =
              call.vm.Linker().ResolveDescriptor("Ljava/io/File;");
          if (!call.vm.Linker().IsAssignable(
                  file_class, call.vm.Model().ObjectClass(other))) {
            throw VmJavaThrow{"Ljava/lang/ClassCastException;",
                              "object is not a java.io.File"};
          }
        }
        const std::array arguments{VmValue::Ref(other)};
        const auto comparison = InvokeVirtual(
            call, call.receiver, "compareTo", "(Ljava/io/File;)I", arguments);
        return comparison.value_or(VmValue::Int(0));
      },
      0x1041U);
  // 返回 File 保存的原始路径字符串。
  builder.OverrideMethod("toString", "()Ljava/lang/String;",
                         [](IntrinsicContext &call) {
    return VmValue::Ref(FilePathRef(call, call.receiver));
  });
  // 返回 Android 唯一的文件系统根目录“/”。
  builder.StaticMethod("listRoots", "()[Ljava/io/File;",
                       [](IntrinsicContext &call) {
    const auto array = call.vm.Model().NewObjectArray(
        call.vm.Linker().ResolveDescriptor("[Ljava/io/File;"),
        call.vm.Linker().ResolveDescriptor("Ljava/io/File;"), 1);
    const auto root = NewFile(call, "/");
    if (root.has_value())
      call.vm.Model().SetObjectElement(array, 0, *root);
    return VmValue::Ref(array);
  });
  // 将绝对 guest 路径转换为正确转义的 file URI。
  builder.VirtualMethod("toURI", "()Ljava/net/URI;",
                        [](IntrinsicContext &call) {
    const auto absolute_file = InvokeVirtual(
        call, call.receiver, "getAbsoluteFile", "()Ljava/io/File;");
    if (!absolute_file.has_value())
      return VmValue::Ref(VmObjectRef{});
    const auto absolute_path = InvokeVirtual(
        call, absolute_file->ref, "getPath", "()Ljava/lang/String;");
    if (!absolute_path.has_value())
      return VmValue::Ref(VmObjectRef{});
    auto path = call.vm.StringUtf8(absolute_path->ref);
    const auto directory = InvokeVirtual(
        call, absolute_file->ref, "isDirectory", "()Z");
    if (!directory.has_value())
      return VmValue::Ref(VmObjectRef{});
    if (directory->AsInt() != 0 && !path.ends_with('/'))
      path.push_back('/');
    const auto uri = NewUri(call, "file:" + EncodeFileUriPath(path));
    return VmValue::Ref(uri.value_or(VmObjectRef{}));
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareFilenameFilter() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/io/FilenameFilter;");
  // 判断指定目录中的文件名是否应被接收。
  builder.UnimplementedVirtual(
      "accept", "(Ljava/io/File;Ljava/lang/String;)Z", 0x0401U);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareFileFilter() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/io/FileFilter;");
  // 判断指定 File 对象是否应被接收。
  builder.UnimplementedVirtual("accept", "(Ljava/io/File;)Z", 0x0401U);
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
  builder.FinalOverrideMethod("close", "()V", [](IntrinsicContext &call) {
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
  builder.FinalOverrideMethod("write", "([B)V", WriteBytes());
  builder.FinalOverrideMethod("flush", "()V", Flush(false));
  builder.FinalOverrideMethod("close", "()V", Flush(true));
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
  catalog.push_back(DeclareFilenameFilter());
  catalog.push_back(DeclareFileFilter());
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
    return DeclareSimpleThrowable("Ljava/io/IOException;", "Ljava/lang/Exception;");
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
  builder.FinalOverrideMethod("write", "([BII)V", WriteRange());
  builder.FinalOverrideMethod("write", "([B)V", WriteBytes());
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
  builder.FinalOverrideMethod(
      "toString", "()Ljava/lang/String;", [](IntrinsicContext &call) {
        const auto &bytes = Output(call).bytes;
        return VmValue::Ref(call.vm.NewStringUtf8(std::string(
            reinterpret_cast<const char *>(bytes.data()), bytes.size())));
      });
  builder.FinalOverrideMethod("close", "()V",
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
  builder.FinalOverrideMethod("close", "()V", [](IntrinsicContext &call) {
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
  builder.FinalOverrideMethod("close", "()V", FlushOutput(true));
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
    return DeclareSimpleThrowable("Ljava/io/UnsupportedEncodingException;", "Ljava/io/IOException;");
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_io_UnsupportedEncodingException() {
    return dvm80_java_io_UnsupportedEncodingException::Declare_java_io_UnsupportedEncodingException();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

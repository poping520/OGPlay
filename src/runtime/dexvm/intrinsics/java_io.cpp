// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_io_EOFException.cpp ----
#include "catalog.h"
#include "../icu_support.h"
#include <unicode/ucnv.h>
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    IntrinsicClassDecl Declare_java_io_EOFException() {
        return DeclareSimpleThrowable("Ljava/io/EOFException;", "Ljava/io/IOException;");
    }
} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_FileNotFoundException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    IntrinsicClassDecl Declare_java_io_FileNotFoundException() {
        return DeclareSimpleThrowable("Ljava/io/FileNotFoundException;", "Ljava/io/IOException;");
    }
} // namespace ogplay::runtime::dexvm::intrinsics


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

namespace ogplay::runtime::dexvm::intrinsics {
    namespace {
        [[nodiscard]] VmObjectRef FilePathRef(IntrinsicContext& call,
                                              const VmObjectRef file) {
            const auto slots = call.vm.Model().InstanceSlots(file);
            return VmObjectRef(slots[0].bits);
        }

        [[nodiscard]] std::string FilePath(IntrinsicContext& call,
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

        [[noreturn]] void IoFailure(const IoRuntimeError& error) {
            throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
        }

        void RequireFileSystem(IntrinsicContext& call) {
            if (!call.vm.IO().HasFileSystem()) {
                throw VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "guest filesystem is unavailable"
                };
            }
        }

        [[nodiscard]] std::optional<IoFileInfo> FileStat(
            IntrinsicContext& call, const VmObjectRef file) {
            RequireFileSystem(call);
            return call.vm.IO().Stat(FilePath(call, file));
        }

        [[nodiscard]] std::string FixFileSlashes(const std::string_view path) {
            std::string fixed;
            fixed.reserve(path.size());
            bool last_was_slash{};
            for (const auto character: path) {
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
                call.vm.NewStringUtf8(FixFileSlashes(path)).Value(), SlotTag::ref
            };
        }

        [[nodiscard]] std::optional<VmValue> InvokeVirtual(
            IntrinsicContext& context, const VmObjectRef receiver,
            const std::string_view name, const std::string_view descriptor,
            const std::span<const VmValue> arguments = {}) {
            if (!receiver.IsValid()) {
                throw VmJavaThrow{
                    "Ljava/lang/NullPointerException;",
                    "virtual receiver == null"
                };
            }
            auto& linker = context.vm.Linker();
            const auto java_class = context.vm.Model().ObjectClass(receiver);
            const auto index = linker.FindVtableIndex(
                java_class, std::string(name), std::string(descriptor));
            if (!index.has_value()) {
                throw VmJavaThrow{
                    "Ljava/lang/AbstractMethodError;",
                    std::string(name) + std::string(descriptor)
                };
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
            IntrinsicContext& call, const std::string_view path) {
            const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
            const std::array file_references{file};
            [[maybe_unused]] const auto file_roots =
                    call.vm.ProtectReferences(file_references);
            const auto java_class = call.vm.Model().ObjectClass(file);
            const auto constructor = call.vm.Linker().FindDirectMethod(
                java_class, "<init>", "(Ljava/lang/String;)V");
            if (!constructor.has_value()) {
                throw DexVmError(DexVmErrorReason::internal_invariant,
                                 "File(String) constructor is unavailable");
            }
            const std::array arguments{
                VmValue::Ref(file),
                VmValue::Ref(call.vm.NewStringUtf8(path))
            };
            const auto outcome = call.vm.Call(*constructor, arguments);
            if (outcome.exception.IsValid()) {
                call.vm.SetPendingException(outcome.exception);
                return std::nullopt;
            }
            return file;
        }

        [[nodiscard]] std::optional<VmObjectRef> NewChildFile(
            IntrinsicContext& call, const VmObjectRef parent,
            const VmObjectRef name) {
            const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
            const std::array file_references{file};
            [[maybe_unused]] const auto file_roots =
                    call.vm.ProtectReferences(file_references);
            const auto java_class = call.vm.Model().ObjectClass(file);
            const auto constructor = call.vm.Linker().FindDirectMethod(
                java_class, "<init>", "(Ljava/io/File;Ljava/lang/String;)V");
            if (!constructor.has_value()) {
                throw DexVmError(DexVmErrorReason::internal_invariant,
                                 "File(File,String) constructor is unavailable");
            }
            const std::array arguments{
                VmValue::Ref(file), VmValue::Ref(parent),
                VmValue::Ref(name)
            };
            const auto outcome = call.vm.Call(*constructor, arguments);
            if (outcome.exception.IsValid()) {
                call.vm.SetPendingException(outcome.exception);
                return std::nullopt;
            }
            return file;
        }

        [[nodiscard]] VmObjectRef NewReferenceArray(
            IntrinsicContext& call, const std::string_view array_descriptor,
            const std::string_view element_descriptor,
            const std::span<const VmObjectRef> values) {
            const auto array = call.vm.Model().NewObjectArray(
                call.vm.Linker().ResolveDescriptor(array_descriptor),
                call.vm.Linker().ResolveDescriptor(element_descriptor),
                static_cast<JniSize>(values.size()));
            for (std::size_t index = 0; index < values.size(); ++index) {
                call.vm.Model().SetObjectElement(
                    array, static_cast<JniSize>(index), values[index]);
            }
            return array;
        }

        [[nodiscard]] std::vector<VmObjectRef> ReferenceArrayValues(
            IntrinsicContext& call, const VmObjectRef array) {
            const auto length = call.vm.Model().ArrayLength(array);
            std::vector<VmObjectRef> values;
            values.reserve(static_cast<std::size_t>(length));
            for (JniSize index = 0; index < length; ++index) {
                values.push_back(call.vm.Model().GetObjectElement(array, index));
            }
            return values;
        }

        [[nodiscard]] std::optional<VmObjectRef> FileNamesToFiles(
            IntrinsicContext& call, const VmObjectRef names) {
            if (!names.IsValid())
                return VmObjectRef{};
            const std::array name_references{names};
            [[maybe_unused]] const auto name_roots =
                    call.vm.ProtectReferences(name_references);
            std::vector<VmObjectRef> files;
            for (const auto name: ReferenceArrayValues(call, names)) {
                const auto file = NewChildFile(call, call.receiver, name);
                if (!file.has_value())
                    return std::nullopt;
                files.push_back(*file);
            }
            return NewReferenceArray(call, "[Ljava/io/File;", "Ljava/io/File;", files);
        }

        [[nodiscard]] std::string EncodeFileUriPath(const std::string_view path) {
            constexpr char kHex[] = "0123456789ABCDEF";
            std::string encoded;
            for (const auto character: path) {
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
            IntrinsicContext& call, const std::string_view spec) {
            const auto uri = call.vm.NewIntrinsicInstance("Ljava/net/URI;");
            const std::array uri_references{uri};
            [[maybe_unused]] const auto uri_roots =
                    call.vm.ProtectReferences(uri_references);
            const auto java_class = call.vm.Model().ObjectClass(uri);
            const auto constructor = call.vm.Linker().FindDirectMethod(
                java_class, "<init>", "(Ljava/lang/String;)V");
            if (!constructor.has_value()) {
                throw DexVmError(DexVmErrorReason::internal_invariant,
                                 "URI(String) constructor is unavailable");
            }
            const std::array arguments{
                VmValue::Ref(uri),
                VmValue::Ref(call.vm.NewStringUtf8(spec))
            };
            const auto outcome = call.vm.Call(*constructor, arguments);
            if (outcome.exception.IsValid()) {
                call.vm.SetPendingException(outcome.exception);
                return std::nullopt;
            }
            return uri;
        }

        IntrinsicHandler OpenInputFromPath(const bool file_argument) {
            return [file_argument](IntrinsicContext& call) {
                if (!call.arguments[0].ref.IsValid()) {
                    throw VmJavaThrow{
                        "Ljava/lang/NullPointerException;",
                        file_argument ? "file == null" : "path == null"
                    };
                }
                RequireFileSystem(call);
                auto path = file_argument
                                ? FilePath(call, call.arguments[0].ref)
                                : call.vm.StringUtf8(call.arguments[0].ref);
                if (!IsAbsoluteFilePath(path)) {
                    const auto working_directory = call.vm.IO().WorkingDirectory();
                    if (!working_directory.has_value()) {
                        throw VmJavaThrow{
                            "Ljava/lang/UnsupportedOperationException;",
                            "guest working directory is unavailable"
                        };
                    }
                    path = ChildFilePath(*working_directory, path);
                }
                const auto bytes = call.vm.IO().ReadFile(path);
                if (!bytes.has_value()) {
                    throw VmJavaThrow{
                        "Ljava/io/FileNotFoundException;",
                        "file not found: " + path
                    };
                }
                call.vm.IO().SetInput(call.receiver, {*bytes, 0, false});
                return VmValue::Void();
            };
        }

        IntrinsicHandler Flush(const bool close) {
            return [close](IntrinsicContext& call) {
                try {
                    call.vm.IO().FlushOutput(call.receiver, close);
                } catch (const IoRuntimeError& error) {
                    IoFailure(error);
                }
                return VmValue::Void();
            };
        }

        IntrinsicClassDecl DeclareFile() {
            auto builder = IntrinsicClassBuilder::Class(
                "Ljava/io/File;", "Ljava/lang/Object;",
                {"Ljava/io/Serializable;", "Ljava/lang/Comparable;"});
            builder.ConstantInt("separatorChar", "C", '/',
                                kAccPublic | kAccStatic | kAccFinal);
            builder.ConstantString("separator", "/",
                                   kAccPublic | kAccStatic | kAccFinal);
            builder.ConstantInt("pathSeparatorChar", "C", ':',
                                kAccPublic | kAccStatic | kAccFinal);
            builder.ConstantString("pathSeparator", ":",
                                   kAccPublic | kAccStatic | kAccFinal);
            builder.InstanceField("path", "Ljava/lang/String;", kAccPrivate);
            // 使用字符串路径创建 File 对象。
            builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext& call) {
                if (!call.arguments[0].ref.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "path == null"};
                }
                SetFilePath(call, call.vm.StringUtf8(call.arguments[0].ref));
                return VmValue::Void();
            });
            // 使用父路径字符串和子名称创建 File 对象。
            builder.Constructor("(Ljava/lang/String;Ljava/lang/String;)V",
                                [](IntrinsicContext& call) {
                                    if (!call.arguments[1].ref.IsValid()) {
                                        throw VmJavaThrow{
                                            "Ljava/lang/NullPointerException;",
                                            "name == null"
                                        };
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
                                        throw VmJavaThrow{
                                            "Ljava/lang/NullPointerException;",
                                            "name == null"
                                        };
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
                    throw VmJavaThrow{
                        "Ljava/lang/IllegalArgumentException;",
                        "URI is not absolute"
                    };
                }
                const auto raw_ssp = InvokeVirtual(
                    call, uri, "getRawSchemeSpecificPart", "()Ljava/lang/String;");
                if (!raw_ssp.has_value()) return VmValue::Void();
                if (!raw_ssp->ref.IsValid() ||
                    !call.vm.StringUtf8(raw_ssp->ref).starts_with('/')) {
                    throw VmJavaThrow{
                        "Ljava/lang/IllegalArgumentException;",
                        "URI is not hierarchical"
                    };
                }
                const auto scheme = InvokeVirtual(
                    call, uri, "getScheme", "()Ljava/lang/String;");
                if (!scheme.has_value()) return VmValue::Void();
                if (!scheme->ref.IsValid() || call.vm.StringUtf8(scheme->ref) != "file") {
                    throw VmJavaThrow{
                        "Ljava/lang/IllegalArgumentException;",
                        "Expected file scheme in URI"
                    };
                }
                const auto raw_path = InvokeVirtual(
                    call, uri, "getRawPath", "()Ljava/lang/String;");
                if (!raw_path.has_value()) return VmValue::Void();
                if (!raw_path->ref.IsValid() ||
                    call.vm.StringUtf8(raw_path->ref).empty()) {
                    throw VmJavaThrow{
                        "Ljava/lang/IllegalArgumentException;",
                        "Expected non-empty path in URI"
                    };
                }
                for (const auto [name, descriptor]: {
                         std::pair{"getRawAuthority", "()Ljava/lang/String;"},
                         std::pair{"getRawQuery", "()Ljava/lang/String;"},
                         std::pair{"getRawFragment", "()Ljava/lang/String;"}
                     }) {
                    const auto component = InvokeVirtual(call, uri, name, descriptor);
                    if (!component.has_value()) return VmValue::Void();
                    if (component->ref.IsValid()) {
                        throw VmJavaThrow{
                            "Ljava/lang/IllegalArgumentException;",
                            std::string("Unexpected URI component: ") + name
                        };
                    }
                }
                const auto path = InvokeVirtual(
                    call, uri, "getPath", "()Ljava/lang/String;");
                if (!path.has_value()) return VmValue::Void();
                SetFilePath(call, call.vm.StringUtf8(path->ref));
                return VmValue::Void();
            });
            // 判断路径是否存在并可由 guest 读取。
            builder.VirtualMethod("canRead", "()Z", [](IntrinsicContext& call) {
                return VmValue::Int(FileStat(call, call.receiver).has_value());
            });
            // 判断路径是否位于 guest 可写命名空间。
            builder.VirtualMethod("canWrite", "()Z", [](IntrinsicContext& call) {
                const auto info = FileStat(call, call.receiver);
                return VmValue::Int(info.has_value() && info->writable);
            });
            // 判断路径在 guest 文件系统中是否存在。
            builder.VirtualMethod("exists", "()Z", [](IntrinsicContext& call) {
                return VmValue::Int(FileStat(call, call.receiver).has_value());
            });
            // 返回文件长度，不存在或读取失败时返回零。
            builder.VirtualMethod("length", "()J", [](IntrinsicContext& call) {
                const auto info = FileStat(call, call.receiver);
                return VmValue::Long(
                    info.has_value() ? static_cast<std::int64_t>(info->size) : 0);
            });
            const auto get_path = [](IntrinsicContext& call) {
                return VmValue::Ref(FilePathRef(call, call.receiver));
            };
            // 返回创建 File 时保存的路径字符串。
            builder.VirtualMethod("getPath", "()Ljava/lang/String;", get_path);
            // 返回基于 guest 工作目录解析的绝对路径。
            builder.VirtualMethod("getAbsolutePath", "()Ljava/lang/String;",
                                  [](IntrinsicContext& call) {
                                      const auto path = FilePath(call, call.receiver);
                                      const auto absolute = InvokeVirtual(
                                          call, call.receiver, "isAbsolute", "()Z");
                                      if (!absolute.has_value())
                                          return VmValue::Ref(VmObjectRef{});
                                      if (absolute->AsInt() != 0)
                                          return VmValue::Ref(call.vm.NewStringUtf8(path));
                                      const auto working_directory = call.vm.IO().WorkingDirectory();
                                      if (!working_directory.has_value()) {
                                          throw VmJavaThrow{
                                              "Ljava/lang/UnsupportedOperationException;",
                                              "guest working directory is unavailable"
                                          };
                                      }
                                      return VmValue::Ref(call.vm.NewStringUtf8(
                                          ChildFilePath(*working_directory, path)));
                                  });
            const auto make_directories = [](IntrinsicContext& call) {
                RequireFileSystem(call);
                return VmValue::Int(
                    call.vm.IO().MakeDirectories(FilePath(call, call.receiver)));
            };
            // 创建单级目录，要求父目录已经存在。
            builder.VirtualMethod("mkdir", "()Z", [](IntrinsicContext& call) {
                RequireFileSystem(call);
                return VmValue::Int(
                    call.vm.IO().MakeDirectory(FilePath(call, call.receiver)));
            });
            // 创建目录及其缺失的父目录。
            builder.VirtualMethod("mkdirs", "()Z", make_directories);
            // 原子创建空文件，并报告是否实际创建。
            builder.VirtualMethod("createNewFile", "()Z", [](IntrinsicContext& call) {
                try {
                    return VmValue::Int(
                        call.vm.IO().CreateFile(FilePath(call, call.receiver)));
                } catch (const IoRuntimeError& error) {
                    IoFailure(error);
                }
            });
            // 删除文件或空目录，并报告是否成功。
            builder.VirtualMethod("delete", "()Z", [](IntrinsicContext& call) {
                RequireFileSystem(call);
                return VmValue::Int(call.vm.IO().Delete(FilePath(call, call.receiver)));
            });
            // 判断路径是否表示 guest 文件系统中的目录。
            builder.VirtualMethod("isDirectory", "()Z", [](IntrinsicContext& call) {
                const auto info = FileStat(call, call.receiver);
                return VmValue::Int(info.has_value() && info->is_directory);
            });
            // 判断路径是否表示 guest 文件系统中的普通文件。
            builder.VirtualMethod("isFile", "()Z", [](IntrinsicContext& call) {
                const auto info = FileStat(call, call.receiver);
                return VmValue::Int(info.has_value() && !info->is_directory);
            });
            // 返回目录中的直接子项名称，非目录时返回 null。
            builder.VirtualMethod(
                "list", "()[Ljava/lang/String;", [](IntrinsicContext& call) {
                    RequireFileSystem(call);
                    const auto names = call.vm.IO().List(FilePath(call, call.receiver));
                    if (!names.has_value())
                        return VmValue::Ref(VmObjectRef{});
                    const auto array = call.vm.Model().NewObjectArray(
                        call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;"),
                        call.vm.Linker().ResolveDescriptor("Ljava/lang/String;"),
                        static_cast<JniSize>(names->size()));
                    const std::array array_references{array};
                    [[maybe_unused]] const auto array_roots =
                            call.vm.ProtectReferences(array_references);
                    for (std::size_t index = 0; index < names->size(); ++index) {
                        call.vm.Model().SetObjectElement(
                            array, static_cast<JniSize>(index),
                            call.vm.NewStringUtf8((*names)[index]));
                    }
                    return VmValue::Ref(array);
                });
            // 使用 FilenameFilter 筛选目录中的直接子项名称。
            builder.VirtualMethod(
                "list", "(Ljava/io/FilenameFilter;)[Ljava/lang/String;",
                [](IntrinsicContext& call) {
                    const auto listed = InvokeVirtual(
                        call, call.receiver, "list", "()[Ljava/lang/String;");
                    if (!listed.has_value())
                        return VmValue::Ref(VmObjectRef{});
                    const auto filter = call.arguments[0].ref;
                    if (!filter.IsValid() || !listed->ref.IsValid())
                        return *listed;
                    const std::array listed_references{listed->ref};
                    [[maybe_unused]] const auto listed_roots =
                            call.vm.ProtectReferences(listed_references);
                    std::vector<VmObjectRef> accepted;
                    for (const auto name: ReferenceArrayValues(call, listed->ref)) {
                        const std::array arguments{
                            VmValue::Ref(call.receiver),
                            VmValue::Ref(name)
                        };
                        const auto matches = InvokeVirtual(
                            call, filter, "accept",
                            "(Ljava/io/File;Ljava/lang/String;)Z", arguments);
                        if (!matches.has_value())
                            return VmValue::Ref(VmObjectRef{});
                        if (matches->AsInt() != 0)
                            accepted.push_back(name);
                    }
                    return VmValue::Ref(NewReferenceArray(
                        call, "[Ljava/lang/String;", "Ljava/lang/String;", accepted));
                });
            // 返回目录直接子项对应的 File 对象数组。
            builder.VirtualMethod("listFiles", "()[Ljava/io/File;",
                                  [](IntrinsicContext& call) {
                                      const auto listed = InvokeVirtual(
                                          call, call.receiver, "list", "()[Ljava/lang/String;");
                                      if (!listed.has_value())
                                          return VmValue::Ref(VmObjectRef{});
                                      const auto files = FileNamesToFiles(call, listed->ref);
                                      return VmValue::Ref(files.value_or(VmObjectRef{}));
                                  });
            // 使用 FilenameFilter 筛选目录子项并返回 File 对象。
            builder.VirtualMethod(
                "listFiles", "(Ljava/io/FilenameFilter;)[Ljava/io/File;",
                [](IntrinsicContext& call) {
                    const std::array arguments{VmValue::Ref(call.arguments[0].ref)};
                    const auto listed = InvokeVirtual(
                        call, call.receiver, "list",
                        "(Ljava/io/FilenameFilter;)[Ljava/lang/String;", arguments);
                    if (!listed.has_value())
                        return VmValue::Ref(VmObjectRef{});
                    const auto files = FileNamesToFiles(call, listed->ref);
                    return VmValue::Ref(files.value_or(VmObjectRef{}));
                });
            // 使用 FileFilter 筛选目录中的 File 对象。
            builder.VirtualMethod(
                "listFiles", "(Ljava/io/FileFilter;)[Ljava/io/File;",
                [](IntrinsicContext& call) {
                    const auto listed = InvokeVirtual(
                        call, call.receiver, "listFiles", "()[Ljava/io/File;");
                    if (!listed.has_value())
                        return VmValue::Ref(VmObjectRef{});
                    const auto filter = call.arguments[0].ref;
                    if (!filter.IsValid() || !listed->ref.IsValid())
                        return *listed;
                    const std::array listed_references{listed->ref};
                    [[maybe_unused]] const auto listed_roots =
                            call.vm.ProtectReferences(listed_references);
                    std::vector<VmObjectRef> accepted;
                    for (const auto file: ReferenceArrayValues(call, listed->ref)) {
                        const std::array arguments{VmValue::Ref(file)};
                        const auto matches = InvokeVirtual(
                            call, filter, "accept", "(Ljava/io/File;)Z", arguments);
                        if (!matches.has_value())
                            return VmValue::Ref(VmObjectRef{});
                        if (matches->AsInt() != 0)
                            accepted.push_back(file);
                    }
                    return VmValue::Ref(NewReferenceArray(
                        call, "[Ljava/io/File;", "Ljava/io/File;", accepted));
                });
            // 返回路径中的文件名部分。
            builder.VirtualMethod("getName", "()Ljava/lang/String;",
                                  [](IntrinsicContext& call) {
                                      const auto path = FilePath(call, call.receiver);
                                      const auto slash = path.rfind('/');
                                      const auto name = slash == std::string::npos
                                                            ? std::string_view(path)
                                                            : std::string_view(path).substr(slash + 1U);
                                      return VmValue::Ref(call.vm.NewStringUtf8(name));
                                  });
            // 返回路径中的父路径字符串，无父路径时返回 null。
            builder.VirtualMethod("getParent", "()Ljava/lang/String;",
                                  [](IntrinsicContext& call) {
                                      const auto parent = ParentFilePath(FilePath(call, call.receiver));
                                      return VmValue::Ref(parent.has_value()
                                                              ? call.vm.NewStringUtf8(*parent)
                                                              : VmObjectRef{});
                                  });
            // 返回表示父路径的 File 对象，无父路径时返回 null。
            builder.VirtualMethod("getParentFile", "()Ljava/io/File;",
                                  [](IntrinsicContext& call) {
                                      const auto parent = InvokeVirtual(
                                          call, call.receiver, "getParent", "()Ljava/lang/String;");
                                      if (!parent.has_value() || !parent->ref.IsValid())
                                          return VmValue::Ref(VmObjectRef{});
                                      const auto file = NewFile(call, call.vm.StringUtf8(parent->ref));
                                      return VmValue::Ref(file.value_or(VmObjectRef{}));
                                  });
            // 判断路径是否以 Android 根目录分隔符开头。
            builder.VirtualMethod("isAbsolute", "()Z", [](IntrinsicContext& call) {
                return VmValue::Int(IsAbsoluteFilePath(FilePath(call, call.receiver)));
            });
            // 返回使用绝对路径创建的新 File 对象。
            builder.VirtualMethod("getAbsoluteFile", "()Ljava/io/File;",
                                  [](IntrinsicContext& call) {
                                      const auto path = InvokeVirtual(
                                          call, call.receiver, "getAbsolutePath", "()Ljava/lang/String;");
                                      if (!path.has_value())
                                          return VmValue::Ref(VmObjectRef{});
                                      const auto file = NewFile(call, call.vm.StringUtf8(path->ref));
                                      return VmValue::Ref(file.value_or(VmObjectRef{}));
                                  });
            // 判断文件名是否按 Android/Unix 规则以点开头。
            builder.VirtualMethod("isHidden", "()Z", [](IntrinsicContext& call) {
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
                                   [](IntrinsicContext& call) {
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
            builder.OverrideMethod("hashCode", "()I", [](IntrinsicContext& call) {
                const auto path = InvokeVirtual(
                    call, call.receiver, "getPath", "()Ljava/lang/String;");
                if (!path.has_value())
                    return VmValue::Int(0);
                const auto hash = InvokeVirtual(call, path->ref, "hashCode", "()I");
                if (!hash.has_value())
                    return VmValue::Int(0);
                return VmValue::Int(hash->AsInt() ^ 1234321);
            });
            const auto compare_to_file = [](IntrinsicContext& call) {
                const auto left = InvokeVirtual(
                    call, call.receiver, "getPath", "()Ljava/lang/String;");
                if (!left.has_value())
                    return VmValue::Int(0);
                const std::array left_references{left->ref};
                [[maybe_unused]] const auto left_roots =
                        call.vm.ProtectReferences(left_references);
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
                [](IntrinsicContext& call) {
                    const auto other = call.arguments[0].ref;
                    if (other.IsValid()) {
                        const auto file_class =
                                call.vm.Linker().ResolveDescriptor("Ljava/io/File;");
                        if (!call.vm.Linker().IsAssignable(
                            file_class, call.vm.Model().ObjectClass(other))) {
                            throw VmJavaThrow{
                                "Ljava/lang/ClassCastException;",
                                "object is not a java.io.File"
                            };
                        }
                    }
                    const std::array arguments{VmValue::Ref(other)};
                    const auto comparison = InvokeVirtual(
                        call, call.receiver, "compareTo", "(Ljava/io/File;)I", arguments);
                    return comparison.value_or(VmValue::Int(0));
                },
                kAccPublic | kAccBridge | kAccSynthetic);
            // 将普通文件重命名到新的 guest 路径。
            builder.VirtualMethod("renameTo", "(Ljava/io/File;)Z",
                                  [](IntrinsicContext& call) {
                                      const auto target = call.arguments[0].ref;
                                      if (!target.IsValid()) {
                                          throw VmJavaThrow{
                                              "Ljava/lang/NullPointerException;",
                                              "destination == null"
                                          };
                                      }
                                      RequireFileSystem(call);
                                      return VmValue::Int(call.vm.IO().Rename(
                                          FilePath(call, call.receiver), FilePath(call, target)));
                                  });
            // 请求设置可写性；当前仅确认已经可写的真实 VFS 状态。
            builder.VirtualMethod("setWritable", "(ZZ)Z",
                                  [](IntrinsicContext& call) {
                                      const bool writable = call.arguments[0].AsInt() != 0;
                                      const auto info = FileStat(call, call.receiver);
                                      return VmValue::Int(writable && info.has_value() && info->writable);
                                  });
            // 使用 ownerOnly=true 请求设置路径可写性。
            builder.VirtualMethod("setWritable", "(Z)Z",
                                  [](IntrinsicContext& call) {
                                      const std::array arguments{call.arguments[0], VmValue::Int(1)};
                                      const auto result = InvokeVirtual(
                                          call, call.receiver, "setWritable", "(ZZ)Z", arguments);
                                      return result.value_or(VmValue::Int(0));
                                  });
            // 返回 File 保存的原始路径字符串。
            builder.OverrideMethod("toString", "()Ljava/lang/String;",
                                   [](IntrinsicContext& call) {
                                       return VmValue::Ref(FilePathRef(call, call.receiver));
                                   });
            // 返回 Android 唯一的文件系统根目录“/”。
            builder.StaticMethod("listRoots", "()[Ljava/io/File;",
                                 [](IntrinsicContext& call) {
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
                                  [](IntrinsicContext& call) {
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
                "accept", "(Ljava/io/File;Ljava/lang/String;)Z",
                kAccPublic | kAccAbstract);
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclareFileFilter() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/io/FileFilter;");
            // 判断指定 File 对象是否应被接收。
            builder.UnimplementedVirtual("accept", "(Ljava/io/File;)Z",
                                         kAccPublic | kAccAbstract);
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclareFileInputStream() {
            auto builder = IntrinsicClassBuilder::Class("Ljava/io/FileInputStream;",
                                                        "Ljava/io/InputStream;");
            const auto fd = builder.BoundInstanceField(
                "fd", "Ljava/io/FileDescriptor;", kAccPrivate);
            const auto should_close =
                    builder.BoundInstanceField("shouldClose", "Z", kAccPrivate | kAccFinal);
            const auto open_path = [fd, should_close](const bool file_argument) {
                return [fd, should_close, file_argument](IntrinsicContext& call) {
                    IntrinsicCall typed(call);
                    const auto source = typed.NonNullRef(0, file_argument ? "file" : "path");
                    RequireFileSystem(call);
                    auto path = file_argument
                                    ? FilePath(call, source)
                                    : call.vm.StringUtf8(source);
                    if (!IsAbsoluteFilePath(path)) {
                        const auto working_directory = call.vm.IO().WorkingDirectory();
                        if (!working_directory.has_value()) {
                            throw VmJavaThrow{
                                "Ljava/lang/UnsupportedOperationException;",
                                "guest working directory is unavailable"
                            };
                        }
                        path = ChildFilePath(*working_directory, path);
                    }
                    const auto bytes = call.vm.IO().ReadFile(path);
                    if (!bytes.has_value()) {
                        throw VmJavaThrow{
                            "Ljava/io/FileNotFoundException;",
                            "file not found: " + path
                        };
                    }
                    auto input = call.vm.IO().SetInput(
                        call.receiver, {*bytes, 0, false});
                    const auto descriptor =
                            call.vm.NewIntrinsicInstance("Ljava/io/FileDescriptor;");
                    call.vm.IO().SetDescriptor(
                        descriptor, {
                            IoRuntime::DescriptorKind::vfs_path, path, 0, false,
                            std::move(input), {}
                        });
                    typed.SetRef(fd, descriptor);
                    typed.SetInt(should_close, 1);
                    return VmValue::Void();
                };
            };
            // 使用 File 创建拥有底层描述符的文件输入流。
            builder.Constructor("(Ljava/io/File;)V", open_path(true));
            // 使用路径创建拥有底层描述符的文件输入流。
            builder.Constructor("(Ljava/lang/String;)V", open_path(false));
            // 基于已有逻辑文件描述符创建不拥有该描述符的输入流。
            builder.Constructor("(Ljava/io/FileDescriptor;)V",
                                [fd, should_close](IntrinsicContext& call) {
                                    IntrinsicCall typed(call);
                                    const auto descriptor_ref = typed.NonNullRef(0, "fd");
                                    auto* descriptor = call.vm.IO().FindDescriptor(descriptor_ref);
                                    if (descriptor == nullptr || descriptor->closed ||
                                        (descriptor->input == nullptr && descriptor->output != nullptr)) {
                                        call.vm.IO().SetInput(call.receiver, {{}, 0, true}, false);
                                    } else if (descriptor->input != nullptr) {
                                        call.vm.IO().ShareInput(call.receiver, descriptor->input, false);
                                    } else if (descriptor->kind == IoRuntime::DescriptorKind::vfs_path) {
                                        const auto bytes = call.vm.IO().ReadFile(descriptor->source);
                                        auto input = call.vm.IO().SetInput(
                                            call.receiver,
                                            {
                                                bytes.has_value() ? *bytes : std::vector<std::byte>{}, 0,
                                                !bytes.has_value()
                                            },
                                            false);
                                        descriptor->input = std::move(input);
                                    } else {
                                        call.vm.IO().SetInput(call.receiver, {{}, 0, true}, false);
                                    }
                                    typed.SetRef(fd, descriptor_ref);
                                    typed.SetInt(should_close, 0);
                                    return VmValue::Void();
                                });
            // 返回无需阻塞即可读取的估计字节数。
            builder.OverrideMethod("available", "()I", [](IntrinsicContext& call) {
                try {
                    const auto& input = call.vm.IO().Input(call.receiver);
                    return VmValue::Int(static_cast<std::int32_t>(
                        input.bytes.size() - input.cursor));
                } catch (const IoRuntimeError& error) {
                    IoFailure(error);
                }
            });
            // 返回此输入流使用的逻辑文件描述符。
            builder.FinalMethod("getFD", "()Ljava/io/FileDescriptor;",
                                [fd](IntrinsicContext& call) {
                                    const auto descriptor =
                                            IntrinsicCall(call).GetRef(fd);
                                    if (!descriptor.IsValid()) {
                                        throw VmJavaThrow{
                                            "Ljava/io/IOException;",
                                            "stream has no file descriptor"
                                        };
                                    }
                                    return VmValue::Ref(descriptor);
                                });
            // 读取一个字节，流结束时返回 -1。
            builder.OverrideMethod("read", "()I", [](IntrinsicContext& call) {
                try {
                    auto& input = call.vm.IO().Input(call.receiver);
                    if (input.cursor >= input.bytes.size()) return VmValue::Int(-1);
                    return VmValue::Int(
                        static_cast<std::uint8_t>(input.bytes[input.cursor++]));
                } catch (const IoRuntimeError& error) {
                    IoFailure(error);
                }
            });
            // 将文件字节读取到数组的指定区间。
            builder.OverrideMethod("read", "([BII)I", [](IntrinsicContext& call) {
                const auto array = call.arguments[0].ref;
                if (!array.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
                }
                const auto offset = call.arguments[1].AsInt();
                const auto length = call.arguments[2].AsInt();
                if (offset < 0 || length < 0 ||
                    static_cast<std::int64_t>(offset) + length >
                    call.vm.Model().ArrayLength(array)) {
                    throw VmJavaThrow{
                        "Ljava/lang/IndexOutOfBoundsException;",
                        "read range exceeds the destination array"
                    };
                }
                try {
                    auto& input = call.vm.IO().Input(call.receiver);
                    if (length == 0) return VmValue::Int(0);
                    const auto remaining = input.bytes.size() - input.cursor;
                    if (remaining == 0) return VmValue::Int(-1);
                    const auto amount = std::min<std::size_t>(
                        static_cast<std::size_t>(length), remaining);
                    call.vm.Model().WriteByteRegion(
                        array, offset,
                        std::span(input.bytes).subspan(input.cursor, amount));
                    input.cursor += amount;
                    return VmValue::Int(static_cast<std::int32_t>(amount));
                } catch (const IoRuntimeError& error) {
                    IoFailure(error);
                }
            });
            // 前移文件读位置并返回实际跳过的字节数。
            builder.OverrideMethod("skip", "(J)J", [](IntrinsicContext& call) {
                const auto requested = call.arguments[0].AsLong();
                if (requested < 0) {
                    throw VmJavaThrow{"Ljava/io/IOException;", "byteCount < 0"};
                }
                try {
                    auto& input = call.vm.IO().Input(call.receiver);
                    const auto amount = std::min<std::uint64_t>(
                        static_cast<std::uint64_t>(requested),
                        input.bytes.size() - input.cursor);
                    input.cursor += static_cast<std::size_t>(amount);
                    return VmValue::Long(static_cast<std::int64_t>(amount));
                } catch (const IoRuntimeError& error) {
                    IoFailure(error);
                }
            });
            // 关闭输入流，并仅在拥有时关闭底层逻辑描述符。
            builder.OverrideMethod("close", "()V",
                                   [fd, should_close](IntrinsicContext& call) {
                                       IntrinsicCall typed(call);
                                       call.vm.IO().CloseInput(call.receiver);
                                       const auto descriptor = typed.GetRef(fd);
                                       if (typed.GetInt(should_close) != 0) {
                                           if (descriptor.IsValid()) call.vm.IO().CloseDescriptor(descriptor);
                                       } else {
                                           typed.SetRef(fd, call.vm.NewIntrinsicInstance(
                                                            "Ljava/io/FileDescriptor;"));
                                       }
                                       return VmValue::Void();
                                   });
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclareFileDescriptor() {
            auto builder = IntrinsicClassBuilder::Class("Ljava/io/FileDescriptor;",
                                                        "Ljava/lang/Object;", {},
                                                        kAccPublic | kAccFinal);
            builder.Constructor("()V", [](IntrinsicContext&) {
                return VmValue::Void();
            });
            builder.FinalMethod("valid", "()Z", [](IntrinsicContext& call) {
                const auto* descriptor = call.vm.IO().FindDescriptor(call.receiver);
                return VmValue::Int(descriptor != nullptr && !descriptor->closed);
            });
            builder.FinalMethod("sync", "()V", [](IntrinsicContext& call) {
                try {
                    call.vm.IO().SyncDescriptor(call.receiver);
                } catch (const IoRuntimeError& error) {
                    IoFailure(error);
                }
                return VmValue::Void();
            });
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclareFileReader() {
            auto builder = IntrinsicClassBuilder::Class(
                "Ljava/io/FileReader;", "Ljava/io/InputStreamReader;");
            for (const auto* signature : {"(Ljava/io/File;)V", "(Ljava/lang/String;)V",
                                          "(Ljava/io/FileDescriptor;)V"}) {
                builder.Constructor(signature, [signature](IntrinsicContext& call) {
                    const auto stream = call.vm.NewIntrinsicInstance("Ljava/io/FileInputStream;");
                    const std::array refs{stream};
                    const auto roots = call.vm.ProtectReferences(refs);
                    const auto initialize = [&](const char* owner, const char* descriptor,
                                                VmObjectRef receiver, VmObjectRef argument) {
                        const auto ctor = call.vm.Linker().FindDirectMethod(
                            call.vm.Linker().ResolveDescriptor(owner), "<init>", descriptor);
                        const std::array args{VmValue::Ref(receiver), VmValue::Ref(argument)};
                        const auto result = call.vm.Call(*ctor, args);
                        if (result.exception.IsValid()) throw VmJavaThrow{
                            call.vm.Linker().Class(result.exception_class).descriptor,
                            result.exception_message, result.exception};
                    };
                    initialize("Ljava/io/FileInputStream;", signature, stream, call.arguments[0].ref);
                    initialize("Ljava/io/InputStreamReader;", "(Ljava/io/InputStream;)V", call.receiver, stream);
                    return VmValue::Void();
                });
            }
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclareFileOutputStream() {
            auto builder = IntrinsicClassBuilder::Class("Ljava/io/FileOutputStream;",
                                                        "Ljava/io/OutputStream;");
            const auto fd = builder.BoundInstanceField(
                "fd", "Ljava/io/FileDescriptor;", kAccPrivate);
            const auto should_close =
                    builder.BoundInstanceField("shouldClose", "Z", kAccPrivate | kAccFinal);

            const auto open_path = [fd, should_close](const bool file_argument,
                                                      const bool has_append) {
                return [fd, should_close, file_argument,
                            has_append](IntrinsicContext& call) {
                    IntrinsicCall typed(call);
                    const auto source = typed.NonNullRef(0, file_argument ? "file" : "path");
                    auto path = file_argument
                                    ? FilePath(call, source)
                                    : call.vm.StringUtf8(source);
                    if (!IsAbsoluteFilePath(path)) {
                        const auto working_directory = call.vm.IO().WorkingDirectory();
                        if (!working_directory.has_value()) {
                            throw VmJavaThrow{
                                "Ljava/lang/UnsupportedOperationException;",
                                "guest working directory is unavailable"
                            };
                        }
                        path = ChildFilePath(*working_directory, path);
                    }
                    const auto append = has_append && typed.Int(1) != 0;
                    std::vector<std::byte> bytes;
                    if (append) {
                        if (const auto existing = call.vm.IO().ReadFile(path))
                            bytes = *existing;
                    }
                    try {
                        // API 19 在构造时即创建文件，并按 append 选择保留或截断原内容。
                        call.vm.IO().WriteFile(path, bytes);
                    } catch (const IoRuntimeError& error) {
                        throw VmJavaThrow{"Ljava/io/FileNotFoundException;", error.what()};
                    }
                    auto output = call.vm.IO().SetOutput(
                        call.receiver, {path, std::move(bytes), true, false});
                    const auto descriptor =
                            call.vm.NewIntrinsicInstance("Ljava/io/FileDescriptor;");
                    call.vm.IO().SetDescriptor(
                        descriptor, {
                            IoRuntime::DescriptorKind::vfs_path, path, 0, false,
                            {}, std::move(output)
                        });
                    typed.SetRef(fd, descriptor);
                    typed.SetInt(should_close, 1);
                    return VmValue::Void();
                };
            };

            // 使用 File 创建覆盖写入的文件输出流。
            builder.Constructor("(Ljava/io/File;)V", open_path(true, false));
            // 使用 File 创建可选择追加写入的文件输出流。
            builder.Constructor("(Ljava/io/File;Z)V", open_path(true, true));
            // 使用路径创建覆盖写入的文件输出流。
            builder.Constructor("(Ljava/lang/String;)V", open_path(false, false));
            // 使用路径创建可选择追加写入的文件输出流。
            builder.Constructor("(Ljava/lang/String;Z)V", open_path(false, true));
            // 基于已有逻辑文件描述符创建不拥有该描述符的输出流。
            builder.Constructor("(Ljava/io/FileDescriptor;)V",
                                [fd, should_close](IntrinsicContext& call) {
                                    IntrinsicCall typed(call);
                                    const auto descriptor_ref = typed.NonNullRef(0, "fd");
                                    auto* descriptor = call.vm.IO().FindDescriptor(descriptor_ref);
                                    if (descriptor == nullptr || descriptor->closed) {
                                        call.vm.IO().SetOutput(call.receiver, {{}, {}, false, false}, false);
                                    } else if (descriptor->output != nullptr) {
                                        call.vm.IO().ShareOutput(call.receiver, descriptor->output, false);
                                    } else {
                                        std::vector<std::byte> bytes;
                                        if (descriptor->kind == IoRuntime::DescriptorKind::vfs_path) {
                                            if (const auto existing = call.vm.IO().ReadFile(descriptor->source))
                                                bytes = *existing;
                                        }
                                        auto output = call.vm.IO().SetOutput(
                                            call.receiver,
                                            {
                                                descriptor->kind == IoRuntime::DescriptorKind::vfs_path
                                                    ? descriptor->source
                                                    : std::string{},
                                                std::move(bytes), false, false
                                            },
                                            false);
                                        descriptor->output = std::move(output);
                                    }
                                    typed.SetRef(fd, descriptor_ref);
                                    typed.SetInt(should_close, 0);
                                    return VmValue::Void();
                                });
            // 写入一个字节的低八位。
            builder.OverrideMethod("write", "(I)V", [](IntrinsicContext& call) {
                try {
                    call.vm.IO().Output(call.receiver).bytes.push_back(
                        static_cast<std::byte>(call.arguments[0].AsInt() & 0xff));
                } catch (const IoRuntimeError& error) {
                    IoFailure(error);
                }
                return VmValue::Void();
            });
            // 将字节数组的指定区间写入文件。
            builder.OverrideMethod("write", "([BII)V", [](IntrinsicContext& call) {
                const auto array = call.arguments[0].ref;
                if (!array.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
                }
                const auto offset = call.arguments[1].AsInt();
                const auto length = call.arguments[2].AsInt();
                if (offset < 0 || length < 0 ||
                    static_cast<std::int64_t>(offset) + length >
                    call.vm.Model().ArrayLength(array)) {
                    throw VmJavaThrow{
                        "Ljava/lang/IndexOutOfBoundsException;",
                        "write range exceeds the source array"
                    };
                }
                try {
                    auto& output = call.vm.IO().Output(call.receiver);
                    const auto bytes = call.vm.Model().ReadByteRegion(array, offset, length);
                    output.bytes.insert(output.bytes.end(), bytes.begin(), bytes.end());
                } catch (const IoRuntimeError& error) {
                    IoFailure(error);
                }
                return VmValue::Void();
            });
            // 返回此输出流使用的逻辑文件描述符。
            builder.FinalMethod("getFD", "()Ljava/io/FileDescriptor;",
                                [fd](IntrinsicContext& call) {
                                    const auto descriptor = IntrinsicCall(call).GetRef(fd);
                                    if (!descriptor.IsValid()) {
                                        throw VmJavaThrow{
                                            "Ljava/io/IOException;",
                                            "stream has no file descriptor"
                                        };
                                    }
                                    return VmValue::Ref(descriptor);
                                });
            // 刷新数据并按所有权关闭底层逻辑文件描述符。
            builder.OverrideMethod("close", "()V",
                                   [fd, should_close](IntrinsicContext& call) {
                                       IntrinsicCall typed(call);
                                       try {
                                           call.vm.IO().FlushOutput(call.receiver, true);
                                       } catch (const IoRuntimeError& error) {
                                           IoFailure(error);
                                       }
                                       const auto descriptor = typed.GetRef(fd);
                                       if (typed.GetInt(should_close) != 0) {
                                           if (descriptor.IsValid()) call.vm.IO().CloseDescriptor(descriptor);
                                       } else {
                                           typed.SetRef(fd, call.vm.NewIntrinsicInstance(
                                                            "Ljava/io/FileDescriptor;"));
                                       }
                                       return VmValue::Void();
                                   });
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclareFileWriter() {
            auto builder =
                    IntrinsicClassBuilder::Class("Ljava/io/FileWriter;", "Ljava/io/Writer;");
            builder.Constructor("(Ljava/io/File;Z)V", [](IntrinsicContext& call) {
                const auto ctor = call.vm.Linker().FindDirectMethod(
                    call.vm.Linker().ResolveDescriptor("Ljava/io/Writer;"), "<init>", "()V");
                const std::array args{VmValue::Ref(call.receiver)};
                const auto result = call.vm.Call(*ctor, args);
                if (result.exception.IsValid()) throw VmJavaThrow{
                    call.vm.Linker().Class(result.exception_class).descriptor,
                    result.exception_message, result.exception};
                const auto path = FilePath(call, call.arguments[0].ref);
                IoRuntime::OutputState output{path, {}, true, false};
                if (call.arguments[1].AsInt() != 0) {
                    if (const auto existing = call.vm.IO().ReadFile(path)) {
                        output.bytes = *existing;
                    }
                }
                call.vm.IO().SetOutput(call.receiver, std::move(output));
                return VmValue::Void();
            });
            builder.FinalMethod(
                "append", "(C)Ljava/io/Writer;", [](IntrinsicContext& call) {
                    IoRuntime::OutputState* output{};
                    try {
                        output = &call.vm.IO().Output(call.receiver);
                    } catch (const IoRuntimeError& error) {
                        IoFailure(error);
                    }
                    const auto unit =
                            static_cast<char16_t>(call.arguments[0].cat1 & 0xffffU);
                    const auto encoded = unit < 0x80U
                                             ? std::string(1, static_cast<char>(unit))
                                             : call.vm.StringUtf8(call.vm.Model().NewString(
                                                 std::u16string(1, unit)));
                    for (const auto value: encoded) {
                        output->bytes.push_back(static_cast<std::byte>(value));
                    }
                    return VmValue::Ref(call.receiver);
                });
            builder.FinalMethod(
                "append", "(Ljava/lang/CharSequence;)Ljava/io/Writer;",
                [](IntrinsicContext& call) {
                    IoRuntime::OutputState* output{};
                    try {
                        output = &call.vm.IO().Output(call.receiver);
                    } catch (const IoRuntimeError& error) {
                        IoFailure(error);
                    }
                    const auto value = call.arguments[0].ref;
                    const auto text =
                            value.IsValid() ? call.vm.StringUtf8(value) : std::string("null");
                    for (const auto character: text) {
                        output->bytes.push_back(static_cast<std::byte>(character));
                    }
                    return VmValue::Ref(call.receiver);
                });
            builder.FinalMethod("flush", "()V", Flush(false));
            builder.FinalMethod("close", "()V", Flush(true));
            return std::move(builder).Build();
        }
    } // namespace

    void AppendJavaIoFiles(std::vector<IntrinsicClassDecl>& catalog) {
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


// ---- migrated from java_io_IOException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    IntrinsicClassDecl Declare_java_io_IOException() {
        return DeclareSimpleThrowable("Ljava/io/IOException;", "Ljava/lang/Exception;");
    }
} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_PrintStream.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    IntrinsicClassDecl Declare_java_io_PrintStream() {
        auto builder = IntrinsicClassBuilder::Class("Ljava/io/PrintStream;", "Ljava/lang/Object;");
        builder.FinalMethod("println", "(Ljava/lang/String;)V",
                            [](IntrinsicContext& context) {
                                const auto argument = context.arguments[0].ref;
                                GuestLine(context, argument.IsValid()
                                                       ? Narrow(Value(context, argument))
                                                       : std::string("null"));
                                return VmValue::Void();
                            });
        builder.FinalMethod("println", "(I)V",
                            [](IntrinsicContext& context) {
                                GuestLine(context, std::to_string(context.arguments[0].AsInt()));
                                return VmValue::Void();
                            });
        builder.FinalMethod("println", "()V",
                            [](IntrinsicContext& context) {
                                GuestLine(context, "");
                                return VmValue::Void();
                            });
        builder.FinalMethod("print", "(Ljava/lang/String;)V",
                            [](IntrinsicContext& context) {
                                const auto argument = context.arguments[0].ref;
                                GuestLine(context, argument.IsValid()
                                                       ? Narrow(Value(context, argument))
                                                       : std::string("null"));
                                return VmValue::Void();
                            });
        auto result = std::move(builder).Build();
        return result;
    }
} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_Serializable.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;


} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_streams.cpp ----
#include "catalog.h"
#include "shared.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/jni/jni_utf.h"

namespace ogplay::runtime::dexvm::intrinsics {
    namespace {
        [[nodiscard]] IoRuntime::InputState& Input(IntrinsicContext& call) {
            try {
                return call.vm.IO().Input(call.receiver);
            } catch (const IoRuntimeError& error) {
                IoFailure(error);
            }
        }

        [[nodiscard]] IoRuntime::OutputState& Output(IntrinsicContext& call) {
            try {
                return call.vm.IO().Output(call.receiver);
            } catch (const IoRuntimeError& error) {
                IoFailure(error);
            }
        }

        [[nodiscard]] IoRuntime::ObjectInputState& ObjectInput(
            IntrinsicContext& call) {
            try {
                return call.vm.IO().ObjectInput(call.receiver);
            } catch (const IoRuntimeError& error) {
                IoFailure(error);
            }
        }

        bool EnsureInput(IntrinsicContext& call, std::size_t count, bool blocking = true) {
            auto& input = Input(call);
            if (input.closed) throw VmJavaThrow{"Ljava/io/IOException;", "stream is closed"};
            while (input.bytes.size() - input.cursor < count) {
                if (!input.source.IsValid()) return false;
                if (!blocking && detail::InvokeGuest(call.vm, input.source, "available", "()I").AsInt() <= 0) return false;
                const auto value = detail::InvokeGuest(call.vm, input.source, "read", "()I").AsInt();
                if (value < 0) return false;
                input.bytes.push_back(static_cast<std::byte>(value));
            }
            return true;
        }

        void DeliverOutput(IntrinsicContext& call, bool flush, bool close) {
            auto& output = Output(call);
            if (!output.sink.IsValid()) { call.vm.IO().FlushOutput(call.receiver, close); return; }
            if (output.closed) throw VmJavaThrow{"Ljava/io/IOException;", "stream is closed"};
            while (output.delivered < output.bytes.size()) {
                const auto count = std::min<std::size_t>(8192, output.bytes.size() - output.delivered);
                const auto array = call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte, static_cast<JniSize>(count));
                call.vm.Model().WriteByteRegion(array, 0, std::span(output.bytes).subspan(output.delivered, count));
                detail::InvokeGuest(call.vm, output.sink, "write", "([BII)V", {VmValue::Ref(array), VmValue::Int(0), VmValue::Int(static_cast<std::int32_t>(count))});
                output.delivered += count;
            }
            if (flush) detail::InvokeGuest(call.vm, output.sink, "flush", "()V");
            if (close) { detail::InvokeGuest(call.vm, output.sink, "close", "()V"); output.closed = true; }
        }

        IntrinsicHandler FlushOutput(const bool close) {
            return [close](IntrinsicContext& call) {
                const auto* state = call.vm.IO().FindOutput(call.receiver);
                if (close && state && state->closed) return VmValue::Void();
                try { DeliverOutput(call, true, close); }
                catch (const IoRuntimeError& error) { IoFailure(error); }
                return VmValue::Void();
            };
        }

        constexpr std::uint8_t kStreamMagicHigh = 0xacU;
        constexpr std::uint8_t kStreamMagicLow = 0xedU;
        constexpr std::uint8_t kStreamVersionHigh = 0x00U;
        constexpr std::uint8_t kStreamVersionLow = 0x05U;
        constexpr std::uint8_t kTcNull = 0x70U;
        constexpr std::uint8_t kTcReference = 0x71U;
        constexpr std::uint8_t kTcClassDesc = 0x72U;
        constexpr std::uint8_t kTcObject = 0x73U;
        constexpr std::uint8_t kTcBlockData = 0x77U;
        constexpr std::uint8_t kTcEndBlockData = 0x78U;
        constexpr std::uint8_t kTcReset = 0x79U;
        constexpr std::uint8_t kTcBlockDataLong = 0x7aU;
        constexpr std::uint8_t kTcString = 0x74U;
        constexpr std::uint8_t kTcLongString = 0x7cU;
        constexpr std::uint8_t kTcEnum = 0x7eU;
        constexpr std::uint8_t kScWriteMethod = 0x01U;
        constexpr std::uint8_t kScSerializable = 0x02U;
        constexpr std::uint8_t kScExternalizable = 0x04U;
        constexpr std::uint8_t kScBlockData = 0x08U;
        constexpr std::uint8_t kScEnum = 0x10U;
        constexpr std::uint32_t kBaseWireHandle = 0x007e0000U;
        constexpr std::size_t kMaximumObjectDepth = 256U;
        constexpr std::size_t kMaximumObjectHandles = 1U << 20U;
        constexpr std::size_t kMaximumSerializedFields = 4096U;

        void AppendBigEndian(std::vector<std::byte>& bytes, const std::uint64_t value,
                             const std::size_t width) {
            for (std::size_t index = width; index > 0; --index) {
                const auto shift = static_cast<unsigned>((index - 1U) * 8U);
                bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
            }
        }

        [[nodiscard]] std::vector<std::uint8_t>
        ModifiedUtf8(IntrinsicContext& call, const VmObjectRef string) {
            if (!string.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/NullPointerException;", "string == null"};
            }
            const auto value = call.vm.Model().StringValue(string);
            std::vector<JniChar> units;
            units.reserve(value.size());
            for (const auto unit: value) {
                units.push_back(static_cast<JniChar>(unit));
            }
            return EncodeJniModifiedUtf8(units);
        }

        [[nodiscard]] VmObjectRef DecodeModifiedUtf8(
            IntrinsicContext& call, const std::span<const std::byte> bytes) {
            std::vector<std::uint8_t> encoded;
            encoded.reserve(bytes.size());
            for (const auto byte: bytes) {
                encoded.push_back(static_cast<std::uint8_t>(byte));
            }
            try {
                const auto decoded = DecodeJniModifiedUtf8(encoded);
                std::u16string value;
                value.reserve(decoded.size());
                for (const auto unit: decoded) {
                    value.push_back(static_cast<char16_t>(unit));
                }
                return call.vm.Model().NewString(value);
            } catch (const JniModifiedUtf8Error& error) {
                throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
            }
        }

        void AppendObjectPrimitiveBlock(IntrinsicContext& call,
                                        const std::span<const std::byte> payload) {
            constexpr std::size_t kMaximumBlock = 1024U;
            auto& bytes = Output(call).bytes;
            std::size_t offset{};
            while (offset < payload.size()) {
                const auto count = std::min(kMaximumBlock, payload.size() - offset);
                if (count < 256U) {
                    bytes.push_back(static_cast<std::byte>(kTcBlockData));
                    bytes.push_back(static_cast<std::byte>(count));
                } else {
                    bytes.push_back(static_cast<std::byte>(kTcBlockDataLong));
                    AppendBigEndian(bytes, count, 4U);
                }
                bytes.insert(bytes.end(), payload.begin() +
                                          static_cast<std::ptrdiff_t>(offset),
                             payload.begin() +
                             static_cast<std::ptrdiff_t>(offset + count));
                offset += count;
            }
            DeliverOutput(call, false, false);
        }

        void AppendObjectPrimitiveInteger(IntrinsicContext& call,
                                          const std::uint64_t value,
                                          const std::size_t width) {
            std::vector<std::byte> payload;
            payload.reserve(width);
            AppendBigEndian(payload, value, width);
            AppendObjectPrimitiveBlock(call, payload);
        }

        [[nodiscard]] std::uint64_t TakeRawUnsigned(IntrinsicContext& call,
                                                    const std::size_t count) {
            auto& input = Input(call);
            if (count > 8U || !EnsureInput(call, count)) {
                throw VmJavaThrow{"Ljava/io/EOFException;", "end of object stream"};
            }
            std::uint64_t value{};
            for (std::size_t index = 0; index < count; ++index) {
                value = (value << 8U) |
                        static_cast<std::uint8_t>(input.bytes[input.cursor++]);
            }
            return value;
        }

        [[nodiscard]] std::span<const std::byte>
        TakeRawBytes(IntrinsicContext& call, const std::size_t count) {
            auto& input = Input(call);
            if (!EnsureInput(call, count)) {
                throw VmJavaThrow{"Ljava/io/EOFException;", "end of object stream"};
            }
            const auto offset = input.cursor;
            input.cursor += count;
            return std::span(input.bytes).subspan(offset, count);
        }

        [[nodiscard]] IoRuntime::ObjectOutputState& ObjectOutput(
            IntrinsicContext& call) {
            try {
                return call.vm.IO().ObjectOutput(call.receiver);
            } catch (const IoRuntimeError& error) {
                IoFailure(error);
            }
        }

        struct ObjectStreamCallbackFailure final { VmObjectRef throwable; };

        void PropagateNestedOutcome(IntrinsicContext&, const VmCallOutcome& outcome) {
            if (outcome.exception.IsValid()) throw ObjectStreamCallbackFailure{outcome.exception};
        }

        [[nodiscard]] std::string StreamClassName(const std::string_view descriptor) {
            std::string result;
            if (descriptor.starts_with('L') && descriptor.ends_with(';')) {
                result.assign(descriptor.substr(1U, descriptor.size() - 2U));
            } else {
                result.assign(descriptor);
            }
            std::replace(result.begin(), result.end(), '/', '.');
            return result;
        }

        [[nodiscard]] std::string DescriptorFromStreamName(
            const std::string_view name) {
            std::string result(name);
            std::replace(result.begin(), result.end(), '.', '/');
            if (!result.starts_with('[')) result = 'L' + result + ';';
            return result;
        }

        void AppendRawModifiedUtf(IntrinsicContext& call,
                                  const std::string_view text) {
            const auto string = call.vm.NewStringUtf8(text);
            const std::array roots{string};
            const auto root_scope = call.vm.ProtectReferences(roots);
            const auto encoded = ModifiedUtf8(call, string);
            if (encoded.size() > std::numeric_limits<std::uint16_t>::max()) {
                throw VmJavaThrow{
                    "Ljava/io/IOException;",
                    "serialized UTF value is too long"
                };
            }
            auto& bytes = Output(call).bytes;
            AppendBigEndian(bytes, encoded.size(), 2U);
            for (const auto byte: encoded) {
                bytes.push_back(static_cast<std::byte>(byte));
            }
        }

        [[nodiscard]] std::string TakeRawModifiedUtf(IntrinsicContext& call) {
            const auto length = static_cast<std::size_t>(TakeRawUnsigned(call, 2U));
            const auto string = DecodeModifiedUtf8(call, TakeRawBytes(call, length));
            return call.vm.StringUtf8(string);
        }

        [[nodiscard]] std::optional<VmFieldId> FindOwnField(
            DexClassLinker& linker, const DexClassId owner, const std::string_view name,
            const std::string_view descriptor) {
            for (const auto field_id: linker.Class(owner).own_instance_fields) {
                const auto& field = linker.Field(field_id);
                if (field.name == name && field.descriptor == descriptor &&
                    !field.is_static) {
                    return field_id;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] bool IsEnumClass(DexClassLinker& linker,
                                       const DexClassId java_class) {
            auto current = std::optional<DexClassId>(java_class);
            while (current.has_value()) {
                const auto& linked = linker.Class(*current);
                if ((linked.access_flags & kAccEnum) != 0U) return true;
                if (linked.descriptor == "Ljava/lang/Enum;") return true;
                current = linked.super;
            }
            return false;
        }

        [[nodiscard]] std::optional<std::int64_t> KnownSerialVersionUid(
            const std::string_view descriptor) {
            static const std::unordered_map<std::string_view, std::int64_t> known{
                {"Ljava/lang/Number;", -8742448824652078965LL},
                {"Ljava/lang/Byte;", -7183698231559129828LL},
                {"Ljava/lang/Short;", 7515723908773894738LL},
                {"Ljava/lang/Integer;", 1360826667806852920LL},
                {"Ljava/lang/Long;", 4290774380558885855LL},
                {"Ljava/lang/Float;", -2671257302660747028LL},
                {"Ljava/lang/Double;", -9172774392245257468LL},
                {"Ljava/lang/Boolean;", -3665804199014368530LL},
                {"Ljava/lang/Character;", 3786198910865385080LL},
                {"Ljava/util/Date;", 7523967970034938905LL},
                {"Ljava/lang/Enum;", 0LL},
            };
            const auto found = known.find(descriptor);
            return found == known.end()
                       ? std::nullopt
                       : std::optional<std::int64_t>(found->second);
        }

        [[nodiscard]] std::int64_t SerialVersionUid(IntrinsicContext& call,
                                                    const DexClassId java_class) {
            const auto descriptor = call.vm.Linker().Class(java_class).descriptor;
            if (IsEnumClass(call.vm.Linker(), java_class)) return 0;
            if (const auto known = KnownSerialVersionUid(descriptor);
                known.has_value()) {
                return *known;
            }
            const auto field = call.vm.Linker().FindFieldRecursive(
                java_class, "serialVersionUID", "J");
            if (field.has_value()) {
                const auto& linked = call.vm.Linker().Field(*field);
                if (linked.owner == java_class && linked.is_static &&
                    (linked.access_flags & kAccFinal) != 0U) {
                    PropagateNestedOutcome(call,
                                           call.vm.EnsureClassInitialized(java_class));
                    const auto& storage =
                            call.vm.Linker().Class(java_class).static_storage;
                    const auto bits = static_cast<std::uint64_t>(storage[linked.slot]) |
                                      (static_cast<std::uint64_t>(
                                           storage[linked.slot + 1U])
                                       << 32U);
                    return static_cast<std::int64_t>(bits);
                }
            }
            throw VmJavaThrow{
                "Ljava/io/IOException;",
                "default serialVersionUID computation is unsupported for " +
                descriptor
            };
        }

        [[nodiscard]] std::vector<IoRuntime::SerializedFieldDescriptor>
        SerializableFields(IntrinsicContext& call, const DexClassId java_class) {
            std::vector<IoRuntime::SerializedFieldDescriptor> fields;
            for (const auto field_id:
                 call.vm.Linker().Class(java_class).own_instance_fields) {
                const auto& field = call.vm.Linker().Field(field_id);
                if (field.is_static || (field.access_flags & kAccTransient) != 0U) {
                    continue;
                }
                fields.push_back({field.descriptor.front(), field.name, field.descriptor});
            }
            std::sort(fields.begin(), fields.end(), [](const auto& left,
                                                       const auto& right) {
                const auto left_primitive = left.type_code != 'L' &&
                                            left.type_code != '[';
                const auto right_primitive = right.type_code != 'L' &&
                                             right.type_code != '[';
                if (left_primitive != right_primitive) return left_primitive;
                return left.name < right.name;
            });
            return fields;
        }

        [[nodiscard]] bool HasCustomWriteObject(IntrinsicContext& call,
                                                const DexClassId java_class) {
            return call.vm.Linker()
                    .FindDirectMethod(java_class, "writeObject",
                                      "(Ljava/io/ObjectOutputStream;)V")
                    .has_value();
        }

        [[nodiscard]] std::shared_ptr<IoRuntime::SerializedClassDescriptor>
        BuildStreamClassDescriptor(IntrinsicContext& call,
                                   const DexClassId java_class,
                                   const bool enum_descriptor = false) {
            const auto descriptor = call.vm.Linker().Class(java_class).descriptor;
            auto result = std::make_shared<IoRuntime::SerializedClassDescriptor>();
            result->descriptor = descriptor;
            result->runtime_class = java_class.Value();
            result->serial_version_uid = SerialVersionUid(call, java_class);
            if (enum_descriptor || IsEnumClass(call.vm.Linker(), java_class)) {
                result->flags = kScSerializable | kScEnum;
            } else if (call.vm.Linker().IsAssignable(
                           call.vm.Linker().ResolveDescriptor("Ljava/io/Externalizable;"),
                           java_class)) {
                result->flags = kScExternalizable | kScBlockData;
            } else if (descriptor == "Ljava/util/Date;") {
                result->flags = kScSerializable | kScWriteMethod;
            } else {
                if (HasCustomWriteObject(call, java_class)) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "custom writeObject is unsupported for " + descriptor
                    };
                }
                result->flags = kScSerializable;
                result->fields = SerializableFields(call, java_class);
            }

            const auto super = call.vm.Linker().Class(java_class).super;
            if (!super.has_value()) return result;
            if ((result->flags & kScEnum) != 0U) {
                if (call.vm.Linker().Class(*super).descriptor !=
                    "Ljava/lang/Object;") {
                    result->super = BuildStreamClassDescriptor(call, *super, true);
                }
                return result;
            }
            const auto serializable =
                    call.vm.Linker().ResolveDescriptor("Ljava/io/Serializable;");
            if (call.vm.Linker().IsAssignable(serializable, *super)) {
                result->super = BuildStreamClassDescriptor(call, *super);
            }
            return result;
        }

        [[nodiscard]] std::uint64_t ReadInstanceFieldBits(
            IntrinsicContext& call, const VmObjectRef object,
            const VmFieldId field_id) {
            const auto& field = call.vm.Linker().Field(field_id);
            const auto slots = call.vm.Model().InstanceSlots(object);
            auto bits = static_cast<std::uint64_t>(slots[field.slot].bits);
            if (field.is_wide) {
                bits |= static_cast<std::uint64_t>(slots[field.slot + 1U].bits) << 32U;
            }
            return bits;
        }

        void WriteInstanceFieldBits(IntrinsicContext& call,
                                    const VmObjectRef object,
                                    const VmFieldId field_id,
                                    const std::uint64_t bits) {
            const auto& field = call.vm.Linker().Field(field_id);
            auto slots = call.vm.Model().InstanceSlots(object);
            slots[field.slot] = {
                static_cast<std::uint32_t>(bits),
                field.is_ref
                    ? SlotTag::ref
                    : field.is_wide
                          ? SlotTag::wide_lo
                          : SlotTag::cat1
            };
            if (field.is_wide) {
                slots[field.slot + 1U] = {
                    static_cast<std::uint32_t>(bits >> 32U),
                    SlotTag::wide_hi
                };
            }
        }

        [[nodiscard]] std::size_t PrimitiveFieldWidth(const char type_code) {
            switch (type_code) {
                case 'B':
                case 'Z': return 1U;
                case 'C':
                case 'S': return 2U;
                case 'F':
                case 'I': return 4U;
                case 'D':
                case 'J': return 8U;
                default: return 0U;
            }
        }

        void InvokeExternal(IntrinsicContext& call, VmObjectRef object, bool write) {
            const auto java_class = call.vm.Model().ObjectClass(object);
            const auto method = call.vm.Linker().FindVtableIndex(
                java_class, write ? "writeExternal" : "readExternal",
                write ? "(Ljava/io/ObjectOutput;)V" : "(Ljava/io/ObjectInput;)V");
            if (!method) throw VmJavaThrow{"Ljava/io/InvalidClassException;",
                                           "Externalizable callback is missing"};
            const std::array args{VmValue::Ref(object), VmValue::Ref(call.receiver)};
            PropagateNestedOutcome(call, call.vm.Call(
                call.vm.Linker().Class(java_class).vtable[*method], args));
        }

        class ObjectStreamWriter final {
        public:
            explicit ObjectStreamWriter(IntrinsicContext& call)
                : call_(call), state_(ObjectOutput(call)) {
            }

            void Write(const VmObjectRef object) {
                if (state_.depth >= kMaximumObjectDepth) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "serialized object graph is too deep"
                    };
                }
                ++state_.depth;
                try {
                    WriteInternal(object);
                    --state_.depth;
                } catch (...) {
                    --state_.depth;
                    throw;
                }
            }

        private:
            [[nodiscard]] std::uint32_t RegisterObject(const VmObjectRef object) {
                if (state_.next_handle - kBaseWireHandle >= kMaximumObjectHandles) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "serialized object graph has too many handles"
                    };
                }
                const auto handle = state_.next_handle++;
                state_.object_handles.emplace(object.Value(), handle);
                state_.handle_objects.push_back(object);
                return handle;
            }

            void WriteReference(const std::uint32_t handle) {
                auto& bytes = Output(call_).bytes;
                bytes.push_back(static_cast<std::byte>(kTcReference));
                AppendBigEndian(bytes, handle, 4U);
            }

            void WriteString(const VmObjectRef string) {
                if (const auto found = state_.object_handles.find(string.Value());
                    found != state_.object_handles.end()) {
                    WriteReference(found->second);
                    return;
                }
                const auto encoded = ModifiedUtf8(call_, string);
                auto& bytes = Output(call_).bytes;
                bytes.push_back(static_cast<std::byte>(
                    encoded.size() <= std::numeric_limits<std::uint16_t>::max()
                        ? kTcString
                        : kTcLongString));
                static_cast<void>(RegisterObject(string));
                AppendBigEndian(bytes, encoded.size(),
                                encoded.size() <=
                                std::numeric_limits<std::uint16_t>::max()
                                    ? 2U
                                    : 8U);
                for (const auto byte: encoded) {
                    bytes.push_back(static_cast<std::byte>(byte));
                }
            }

            void WriteClassDescriptor(
                const std::shared_ptr<IoRuntime::SerializedClassDescriptor>& descriptor) {
                if (descriptor == nullptr) {
                    Output(call_).bytes.push_back(static_cast<std::byte>(kTcNull));
                    return;
                }
                if (const auto found =
                            state_.class_handles.find(descriptor->runtime_class);
                    found != state_.class_handles.end()) {
                    WriteReference(found->second);
                    return;
                }
                if (state_.next_handle - kBaseWireHandle >= kMaximumObjectHandles) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "serialized object graph has too many handles"
                    };
                }
                state_.class_handles.emplace(descriptor->runtime_class,
                                             state_.next_handle++);
                auto& bytes = Output(call_).bytes;
                bytes.push_back(static_cast<std::byte>(kTcClassDesc));
                AppendRawModifiedUtf(call_, StreamClassName(descriptor->descriptor));
                AppendBigEndian(bytes,
                                static_cast<std::uint64_t>(
                                    descriptor->serial_version_uid),
                                8U);
                bytes.push_back(static_cast<std::byte>(descriptor->flags));
                AppendBigEndian(bytes, descriptor->fields.size(), 2U);
                for (const auto& field: descriptor->fields) {
                    bytes.push_back(static_cast<std::byte>(field.type_code));
                    AppendRawModifiedUtf(call_, field.name);
                    if (field.type_code == 'L' || field.type_code == '[') {
                        const auto type_string = call_.vm.NewStringUtf8(field.descriptor);
                        const std::array roots{type_string};
                        const auto root_scope = call_.vm.ProtectReferences(roots);
                        Write(type_string);
                    }
                }
                bytes.push_back(static_cast<std::byte>(kTcEndBlockData));
                WriteClassDescriptor(descriptor->super);
            }

            void WriteClassData(
                const VmObjectRef object,
                const std::shared_ptr<IoRuntime::SerializedClassDescriptor>& descriptor) {
                if ((descriptor->flags & kScExternalizable) != 0U) {
                    InvokeExternal(call_, object, true);
                    Output(call_).bytes.push_back(static_cast<std::byte>(kTcEndBlockData));
                    return;
                }
                if (descriptor->super != nullptr) {
                    WriteClassData(object, descriptor->super);
                }
                const auto owner = DexClassId(descriptor->runtime_class);
                for (const auto& field: descriptor->fields) {
                    const auto local = FindOwnField(call_.vm.Linker(), owner, field.name,
                                                    field.descriptor);
                    if (!local.has_value()) {
                        throw VmJavaThrow{
                            "Ljava/io/IOException;",
                            "serialized field disappeared: " + field.name
                        };
                    }
                    if (field.type_code == 'L' || field.type_code == '[') {
                        Write(VmObjectRef(static_cast<std::uint32_t>(
                            ReadInstanceFieldBits(call_, object, *local))));
                    } else {
                        AppendBigEndian(Output(call_).bytes,
                                        ReadInstanceFieldBits(call_, object, *local),
                                        PrimitiveFieldWidth(field.type_code));
                    }
                }
                if (descriptor->descriptor == "Ljava/util/Date;") {
                    const auto millis = call_.vm.Linker().FindFieldRecursive(
                        owner, "milliseconds", "J");
                    if (!millis.has_value()) {
                        throw VmJavaThrow{
                            "Ljava/io/IOException;",
                            "Date milliseconds field is unavailable"
                        };
                    }
                    std::vector<std::byte> payload;
                    AppendBigEndian(payload, ReadInstanceFieldBits(call_, object, *millis),
                                    8U);
                    AppendObjectPrimitiveBlock(call_, payload);
                    Output(call_).bytes.push_back(
                        static_cast<std::byte>(kTcEndBlockData));
                }
            }

            void WriteEnum(const VmObjectRef object, const DexClassId java_class) {
                Output(call_).bytes.push_back(static_cast<std::byte>(kTcEnum));
                const auto descriptor =
                        BuildStreamClassDescriptor(call_, java_class, true);
                WriteClassDescriptor(descriptor);
                static_cast<void>(RegisterObject(object));
                const auto name = call_.vm.Linker().FindFieldRecursive(
                    java_class, "name", "Ljava/lang/String;");
                if (!name.has_value()) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "enum name field is unavailable"
                    };
                }
                Write(VmObjectRef(static_cast<std::uint32_t>(
                    ReadInstanceFieldBits(call_, object, *name))));
            }

            void WriteInternal(const VmObjectRef object) {
                if (!object.IsValid()) {
                    Output(call_).bytes.push_back(static_cast<std::byte>(kTcNull));
                    return;
                }
                if (const auto found = state_.object_handles.find(object.Value());
                    found != state_.object_handles.end()) {
                    WriteReference(found->second);
                    return;
                }
                const auto java_class = call_.vm.Model().ObjectClass(object);
                const auto string_class =
                        call_.vm.Linker().ResolveDescriptor("Ljava/lang/String;");
                if (java_class == string_class) {
                    WriteString(object);
                    return;
                }
                if (IsEnumClass(call_.vm.Linker(), java_class)) {
                    WriteEnum(object, java_class);
                    return;
                }
                const auto is_array = call_.vm.Linker().Class(java_class).is_array;
                const auto descriptor =
                        call_.vm.Linker().Class(java_class).descriptor;
                if (is_array) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "array serialization is unsupported"
                    };
                }
                const auto serializable =
                        call_.vm.Linker().ResolveDescriptor("Ljava/io/Serializable;");
                if (!call_.vm.Linker().IsAssignable(serializable, java_class)) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "object is not serializable: " + descriptor
                    };
                }
                const auto class_descriptor =
                        BuildStreamClassDescriptor(call_, java_class);
                Output(call_).bytes.push_back(static_cast<std::byte>(kTcObject));
                WriteClassDescriptor(class_descriptor);
                static_cast<void>(RegisterObject(object));
                WriteClassData(object, class_descriptor);
            }

            IntrinsicContext& call_;
            IoRuntime::ObjectOutputState& state_;
        };

        class ObjectStreamReader final {
        public:
            explicit ObjectStreamReader(IntrinsicContext& call)
                : call_(call), state_(ObjectInput(call)) {
            }

            [[nodiscard]] VmObjectRef Read() {
                if (state_.depth >= kMaximumObjectDepth) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "serialized object graph is too deep"
                    };
                }
                ++state_.depth;
                try {
                    const auto result = ReadInternal();
                    --state_.depth;
                    return result;
                } catch (...) {
                    --state_.depth;
                    throw;
                }
            }

        private:
            [[nodiscard]] std::size_t RegisterHandle(
                IoRuntime::ObjectInputHandle handle) {
                if (state_.handles.size() >= kMaximumObjectHandles) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "serialized object graph has too many handles"
                    };
                }
                state_.handles.push_back(std::move(handle));
                return state_.handles.size() - 1U;
            }

            [[nodiscard]] const IoRuntime::ObjectInputHandle& TakeReference() {
                const auto handle = static_cast<std::uint32_t>(TakeRawUnsigned(call_, 4U));
                if (handle < kBaseWireHandle ||
                    handle - kBaseWireHandle >= state_.handles.size()) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "invalid object stream reference"
                    };
                }
                return state_.handles[handle - kBaseWireHandle];
            }

            [[nodiscard]] VmObjectRef ReadString(const std::uint8_t token) {
                const auto length = token == kTcString
                                        ? TakeRawUnsigned(call_, 2U)
                                        : TakeRawUnsigned(call_, 8U);
                if (length > std::numeric_limits<std::size_t>::max()) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "object stream string is too large"
                    };
                }
                const auto string = DecodeModifiedUtf8(
                    call_, TakeRawBytes(call_, static_cast<std::size_t>(length)));
                static_cast<void>(RegisterHandle({string, nullptr}));
                return string;
            }

            [[nodiscard]] DexClassId ResolveStreamClass(
                const std::string_view stream_name) {
                const auto descriptor = DescriptorFromStreamName(stream_name);
                try {
                    return call_.vm.Linker().ResolveDescriptor(descriptor);
                } catch (const DexVmError&) {
                    throw VmJavaThrow{"Ljava/lang/ClassNotFoundException;", descriptor};
                }
            }

            void DiscardClassAnnotation() {
                while (true) {
                    const auto token =
                            static_cast<std::uint8_t>(TakeRawUnsigned(call_, 1U));
                    if (token == kTcEndBlockData) return;
                    std::size_t length{};
                    if (token == kTcBlockData) {
                        length = static_cast<std::size_t>(TakeRawUnsigned(call_, 1U));
                    } else if (token == kTcBlockDataLong) {
                        length = static_cast<std::size_t>(TakeRawUnsigned(call_, 4U));
                    } else {
                        // Unread callback objects still participate in the shared
                        // handle table, including references from later objects.
                        --Input(call_).cursor;
                        static_cast<void>(Read());
                        continue;
                    }
                    static_cast<void>(TakeRawBytes(call_, length));
                }
            }

            [[nodiscard]] std::shared_ptr<IoRuntime::SerializedClassDescriptor>
            ReadClassDescriptor() {
                const auto token =
                        static_cast<std::uint8_t>(TakeRawUnsigned(call_, 1U));
                if (token == kTcNull) return {};
                if (token == kTcReference) {
                    const auto& handle = TakeReference();
                    if (handle.class_descriptor == nullptr) {
                        throw VmJavaThrow{
                            "Ljava/io/IOException;",
                            "object reference used as a class descriptor"
                        };
                    }
                    return handle.class_descriptor;
                }
                if (token != kTcClassDesc) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "invalid object stream class descriptor"
                    };
                }
                const auto name = TakeRawModifiedUtf(call_);
                const auto java_class = ResolveStreamClass(name);
                auto descriptor =
                        std::make_shared<IoRuntime::SerializedClassDescriptor>();
                descriptor->descriptor = DescriptorFromStreamName(name);
                descriptor->runtime_class = java_class.Value();
                descriptor->serial_version_uid = static_cast<std::int64_t>(
                    TakeRawUnsigned(call_, 8U));
                const auto handle_index = RegisterHandle({VmObjectRef{0}, descriptor});
                descriptor->flags =
                        static_cast<std::uint8_t>(TakeRawUnsigned(call_, 1U));
                const auto field_count =
                        static_cast<std::size_t>(TakeRawUnsigned(call_, 2U));
                if (field_count > kMaximumSerializedFields) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "object stream class has too many fields"
                    };
                }
                descriptor->fields.reserve(field_count);
                for (std::size_t index = 0; index < field_count; ++index) {
                    IoRuntime::SerializedFieldDescriptor field;
                    field.type_code =
                            static_cast<char>(TakeRawUnsigned(call_, 1U));
                    field.name = TakeRawModifiedUtf(call_);
                    if (field.type_code == 'L' || field.type_code == '[') {
                        const auto type_string = Read();
                        const auto string_class =
                                call_.vm.Linker().ResolveDescriptor("Ljava/lang/String;");
                        if (!type_string.IsValid() ||
                            call_.vm.Model().ObjectClass(type_string) != string_class) {
                            throw VmJavaThrow{
                                "Ljava/io/IOException;",
                                "invalid serialized field type"
                            };
                        }
                        field.descriptor = call_.vm.StringUtf8(type_string);
                    } else {
                        if (PrimitiveFieldWidth(field.type_code) == 0U) {
                            throw VmJavaThrow{
                                "Ljava/io/IOException;",
                                "invalid serialized primitive field type"
                            };
                        }
                        field.descriptor.assign(1U, field.type_code);
                    }
                    descriptor->fields.push_back(std::move(field));
                }
                DiscardClassAnnotation();
                descriptor->super = ReadClassDescriptor();
                static_cast<void>(handle_index);
                VerifyClassDescriptor(*descriptor, java_class);
                return descriptor;
            }

            void VerifyClassDescriptor(
                const IoRuntime::SerializedClassDescriptor& descriptor,
                const DexClassId java_class) {
                const auto externalizable = call_.vm.Linker().IsAssignable(
                    call_.vm.Linker().ResolveDescriptor("Ljava/io/Externalizable;"), java_class);
                const bool external = (descriptor.flags & kScExternalizable) != 0U;
                if (external != externalizable ||
                    (external && ((descriptor.flags & kScBlockData) == 0U ||
                                  (descriptor.flags & kScSerializable) != 0U ||
                                  !descriptor.fields.empty())) ||
                    (!external && (descriptor.flags & kScSerializable) == 0U)) {
                    throw VmJavaThrow{"Ljava/io/InvalidClassException;",
                                      "incompatible serialization protocol"};
                }
                const auto serializable =
                        call_.vm.Linker().ResolveDescriptor("Ljava/io/Serializable;");
                if (!call_.vm.Linker().IsAssignable(serializable, java_class)) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "local class is not serializable: " +
                        descriptor.descriptor
                    };
                }
                if (descriptor.serial_version_uid !=
                    SerialVersionUid(call_, java_class)) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "serialVersionUID mismatch for " +
                        descriptor.descriptor
                    };
                }
                if ((descriptor.flags & kScWriteMethod) != 0U &&
                    descriptor.descriptor != "Ljava/util/Date;") {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "custom readObject is unsupported for " +
                        descriptor.descriptor
                    };
                }
            }

            void ReadClassData(
                const VmObjectRef object,
                const std::shared_ptr<IoRuntime::SerializedClassDescriptor>& descriptor) {
                if ((descriptor->flags & kScExternalizable) != 0U) {
                    InvokeExternal(call_, object, false);
                    static_cast<void>(TakeRawBytes(call_, state_.block_remaining));
                    state_.block_remaining = 0;
                    state_.pushback.reset();
                    DiscardClassAnnotation();
                    return;
                }
                if (descriptor->super != nullptr) {
                    ReadClassData(object, descriptor->super);
                }
                const auto owner = DexClassId(descriptor->runtime_class);
                for (const auto& field: descriptor->fields) {
                    const auto local = FindOwnField(call_.vm.Linker(), owner, field.name,
                                                    field.descriptor);
                    if (field.type_code == 'L' || field.type_code == '[') {
                        const auto value = Read();
                        if (local.has_value()) {
                            if (value.IsValid()) {
                                const auto target =
                                        call_.vm.Linker().ResolveDescriptor(field.descriptor);
                                if (!call_.vm.Linker().IsAssignable(
                                    target, call_.vm.Model().ObjectClass(value))) {
                                    throw VmJavaThrow{
                                        "Ljava/io/IOException;",
                                        "serialized field type mismatch: " +
                                        field.name
                                    };
                                }
                            }
                            WriteInstanceFieldBits(call_, object, *local, value.Value());
                        }
                    } else {
                        const auto bits =
                                TakeRawUnsigned(call_, PrimitiveFieldWidth(field.type_code));
                        if (local.has_value()) {
                            WriteInstanceFieldBits(call_, object, *local, bits);
                        }
                    }
                }
                if (descriptor->descriptor == "Ljava/util/Date;") {
                    const auto millis = call_.vm.Linker().FindFieldRecursive(
                        owner, "milliseconds", "J");
                    if (!millis.has_value()) {
                        throw VmJavaThrow{
                            "Ljava/io/IOException;",
                            "Date milliseconds field is unavailable"
                        };
                    }
                    const auto token =
                            static_cast<std::uint8_t>(TakeRawUnsigned(call_, 1U));
                    std::size_t length{};
                    if (token == kTcBlockData) {
                        length = static_cast<std::size_t>(TakeRawUnsigned(call_, 1U));
                    } else if (token == kTcBlockDataLong) {
                        length = static_cast<std::size_t>(TakeRawUnsigned(call_, 4U));
                    } else {
                        throw VmJavaThrow{
                            "Ljava/io/IOException;",
                            "Date custom data block is missing"
                        };
                    }
                    if (length != 8U) {
                        throw VmJavaThrow{
                            "Ljava/io/IOException;",
                            "Date custom data has invalid length"
                        };
                    }
                    WriteInstanceFieldBits(call_, object, *millis,
                                           TakeRawUnsigned(call_, 8U));
                    if (TakeRawUnsigned(call_, 1U) != kTcEndBlockData) {
                        throw VmJavaThrow{
                            "Ljava/io/IOException;",
                            "Date custom data is not terminated"
                        };
                    }
                }
            }

            [[nodiscard]] VmObjectRef ReadEnum() {
                const auto descriptor = ReadClassDescriptor();
                if (descriptor == nullptr ||
                    (descriptor->flags & kScEnum) == 0U) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "enum class descriptor is missing"
                    };
                }
                const auto placeholder =
                        RegisterHandle({VmObjectRef{0}, nullptr});
                const auto name = Read();
                const auto string_class =
                        call_.vm.Linker().ResolveDescriptor("Ljava/lang/String;");
                if (!name.IsValid() || call_.vm.Model().ObjectClass(name) != string_class) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "enum constant name is invalid"
                    };
                }
                const auto java_class = DexClassId(descriptor->runtime_class);
                PropagateNestedOutcome(call_,
                                       call_.vm.EnsureClassInitialized(java_class));
                const auto wanted = call_.vm.Model().StringValue(name);
                for (const auto field_id:
                     call_.vm.Linker().Class(java_class).own_static_fields) {
                    const auto& field = call_.vm.Linker().Field(field_id);
                    if (!field.is_ref || field.descriptor != descriptor->descriptor) {
                        continue;
                    }
                    const auto candidate = VmObjectRef(
                        call_.vm.Linker().Class(java_class).static_storage[field.slot]);
                    if (!candidate.IsValid()) continue;
                    const auto name_field = call_.vm.Linker().FindFieldRecursive(
                        java_class, "name", "Ljava/lang/String;");
                    if (!name_field.has_value()) break;
                    const auto candidate_name = VmObjectRef(static_cast<std::uint32_t>(
                        ReadInstanceFieldBits(call_, candidate, *name_field)));
                    if (candidate_name.IsValid() &&
                        call_.vm.Model().StringValue(candidate_name) == wanted) {
                        state_.handles[placeholder].object = candidate;
                        return candidate;
                    }
                }
                throw VmJavaThrow{
                    "Ljava/io/IOException;",
                    "enum constant is unavailable"
                };
            }

            [[nodiscard]] VmObjectRef ReadInternal() {
                while (true) {
                    const auto token =
                            static_cast<std::uint8_t>(TakeRawUnsigned(call_, 1U));
                    if (token == kTcReset) {
                        state_.handles.clear();
                        continue;
                    }
                    if (token == kTcNull) return VmObjectRef{0};
                    if (token == kTcReference) {
                        const auto& handle = TakeReference();
                        if (handle.class_descriptor != nullptr) {
                            throw VmJavaThrow{
                                "Ljava/io/IOException;",
                                "class descriptor used as an object reference"
                            };
                        }
                        return handle.object;
                    }
                    if (token == kTcString || token == kTcLongString) {
                        return ReadString(token);
                    }
                    if (token == kTcEnum) return ReadEnum();
                    if (token != kTcObject) {
                        throw VmJavaThrow{
                            "Ljava/io/IOException;",
                            "unsupported object stream token"
                        };
                    }
                    const auto descriptor = ReadClassDescriptor();
                    if (descriptor == nullptr ||
                        (descriptor->flags & kScEnum) != 0U) {
                        throw VmJavaThrow{
                            "Ljava/io/IOException;",
                            "object class descriptor is invalid"
                        };
                    }
                    const auto java_class = DexClassId(descriptor->runtime_class);
                    const auto& linked = call_.vm.Linker().Class(java_class);
                    if (linked.is_interface || linked.is_array ||
                        (linked.access_flags & kAccAbstract) != 0U) {
                        throw VmJavaThrow{
                            "Ljava/io/IOException;",
                            "serialized class is not instantiable"
                        };
                    }
                    const auto object =
                            call_.vm.Model().NewInstance(java_class, linked.instance_slots);
                    static_cast<void>(RegisterHandle({object, nullptr}));
                    if ((descriptor->flags & kScExternalizable) != 0U) {
                        const auto ctor = call_.vm.Linker().FindDirectMethod(
                            java_class, "<init>", "()V");
                        if (!ctor || call_.vm.Linker().Method(*ctor).owner != java_class ||
                            (call_.vm.Linker().Method(*ctor).access_flags & kAccPublic) == 0U)
                            throw VmJavaThrow{"Ljava/io/InvalidClassException;",
                                              "Externalizable requires a public no-arg constructor"};
                        PropagateNestedOutcome(call_, call_.vm.EnsureClassInitialized(java_class));
                        const std::array args{VmValue::Ref(object)};
                        PropagateNestedOutcome(call_, call_.vm.Call(*ctor, args));
                    }
                    ReadClassData(object, descriptor);
                    return object;
                }
            }

            IntrinsicContext& call_;
            IoRuntime::ObjectInputState& state_;
        };

        [[nodiscard]] bool PrepareObjectPrimitiveBlock(IntrinsicContext& call, bool blocking = true) {
            auto& object = ObjectInput(call);
            if (object.pushback.has_value() || object.block_remaining > 0U) return true;
            auto& input = Input(call);
            while (EnsureInput(call, 1, blocking)) {
                const auto token = static_cast<std::uint8_t>(input.bytes[input.cursor]);
                const std::size_t header = token == kTcBlockData ? 2 : token == kTcBlockDataLong ? 5 : 0;
                if (!header) return false;
                if (!EnsureInput(call, header, blocking)) {
                    if (!blocking) return false;
                    throw VmJavaThrow{"Ljava/io/EOFException;", "truncated object block header"};
                }
                std::uint64_t count{};
                for (std::size_t i = 1; i < header; ++i)
                    count = (count << 8U) | static_cast<std::uint8_t>(input.bytes[input.cursor + i]);
                if (count > SIZE_MAX - header || !EnsureInput(call, header + static_cast<std::size_t>(count), blocking)) {
                    if (!blocking) return false;
                    throw VmJavaThrow{"Ljava/io/IOException;", "invalid object stream block length"};
                }
                input.cursor += header;
                object.block_remaining = static_cast<std::size_t>(count);
                if (object.block_remaining > 0U) return true;
            }
            return false;
        }

        [[nodiscard]] std::optional<std::uint8_t>
        ReadObjectPrimitiveByte(IntrinsicContext& call) {
            auto& object = ObjectInput(call);
            if (object.pushback.has_value()) {
                const auto value = *object.pushback;
                object.pushback.reset();
                return value;
            }
            if (!PrepareObjectPrimitiveBlock(call)) return std::nullopt;
            auto& input = Input(call);
            --object.block_remaining;
            return static_cast<std::uint8_t>(input.bytes[input.cursor++]);
        }

        [[nodiscard]] std::vector<std::byte>
        ReadObjectPrimitiveBytes(IntrinsicContext& call, const std::size_t count) {
            std::vector<std::byte> bytes;
            bytes.reserve(count);
            while (bytes.size() < count) {
                const auto value = ReadObjectPrimitiveByte(call);
                if (!value.has_value()) {
                    throw VmJavaThrow{"Ljava/io/EOFException;", "end of primitive data"};
                }
                bytes.push_back(static_cast<std::byte>(*value));
            }
            return bytes;
        }

        [[nodiscard]] std::uint64_t ReadObjectPrimitiveUnsigned(
            IntrinsicContext& call, const std::size_t width) {
            std::uint64_t value{};
            for (const auto byte: ReadObjectPrimitiveBytes(call, width)) {
                value = (value << 8U) | static_cast<std::uint8_t>(byte);
            }
            return value;
        }

        IntrinsicHandler ReadObjectInputRange() {
            return [](IntrinsicContext& call) {
                const auto array = call.arguments[0].ref;
                if (!array.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
                }
                const auto offset = call.arguments[1].AsInt();
                const auto length = call.arguments[2].AsInt();
                if (offset < 0 || length < 0 ||
                    static_cast<std::int64_t>(offset) + length >
                    call.vm.Model().ArrayLength(array)) {
                    throw VmJavaThrow{
                        "Ljava/lang/IndexOutOfBoundsException;",
                        "read range exceeds the destination array"
                    };
                }
                if (length == 0) return VmValue::Int(0);
                std::vector<std::byte> bytes;
                bytes.reserve(static_cast<std::size_t>(length));
                while (bytes.size() < static_cast<std::size_t>(length)) {
                    const auto value = ReadObjectPrimitiveByte(call);
                    if (!value.has_value()) break;
                    bytes.push_back(static_cast<std::byte>(*value));
                }
                if (bytes.empty()) return VmValue::Int(-1);
                call.vm.Model().WriteByteRegion(array, offset, bytes);
                return VmValue::Int(static_cast<std::int32_t>(bytes.size()));
            };
        }

        IntrinsicHandler SkipObjectInput() {
            return [](IntrinsicContext& call) {
                const auto requested = call.arguments[0].AsLong();
                std::int64_t skipped{};
                while (skipped < requested) {
                    if (!ReadObjectPrimitiveByte(call).has_value()) break;
                    ++skipped;
                }
                return VmValue::Long(skipped);
            };
        }

        class ReaderMonitor final {
        public:
            ReaderMonitor(Interpreter& vm, VmObjectRef lock)
                : vm_(vm), lock_(lock), token_(vm.CurrentContextToken()) {
                vm_.Monitors().Enter(lock_, token_);
            }
            ~ReaderMonitor() { vm_.Monitors().Exit(lock_, token_); }
        private:
            Interpreter& vm_;
            VmObjectRef lock_;
            std::uint64_t token_;
        };

        IntrinsicClassDecl DeclareInputStreamReader() {
            auto b = IntrinsicClassBuilder::Class("Ljava/io/InputStreamReader;", "Ljava/io/Reader;");
            const auto source = b.BoundInstanceField("source", "Ljava/io/InputStream;", kAccPrivate);
            const auto encoding = b.BoundInstanceField("encoding", "Ljava/lang/String;", kAccPrivate);
            const auto closed = b.BoundInstanceField("closed", "Z", kAccPrivate);
            const auto construct = [source, encoding](IntrinsicContext& c, std::string name) {
                if (!c.arguments[0].ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "input == null"};
                const auto parent = c.vm.Linker().ResolveDescriptor("Ljava/io/Reader;");
                const auto ctor = c.vm.Linker().FindDirectMethod(parent, "<init>", "(Ljava/lang/Object;)V");
                const std::array args{VmValue::Ref(c.receiver), VmValue::Ref(c.arguments[0].ref)};
                const auto result = c.vm.Call(*ctor, args);
                if (result.exception.IsValid()) throw VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message, result.exception};
                IntrinsicCall(c).SetRef(source, c.arguments[0].ref);
                IntrinsicCall(c).SetRef(encoding, c.vm.NewStringUtf8(name));
                InitializePinnedIcu(); UErrorCode status = U_ZERO_ERROR;
                auto* converter = ucnv_open(name.c_str(), &status); CheckIcu(status);
                c.vm.IO().Decoder(c.receiver).converter = std::shared_ptr<void>(converter, [](void* p) { ucnv_close(static_cast<UConverter*>(p)); });
                return VmValue::Void();
            };
            b.Constructor("(Ljava/io/InputStream;)V", [construct](IntrinsicContext& c) { return construct(c, "UTF-8"); });
            b.Constructor("(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V", [construct](IntrinsicContext& c) { return construct(c, CharsetName(c.vm, c.arguments[1].ref)); });
            b.Constructor("(Ljava/io/InputStream;Ljava/lang/String;)V", [construct](IntrinsicContext& c) {
                if (!c.arguments[1].ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "charset == null"};
                try { return construct(c, CanonicalCharset(c.vm.StringUtf8(c.arguments[1].ref))); }
                catch (const VmJavaThrow& e) { if (e.descriptor == "Ljava/nio/charset/UnsupportedCharsetException;" || e.descriptor == "Ljava/nio/charset/IllegalCharsetNameException;") throw VmJavaThrow{"Ljava/io/UnsupportedEncodingException;", e.message}; throw; }
            });
            const auto read = [source, closed](IntrinsicContext& c) -> std::int32_t {
                if (IntrinsicCall(c).GetInt(closed)) throw VmJavaThrow{"Ljava/io/IOException;", "reader is closed"};
                auto& state = c.vm.IO().Decoder(c.receiver);
                if (!state.converter) throw VmJavaThrow{"Ljava/io/IOException;", "reader is uninitialized"};
                while (state.pending.empty() && !state.ended) {
                    const auto value = detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(source), "read", "()I").AsInt();
                    state.ended = value < 0;
                    const char byte = static_cast<char>(value); const char* input = &byte;
                    const char* end = input + (state.ended ? 0 : 1);
                    UChar output[4]; auto* next = output; UErrorCode status = U_ZERO_ERROR;
                    ucnv_toUnicode(static_cast<UConverter*>(state.converter.get()), &next, output + 4, &input, end, nullptr, state.ended, &status);
                    CheckIcu(status);
                    for (auto* unit = output; unit != next; ++unit) state.pending.push_back(static_cast<char16_t>(*unit));
                }
                if (state.pending.empty()) return -1;
                const auto unit = state.pending.front(); state.pending.pop_front(); return unit;
            };
            b.OverrideMethod("read", "()I", [read,source](IntrinsicContext& c) {
                const ReaderMonitor lock(c.vm, IntrinsicCall(c).GetRef(source));
                return VmValue::Int(read(c));
            });
            b.OverrideMethod("read", "([CII)I", [read,source,closed](IntrinsicContext& c) {
                const ReaderMonitor lock(c.vm, IntrinsicCall(c).GetRef(source));
                if (IntrinsicCall(c).GetInt(closed)) throw VmJavaThrow{"Ljava/io/IOException;", "reader is closed"};
                const auto array = c.arguments[0].ref;
                if (!array.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
                const auto offset = c.arguments[1].AsInt(), count = c.arguments[2].AsInt();
                if (offset < 0 || count < 0 || static_cast<std::int64_t>(offset) + count > c.vm.Model().ArrayLength(array)) throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "read range"};
                std::int32_t done = 0;
                while (done < count) {
                    if (done && c.vm.IO().Decoder(c.receiver).pending.empty() && detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(source), "available", "()I").AsInt() == 0) break;
                    const auto unit = read(c); if (unit < 0) return VmValue::Int(done ? done : -1);
                    c.vm.Model().SetPrimitiveElement(array, offset + done++, static_cast<std::uint64_t>(unit));
                }
                return VmValue::Int(done);
            });
            b.OverrideMethod("ready", "()Z", [source, closed](IntrinsicContext& c) {
                const ReaderMonitor lock(c.vm, IntrinsicCall(c).GetRef(source));
                if (IntrinsicCall(c).GetInt(closed)) throw VmJavaThrow{"Ljava/io/IOException;", "reader is closed"};
                return VmValue::Int(!c.vm.IO().Decoder(c.receiver).pending.empty() || detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(source), "available", "()I").AsInt() > 0);
            });
            b.OverrideMethod("close", "()V", [source, closed](IntrinsicContext& c) {
                const ReaderMonitor lock(c.vm, IntrinsicCall(c).GetRef(source));
                if (!IntrinsicCall(c).GetInt(closed)) {
                    detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(source), "close", "()V");
                    c.vm.IO().Decoder(c.receiver) = {};
                    IntrinsicCall(c).SetInt(closed, 1);
                }
                return VmValue::Void();
            });
            b.VirtualMethod("getEncoding", "()Ljava/lang/String;", [encoding,closed](IntrinsicContext& c) { return VmValue::Ref(IntrinsicCall(c).GetInt(closed) ? VmObjectRef{} : IntrinsicCall(c).GetRef(encoding)); });
            return std::move(b).Build();
        }

        IntrinsicClassDecl DeclareObjectInputStream() {
            auto builder = IntrinsicClassBuilder::Class(
                "Ljava/io/ObjectInputStream;", "Ljava/io/InputStream;",
                {"Ljava/io/ObjectInput;", "Ljava/io/ObjectStreamConstants;"});
            builder.Constructor("()V",
                                [](IntrinsicContext&) { return VmValue::Void(); },
                                kAccProtected);
            builder.Constructor("(Ljava/io/InputStream;)V", [](IntrinsicContext& call) {
                const auto source = call.arguments[0].ref;
                if (!source.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "input == null"};
                }
                IoRuntime::InputState input; input.source = source;
                call.vm.IO().SetInput(call.receiver, std::move(input));
                call.vm.IO().BeginObjectInput(call.receiver);
                if (TakeRawUnsigned(call, 4) != UINT64_C(0xaced0005))
                    throw VmJavaThrow{"Ljava/io/IOException;", "invalid object stream header"};
                return VmValue::Void();
            });
            builder.OverrideMethod("read", "()I", [](IntrinsicContext& call) {
                const auto value = ReadObjectPrimitiveByte(call);
                return VmValue::Int(value.has_value() ? *value : -1);
            });
            builder.OverrideMethod("read", "([BII)I", ReadObjectInputRange());
            builder.OverrideMethod("available", "()I", [](IntrinsicContext& call) {
                if (!PrepareObjectPrimitiveBlock(call, false)) return VmValue::Int(0);
                const auto& object = ObjectInput(call);
                const auto available = object.block_remaining +
                                       (object.pushback.has_value() ? 1U : 0U);
                return VmValue::Int(static_cast<std::int32_t>(std::min<std::size_t>(
                    available,
                    static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))));
            });
            builder.OverrideMethod("skip", "(J)J", SkipObjectInput());
            builder.OverrideMethod("close", "()V", [](IntrinsicContext& call) {
                auto* input = call.vm.IO().FindInput(call.receiver);
                if (input && !input->closed) {
                    if (input->source.IsValid()) detail::InvokeGuest(call.vm, input->source, "close", "()V");
                    call.vm.IO().CloseInput(call.receiver);
                }
                return VmValue::Void();
            });
            builder.VirtualMethod("readBoolean", "()Z", [](IntrinsicContext& call) {
                return VmValue::Int(ReadObjectPrimitiveUnsigned(call, 1U) != 0U);
            });
            builder.VirtualMethod("readByte", "()B", [](IntrinsicContext& call) {
                return VmValue::Int(static_cast<std::int8_t>(
                    ReadObjectPrimitiveUnsigned(call, 1U)));
            });
            builder.VirtualMethod("readChar", "()C", [](IntrinsicContext& call) {
                return VmValue::Int(static_cast<std::uint16_t>(
                    ReadObjectPrimitiveUnsigned(call, 2U)));
            });
            builder.VirtualMethod("readDouble", "()D", [](IntrinsicContext& call) {
                return VmValue::Double(std::bit_cast<double>(
                    ReadObjectPrimitiveUnsigned(call, 8U)));
            });
            builder.VirtualMethod("readFloat", "()F", [](IntrinsicContext& call) {
                return VmValue::Float(std::bit_cast<float>(static_cast<std::uint32_t>(
                    ReadObjectPrimitiveUnsigned(call, 4U))));
            });
            builder.VirtualMethod("readInt", "()I", [](IntrinsicContext& call) {
                return VmValue::Int(static_cast<std::int32_t>(
                    ReadObjectPrimitiveUnsigned(call, 4U)));
            });
            builder.VirtualMethod("readLong", "()J", [](IntrinsicContext& call) {
                return VmValue::Long(static_cast<std::int64_t>(
                    ReadObjectPrimitiveUnsigned(call, 8U)));
            });
            builder.VirtualMethod("readShort", "()S", [](IntrinsicContext& call) {
                return VmValue::Int(static_cast<std::int16_t>(
                    ReadObjectPrimitiveUnsigned(call, 2U)));
            });
            builder.VirtualMethod("readUnsignedByte", "()I",
                                  [](IntrinsicContext& call) {
                                      return VmValue::Int(static_cast<std::uint8_t>(
                                          ReadObjectPrimitiveUnsigned(call, 1U)));
                                  });
            builder.VirtualMethod("readUnsignedShort", "()I",
                                  [](IntrinsicContext& call) {
                                      return VmValue::Int(static_cast<std::uint16_t>(
                                          ReadObjectPrimitiveUnsigned(call, 2U)));
                                  });
            builder.VirtualMethod("readFully", "([B)V", [](IntrinsicContext& call) {
                const auto array = call.arguments[0].ref;
                if (!array.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
                }
                const auto count = static_cast<std::size_t>(
                    call.vm.Model().ArrayLength(array));
                const auto bytes = ReadObjectPrimitiveBytes(call, count);
                call.vm.Model().WriteByteRegion(array, 0, bytes);
                return VmValue::Void();
            });
            builder.VirtualMethod("readFully", "([BII)V", [](IntrinsicContext& call) {
                const auto array = call.arguments[0].ref;
                if (!array.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
                }
                const auto offset = call.arguments[1].AsInt();
                const auto count = call.arguments[2].AsInt();
                if (offset < 0 || count < 0 ||
                    static_cast<std::int64_t>(offset) + count >
                    call.vm.Model().ArrayLength(array)) {
                    throw VmJavaThrow{
                        "Ljava/lang/IndexOutOfBoundsException;",
                        "readFully range exceeds the destination array"
                    };
                }
                const auto bytes = ReadObjectPrimitiveBytes(
                    call, static_cast<std::size_t>(count));
                call.vm.Model().WriteByteRegion(array, offset, bytes);
                return VmValue::Void();
            });
            builder.VirtualMethod(
                "readLine", "()Ljava/lang/String;", [](IntrinsicContext& call) {
                    std::u16string line;
                    while (true) {
                        const auto value = ReadObjectPrimitiveByte(call);
                        if (!value.has_value()) {
                            return VmValue::Ref(line.empty()
                                                    ? VmObjectRef{}
                                                    : call.vm.Model().NewString(line));
                        }
                        if (*value == '\n') break;
                        if (*value == '\r') {
                            const auto next = ReadObjectPrimitiveByte(call);
                            if (next.has_value() && *next != '\n') {
                                ObjectInput(call).pushback = *next;
                            }
                            break;
                        }
                        line.push_back(static_cast<char16_t>(*value));
                    }
                    return VmValue::Ref(call.vm.Model().NewString(line));
                });
            builder.VirtualMethod(
                "readUTF", "()Ljava/lang/String;", [](IntrinsicContext& call) {
                    const auto length = static_cast<std::size_t>(
                        ReadObjectPrimitiveUnsigned(call, 2U));
                    const auto bytes = ReadObjectPrimitiveBytes(call, length);
                    return VmValue::Ref(DecodeModifiedUtf8(call, bytes));
                });
            builder.VirtualMethod("skipBytes", "(I)I", [](IntrinsicContext& call) {
                const auto requested = std::max(call.arguments[0].AsInt(), 0);
                std::int32_t skipped{};
                while (skipped < requested) {
                    if (!ReadObjectPrimitiveByte(call).has_value()) break;
                    ++skipped;
                }
                return VmValue::Int(skipped);
            });
            builder.FinalMethod(
                "readObject", "()Ljava/lang/Object;", [](IntrinsicContext& call) {
                    auto& object = ObjectInput(call);
                    if (object.pushback.has_value() || object.block_remaining > 0U) {
                        throw VmJavaThrow{
                            "Ljava/io/IOException;",
                            "primitive block data remains"
                        };
                    }
                    try { return VmValue::Ref(ObjectStreamReader(call).Read()); }
                    catch (const ObjectStreamCallbackFailure& failure) {
                        call.vm.SetPendingException(failure.throwable);
                        return VmValue::Ref(VmObjectRef{});
                    }
                });
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclareObjectOutputStream() {
            auto builder = IntrinsicClassBuilder::Class(
                "Ljava/io/ObjectOutputStream;", "Ljava/io/OutputStream;",
                {"Ljava/io/ObjectOutput;", "Ljava/io/ObjectStreamConstants;"});
            builder.Constructor("()V",
                                [](IntrinsicContext&) { return VmValue::Void(); },
                                kAccProtected);
            builder.Constructor("(Ljava/io/OutputStream;)V", [](IntrinsicContext& call) {
                const auto target = call.arguments[0].ref;
                if (!target.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "output == null"};
                }
                try {
                    IoRuntime::OutputState output; output.sink = target;
                    call.vm.IO().SetOutput(call.receiver, std::move(output));
                    call.vm.IO().BeginObjectOutput(call.receiver);
                } catch (const IoRuntimeError& error) {
                    IoFailure(error);
                }
                auto& bytes = Output(call).bytes;
                bytes.push_back(static_cast<std::byte>(kStreamMagicHigh));
                bytes.push_back(static_cast<std::byte>(kStreamMagicLow));
                bytes.push_back(static_cast<std::byte>(kStreamVersionHigh));
                bytes.push_back(static_cast<std::byte>(kStreamVersionLow));
                DeliverOutput(call, false, false);
                return VmValue::Void();
            });
            builder.OverrideMethod("write", "(I)V", [](IntrinsicContext& call) {
                const std::array payload{
                    static_cast<std::byte>(call.arguments[0].AsInt() & 0xff)
                };
                AppendObjectPrimitiveBlock(call, payload);
                return VmValue::Void();
            });
            builder.OverrideMethod("write", "([BII)V", [](IntrinsicContext& call) {
                const auto array = call.arguments[0].ref;
                if (!array.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
                }
                const auto offset = call.arguments[1].AsInt();
                const auto length = call.arguments[2].AsInt();
                if (offset < 0 || length < 0 ||
                    static_cast<std::int64_t>(offset) + length >
                    call.vm.Model().ArrayLength(array)) {
                    throw VmJavaThrow{
                        "Ljava/lang/IndexOutOfBoundsException;",
                        "write range exceeds the source array"
                    };
                }
                const auto bytes = call.vm.Model().ReadByteRegion(array, offset, length);
                AppendObjectPrimitiveBlock(call, bytes);
                return VmValue::Void();
            });
            builder.OverrideMethod("flush", "()V", FlushOutput(false));
            builder.OverrideMethod("close", "()V", FlushOutput(true));
            builder.VirtualMethod("writeBoolean", "(Z)V", [](IntrinsicContext& call) {
                AppendObjectPrimitiveInteger(call,
                                             call.arguments[0].AsInt() != 0 ? 1U : 0U,
                                             1U);
                return VmValue::Void();
            });
            builder.VirtualMethod("writeByte", "(I)V", [](IntrinsicContext& call) {
                AppendObjectPrimitiveInteger(
                    call, static_cast<std::uint8_t>(call.arguments[0].AsInt()), 1U);
                return VmValue::Void();
            });
            builder.VirtualMethod("writeChar", "(I)V", [](IntrinsicContext& call) {
                AppendObjectPrimitiveInteger(
                    call, static_cast<std::uint16_t>(call.arguments[0].AsInt()), 2U);
                return VmValue::Void();
            });
            builder.VirtualMethod("writeDouble", "(D)V", [](IntrinsicContext& call) {
                AppendObjectPrimitiveInteger(
                    call, std::bit_cast<std::uint64_t>(call.arguments[0].AsDouble()), 8U);
                return VmValue::Void();
            });
            builder.VirtualMethod("writeFloat", "(F)V", [](IntrinsicContext& call) {
                AppendObjectPrimitiveInteger(
                    call, std::bit_cast<std::uint32_t>(call.arguments[0].AsFloat()), 4U);
                return VmValue::Void();
            });
            builder.VirtualMethod("writeInt", "(I)V", [](IntrinsicContext& call) {
                AppendObjectPrimitiveInteger(
                    call, static_cast<std::uint32_t>(call.arguments[0].AsInt()), 4U);
                return VmValue::Void();
            });
            builder.VirtualMethod("writeLong", "(J)V", [](IntrinsicContext& call) {
                AppendObjectPrimitiveInteger(
                    call, static_cast<std::uint64_t>(call.arguments[0].AsLong()), 8U);
                return VmValue::Void();
            });
            builder.VirtualMethod("writeShort", "(I)V", [](IntrinsicContext& call) {
                AppendObjectPrimitiveInteger(
                    call, static_cast<std::uint16_t>(call.arguments[0].AsInt()), 2U);
                return VmValue::Void();
            });
            builder.VirtualMethod("writeBytes", "(Ljava/lang/String;)V", [](IntrinsicContext& call) {
                const auto string = call.arguments[0].ref;
                if (!string.IsValid()) {
                    throw VmJavaThrow{
                        "Ljava/lang/NullPointerException;",
                        "string == null"
                    };
                }
                const auto value = call.vm.Model().StringValue(string);
                std::vector<std::byte> bytes;
                bytes.reserve(value.size());
                for (const auto unit: value) {
                    bytes.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(unit)));
                }
                AppendObjectPrimitiveBlock(call, bytes);
                return VmValue::Void();
            });
            builder.VirtualMethod("writeChars", "(Ljava/lang/String;)V", [](IntrinsicContext& call) {
                const auto string = call.arguments[0].ref;
                if (!string.IsValid()) {
                    throw VmJavaThrow{
                        "Ljava/lang/NullPointerException;",
                        "string == null"
                    };
                }
                const auto value =
                        call.vm.Model().StringValue(string);
                std::vector<std::byte> bytes;
                bytes.reserve(value.size() * 2U);
                for (const auto unit: value) {
                    AppendBigEndian(bytes, unit, 2U);
                }
                AppendObjectPrimitiveBlock(call, bytes);
                return VmValue::Void();
            });
            builder.VirtualMethod("writeUTF", "(Ljava/lang/String;)V", [](IntrinsicContext& call) {
                const auto encoded =
                        ModifiedUtf8(call, call.arguments[0].ref);
                if (encoded.size() > 65535U) {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "modified UTF-8 string is too long"
                    };
                }
                std::vector<std::byte> bytes;
                bytes.reserve(encoded.size() + 2U);
                AppendBigEndian(bytes, encoded.size(), 2U);
                for (const auto byte: encoded) {
                    bytes.push_back(static_cast<std::byte>(byte));
                }
                AppendObjectPrimitiveBlock(call, bytes);
                return VmValue::Void();
            });
            builder.FinalMethod("writeObject", "(Ljava/lang/Object;)V", [](IntrinsicContext& call) {
                try { ObjectStreamWriter(call).Write(call.arguments[0].ref); DeliverOutput(call, false, false); }
                catch (const ObjectStreamCallbackFailure& failure) {
                    call.vm.SetPendingException(failure.throwable);
                }
                return VmValue::Void();
            });
            return std::move(builder).Build();
        }
    } // namespace

    void AppendJavaIoStreams(std::vector<IntrinsicClassDecl>& catalog) {
        catalog.push_back(DeclareInputStreamReader());
        catalog.push_back(DeclareObjectInputStream());
        catalog.push_back(DeclareObjectOutputStream());
    }
} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_UnsupportedEncodingException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    IntrinsicClassDecl Declare_java_io_UnsupportedEncodingException() {
        return DeclareSimpleThrowable("Ljava/io/UnsupportedEncodingException;", "Ljava/io/IOException;");
    }
} // namespace ogplay::runtime::dexvm::intrinsics

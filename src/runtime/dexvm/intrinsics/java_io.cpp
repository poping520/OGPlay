// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_io_EOFException.cpp ----
#include "catalog.h"
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
#include "ogplay/runtime/dexvm/reflection.h"

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

} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_PrintStream.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    IntrinsicClassDecl Declare_java_io_PrintStream() {
        auto builder = IntrinsicClassBuilder::Class("Ljava/io/PrintStream;", "Ljava/lang/Object;", {"Ljava/lang/Appendable;"});
        for (const auto* result : {"Ljava/io/PrintStream;", "Ljava/lang/Appendable;"}) {
            builder.VirtualMethod("append", std::string("(Ljava/lang/CharSequence;)") + result,
                [](IntrinsicContext& c) {
                    const auto ref = c.arguments[0].ref;
                    const auto text = ref.IsValid() ? InvokeGuest(c.vm, ref, "toString", "()Ljava/lang/String;").ref : c.vm.NewStringUtf8("null");
                    const auto bytes = c.vm.StringUtf8(text);
                    if (!bytes.empty()) GuestLine(c, bytes);
                    return VmValue::Ref(c.receiver);
                });
            builder.VirtualMethod("append", std::string("(C)") + result,
                [](IntrinsicContext& c) {
                    const auto text = c.vm.Model().NewString(std::u16string(1, static_cast<char16_t>(c.arguments[0].AsInt())));
                    const auto bytes = c.vm.StringUtf8(text);
                    if (!bytes.empty()) GuestLine(c, bytes);
                    return VmValue::Ref(c.receiver);
                });
            builder.VirtualMethod("append", std::string("(Ljava/lang/CharSequence;II)") + result,
                [](IntrinsicContext& c) {
                    const auto ref = c.arguments[0].ref.IsValid() ? c.arguments[0].ref : c.vm.NewStringUtf8("null");
                    const auto part = InvokeGuest(c.vm, ref, "subSequence", "(II)Ljava/lang/CharSequence;",
                        {c.arguments[1], c.arguments[2]}).ref;
                    const auto text = InvokeGuest(c.vm, part, "toString", "()Ljava/lang/String;").ref;
                    const auto bytes = c.vm.StringUtf8(text);
                    if (!bytes.empty()) GuestLine(c, bytes);
                    return VmValue::Ref(c.receiver);
                });
        }
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
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/jni/jni_utf.h"

namespace ogplay::runtime::dexvm::intrinsics {
    namespace {
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
                auto& decoder = c.vm.IO().Decoder(c.receiver);
                decoder.encoding = std::move(name);
                decoder.encoded.clear();
                decoder.pending.clear();
                decoder.ended = false;
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
                if (state.encoding.empty()) throw VmJavaThrow{"Ljava/io/IOException;", "reader is uninitialized"};
                while (state.pending.empty() && !state.ended) {
                    const auto value = detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(source), "read", "()I").AsInt();
                    state.ended = value < 0;
                    if (!state.ended) state.encoded.push_back(static_cast<std::byte>(value));
                    std::size_t required = 1U;
                    if (state.encoding.starts_with("UTF-16")) required = 2U;
                    else if (state.encoding == "UTF-8" && !state.encoded.empty()) {
                        const auto lead = static_cast<std::uint8_t>(state.encoded.front());
                        required = lead < 0x80U ? 1U : (lead & 0xe0U) == 0xc0U ? 2U :
                                   (lead & 0xf0U) == 0xe0U ? 3U : (lead & 0xf8U) == 0xf0U ? 4U : 1U;
                    }
                    if (state.encoded.size() >= required || (state.ended && !state.encoded.empty())) {
                        const auto decoded = DecodeCharset(state.encoded, state.encoding);
                        state.encoded.clear();
                        for (const auto unit : decoded) state.pending.push_back(unit);
                    }
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

    } // namespace

    IntrinsicClassDecl Declare_java_io_ObjectStreamClass() {
        auto b = IntrinsicClassBuilder::Class("Ljava/io/ObjectStreamClass;");
        b.StaticMethod("getConstructorId", "(Ljava/lang/Class;)J", [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(IntrinsicCall(c).NonNullRef(0, "class"));
            return VmValue::Long(c.vm.Reflection().SerializationConstructor(type));
        }, kAccPrivate | kAccNative);
        b.StaticMethod("newInstance", "(Ljava/lang/Class;J)Ljava/lang/Object;", [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(IntrinsicCall(c).NonNullRef(0, "class"));
            return VmValue::Ref(c.vm.Reflection().NewSerializationInstance(type, c.arguments[1].AsLong()));
        }, kAccPrivate | kAccNative);
        for (const auto& [name, signature] : std::array{
                 std::pair{"getConstructorSignature", "(Ljava/lang/reflect/Constructor;)Ljava/lang/String;"},
                 std::pair{"getMethodSignature", "(Ljava/lang/reflect/Method;)Ljava/lang/String;"},
                 std::pair{"getFieldSignature", "(Ljava/lang/reflect/Field;)Ljava/lang/String;"}}) {
            b.StaticMethod(name, signature, [](IntrinsicContext& c) {
                const auto descriptor = c.vm.Reflection().MemberDescriptor(IntrinsicCall(c).NonNullRef(0, "member"));
                return VmValue::Ref(c.vm.NewStringUtf8(descriptor));
            }, kAccNative);
        }
        b.StaticMethod("hasClinit", "(Ljava/lang/Class;)Z", [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(IntrinsicCall(c).NonNullRef(0, "class"));
            c.vm.Linker().EnsureClassLinked(type);
            const auto initialized = c.vm.EnsureClassInitialized(type); // JNI GetStaticMethodID initializes the class.
            if (initialized.exception.IsValid()) {
                return VmValue::Int(0); // AOSP clears lookup/init exceptions.
            }
            return VmValue::Int(c.vm.Linker().FindDirectMethod(type, "<clinit>", "()V").has_value());
        }, kAccPrivate | kAccNative);
        return std::move(b).Build();
    }

    IntrinsicClassDecl Declare_java_io_ObjectOutputStream() {
        auto b = IntrinsicClassBuilder::Class("Ljava/io/ObjectOutputStream;");
        b.UnimplementedStatic("getFieldL", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Object;", kAccPrivate | kAccNative);
        return std::move(b).Build();
    }

    void AppendJavaIoStreams(std::vector<IntrinsicClassDecl>& catalog) {
        catalog.push_back(DeclareInputStreamReader());
        catalog.push_back(Declare_java_io_ObjectOutputStream());
        catalog.push_back(Declare_java_io_ObjectStreamClass());
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

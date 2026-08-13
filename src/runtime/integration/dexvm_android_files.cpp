// java.io.File and the file-backed streams. Everything resolves through the
// shared guest VFS, so a Java save, a native fopen and the per-title sandbox
// overlay all see one filesystem (ADR-0020). Absent paths answer the
// documented Java values instead of faking success.

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ogplay/runtime/vfs/vfs.h"

#include "dexvm_android_internal.h"

namespace ogplay::runtime::android_intrinsics {

// Reads a whole file from the shared guest VFS; nullopt when the VFS is
// absent or the path does not resolve.
[[nodiscard]] std::optional<std::vector<std::byte>> VfsReadAll(
    const Context& context, const std::string& path) {
    if (context->vfs == nullptr) return std::nullopt;
    try {
        const auto info = context->vfs->Stat(path);
        const auto descriptor =
            context->vfs->Open(path, VfsOpenOptions{.read = true});
        std::vector<std::byte> bytes(info.size);
        std::size_t cursor = 0;
        while (cursor < bytes.size()) {
            const auto got = context->vfs->Read(
                descriptor, std::span(bytes).subspan(cursor));
            if (got == 0) break;
            cursor += got;
        }
        context->vfs->Close(descriptor);
        bytes.resize(cursor);
        return bytes;
    } catch (const VfsError&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::uint64_t> VfsSizeOf(
    const Context& context, const std::string& path) {
    if (context->vfs == nullptr) return std::nullopt;
    try {
        return context->vfs->Stat(path).size;
    } catch (const VfsError&) {
        return std::nullopt;
    }
}



// The File family goes through the shared VFS so a Java save and a native
// fopen see one world, and so the sandbox overlay persists both (ADR-0020).
// A missing VFS is a host assembly defect, not a guest-visible gap.
[[nodiscard]] VirtualFileSystem& RequireVfs(const Context& context) {
    if (context->vfs == nullptr) {
        throw dx::DexVmError(
            dx::DexVmErrorReason::internal_invariant,
            "the android platform context has no guest filesystem");
    }
    return *context->vfs;
}

[[nodiscard]] std::optional<VfsFileInfo> VfsStatOf(const Context& context,
                                                   const std::string& path) {
    if (context->vfs == nullptr) return std::nullopt;
    try {
        return context->vfs->Stat(path);
    } catch (const VfsError&) {
        return std::nullopt;
    }
}

// close() is a sandbox flush point, so this is where a save reaches disk.
void VfsWriteAll(const Context& context, const std::string& path,
                 const std::span<const std::byte> bytes) {
    auto& vfs = RequireVfs(context);
    try {
        const auto descriptor = vfs.Open(
            path, VfsOpenOptions{.write = true, .create = true,
                                 .truncate = true});
        std::size_t cursor = 0;
        while (cursor < bytes.size()) {
            cursor += vfs.Write(descriptor, bytes.subspan(cursor));
        }
        vfs.Close(descriptor);
    } catch (const VfsError& error) {
        throw dx::VmJavaThrow{"Ljava/io/IOException;",
                              "cannot write " + path + ": " + error.what()};
    }
}

// java.io.File.mkdirs: creates every missing level and reports the truth.
[[nodiscard]] bool VfsMakeDirectories(const Context& context,
                                      const std::string& path) {
    if (context->vfs == nullptr) return false;
    if (const auto info = VfsStatOf(context, path); info.has_value()) {
        return false;  // already present: mkdirs() answers false
    }
    std::string prefix;
    bool created = false;
    for (std::size_t cursor = 1; cursor <= path.size(); ++cursor) {
        if (cursor != path.size() && path[cursor] != '/') continue;
        prefix = path.substr(0, cursor);
        if (VfsStatOf(context, prefix).has_value()) continue;
        try {
            context->vfs->CreateDirectory(prefix);
            created = true;
        } catch (const VfsError&) {
            return false;  // real failure, real false
        }
    }
    return created;
}

[[nodiscard]] std::string FilePathOf(dx::IntrinsicContext& call,
                                     const dx::VmObjectRef file) {
    const auto slots = call.vm.Model().InstanceSlots(file);
    return call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits));
}

void RegisterFiles(dx::IntrinsicRegistry& registry, const Context& context) {
    registry.Register("android.file.init",
                      [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        slots[0] = {call.arguments[0].ref.Value(), dx::SlotTag::ref};
        return dx::VmValue::Void();
    });
    registry.Register("android.file.init_parent_child",
                      [](dx::IntrinsicContext& call) {
        auto joined = call.vm.StringUtf8(call.arguments[0].ref);
        if (!joined.empty() && joined.back() != '/') joined += '/';
        joined += call.vm.StringUtf8(call.arguments[1].ref);
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        slots[0] = {call.vm.NewStringUtf8(joined).Value(),
                    dx::SlotTag::ref};
        return dx::VmValue::Void();
    });
    // One filesystem view: the VFS already merges the read-only layers with
    // the sandbox overlay, so there is nothing else to consult.
    const auto list_children = [context](const std::string& path) {
        std::vector<std::string> names;
        if (context->vfs == nullptr) return names;
        for (auto& entry : context->vfs->ListDirectory(path)) {
            names.push_back(std::move(entry.name));
        }
        return names;
    };
    registry.Register("android.file.exists",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.receiver);
        return dx::VmValue::Int(
            VfsStatOf(context, path).has_value() ? 1 : 0);
    });
    registry.Register("android.file.is_directory",
                      [context](dx::IntrinsicContext& call) {
        const auto info = VfsStatOf(context, FilePathOf(call, call.receiver));
        return dx::VmValue::Int(
            info.has_value() && info->is_directory ? 1 : 0);
    });
    registry.Register("android.file.list",
                      [list_children](dx::IntrinsicContext& call) {
        auto names = list_children(FilePathOf(call, call.receiver));
        if (names.empty()) {
            // Documented value for a path that is not a listable directory.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        auto& vm = call.vm;
        const auto array_class =
            vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
        const auto element_class =
            vm.Linker().ResolveDescriptor("Ljava/lang/String;");
        const auto array = vm.Model().NewObjectArray(
            array_class, element_class,
            static_cast<JniSize>(names.size()));
        for (std::size_t index = 0; index < names.size(); ++index) {
            vm.Model().SetObjectElement(
                array, static_cast<JniSize>(index),
                vm.NewStringUtf8(names[index]));
        }
        return dx::VmValue::Ref(array);
    });
    registry.Register("android.file.length",
                      [context](dx::IntrinsicContext& call) {
        const auto size = VfsSizeOf(context, FilePathOf(call, call.receiver));
        // 0 is the documented value for nonexistent paths.
        return dx::VmValue::Long(
            size.has_value() ? static_cast<std::int64_t>(*size) : 0);
    });
    registry.Register("android.file.get_path",
                      [](dx::IntrinsicContext& call) {
        return MakeString(call, FilePathOf(call, call.receiver));
    });
    registry.Register("android.file.mkdirs",
                      [context](dx::IntrinsicContext& call) {
        // Really creates the levels and really reports failure; the old
        // unconditional true hid save directories that never existed.
        return dx::VmValue::Int(
            VfsMakeDirectories(context, FilePathOf(call, call.receiver)) ? 1
                                                                         : 0);
    });
    registry.Register("android.file.create_new",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.receiver);
        if (VfsStatOf(context, path).has_value()) return dx::VmValue::Int(0);
        VfsWriteAll(context, path, {});
        return dx::VmValue::Int(1);
    });
    registry.Register("android.environment.get_external_storage_dir",
                      [context](dx::IntrinsicContext& call) {
        const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
        const auto slots = call.vm.Model().InstanceSlots(file);
        slots[0] = {
            call.vm.NewStringUtf8(context->external_storage_root).Value(),
            dx::SlotTag::ref};
        return dx::VmValue::Ref(file);
    });
    registry.Register("android.context.get_external_files_dir",
                      [context](dx::IntrinsicContext& call) {
        // Platform layout under the external mount; a null type argument
        // answers the package files root.
        auto path = context->external_storage_root + "/Android/data/" +
                    context->package_name + "/files";
        const auto type = call.arguments[0].ref;
        if (type.IsValid()) {
            path += "/" + call.vm.StringUtf8(type);
        }
        const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
        const auto slots = call.vm.Model().InstanceSlots(file);
        slots[0] = {call.vm.NewStringUtf8(path).Value(), dx::SlotTag::ref};
        return dx::VmValue::Ref(file);
    });
    registry.Register("android.environment.get_external_storage_state",
                      [context](dx::IntrinsicContext& call) {
        // The external mount is required by the profile and read at
        // startup, so MEDIA_MOUNTED is the truthful state.
        return MakeString(call, "mounted");
    });
    registry.Register("android.statfs.init", [](dx::IntrinsicContext&) {
        // Only the external volume is queryable on this platform; the
        // constructor path argument selects nothing further.
        return dx::VmValue::Void();
    });
    registry.Register("android.statfs.get_block_size",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(4096);
    });
    registry.Register("android.statfs.get_available_blocks",
                      [context](dx::IntrinsicContext&) {
        const auto blocks = context->external_free_bytes / 4096U;
        return dx::VmValue::Int(static_cast<std::int32_t>(
            std::min<std::uint64_t>(blocks, INT32_MAX)));
    });
    registry.Register("android.file_writer.init_file_append",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.arguments[0].ref);
        DexVmAndroidContext::OutputStream output{path, {}, false};
        if (call.arguments[1].AsInt() != 0) {
            if (const auto existing = VfsReadAll(context, path)) {
                output.bytes = *existing;
            }
        }
        context->output_streams[call.receiver.Value()] = std::move(output);
        return dx::VmValue::Void();
    });
    registry.Register("android.file.delete",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.receiver);
        const auto info = VfsStatOf(context, path);
        if (!info.has_value() || context->vfs == nullptr) {
            return dx::VmValue::Int(0);
        }
        try {
            if (info->is_directory) {
                context->vfs->RemoveDirectory(path);
            } else {
                context->vfs->RemoveFile(path);
            }
        } catch (const VfsError&) {
            // Read-only layers and non-empty directories really do fail.
            return dx::VmValue::Int(0);
        }
        return dx::VmValue::Int(1);
    });
    const auto open_input = [context](dx::IntrinsicContext& call,
                                      const std::string& path) {
        auto bytes = VfsReadAll(context, path);
        if (!bytes.has_value()) {
            throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;",
                                  "file not found: " + path};
        }
        context->streams[call.receiver.Value()] =
            DexVmAndroidContext::Stream{std::move(*bytes), 0, false};
        return dx::VmValue::Void();
    };
    registry.Register("android.file_stream.init_file",
                      [context, open_input](dx::IntrinsicContext& call) {
        const auto file = call.arguments[0].ref;
        const auto slots = call.vm.Model().InstanceSlots(file);
        return open_input(
            call, call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits)));
    });
    registry.Register("android.file_stream.init_path",
                      [context, open_input](dx::IntrinsicContext& call) {
        return open_input(call,
                          call.vm.StringUtf8(call.arguments[0].ref));
    });
    const auto open_output = [context](dx::IntrinsicContext& call,
                                       const std::string& path) {
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{path, {}, false};
        return dx::VmValue::Void();
    };
    registry.Register("android.file_output.init_path",
                      [open_output](dx::IntrinsicContext& call) {
        return open_output(call,
                           call.vm.StringUtf8(call.arguments[0].ref));
    });
    registry.Register("android.file_output.init_file",
                      [open_output](dx::IntrinsicContext& call) {
        const auto slots =
            call.vm.Model().InstanceSlots(call.arguments[0].ref);
        return open_output(
            call, call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits)));
    });
    const auto flush_output = [context](dx::IntrinsicContext& call,
                                        const std::uint32_t handle) {
        const auto found = context->output_streams.find(handle);
        if (found == context->output_streams.end()) return;
        VfsWriteAll(context, found->second.path, found->second.bytes);
        found->second.closed = true;
        static_cast<void>(call);
    };
    registry.Register("android.file_output.write_bytes",
                      [context](dx::IntrinsicContext& call) {
        const auto found =
            context->output_streams.find(call.receiver.Value());
        if (found == context->output_streams.end() ||
            found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "output stream is closed"};
        }
        auto& model = call.vm.Model();
        const auto array = call.arguments[0].ref;
        const auto bytes =
            model.ReadByteRegion(array, 0, model.ArrayLength(array));
        found->second.bytes.insert(found->second.bytes.end(), bytes.begin(),
                                   bytes.end());
        return dx::VmValue::Void();
    });
    registry.Register("android.file_output.flush",
                      [context](dx::IntrinsicContext& call) {
        // Bytes become visible to readers at flush (and again at close).
        const auto found =
            context->output_streams.find(call.receiver.Value());
        if (found != context->output_streams.end() &&
            !found->second.closed) {
            VfsWriteAll(context, found->second.path, found->second.bytes);
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.file_output.close",
                      [context, flush_output](dx::IntrinsicContext& call) {
        flush_output(call, call.receiver.Value());
        return dx::VmValue::Void();
    });
    registry.Register("android.data_output.init",
                      [context](dx::IntrinsicContext& call) {
        // Chain: reuse the wrapped stream's output slot.
        const auto target = call.arguments[0].ref;
        const auto found = context->output_streams.find(target.Value());
        if (found == context->output_streams.end()) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "DataOutputStream target is not open"};
        }
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{found->second.path, {}, false};
        return dx::VmValue::Void();
    });
    registry.Register("android.data_output.write_utf",
                      [context](dx::IntrinsicContext& call) {
        auto found = context->output_streams.find(call.receiver.Value());
        if (found == context->output_streams.end() || found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "DataOutputStream is closed"};
        }
        const auto text = call.vm.StringUtf8(call.arguments[0].ref);
        auto& bytes = found->second.bytes;
        bytes.push_back(static_cast<std::byte>((text.size() >> 8U) & 0xffU));
        bytes.push_back(static_cast<std::byte>(text.size() & 0xffU));
        for (const auto character : text) {
            bytes.push_back(static_cast<std::byte>(character));
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.data_output.close",
                      [context, flush_output](dx::IntrinsicContext& call) {
        flush_output(call, call.receiver.Value());
        return dx::VmValue::Void();
    });
}

}  // namespace ogplay::runtime::android_intrinsics

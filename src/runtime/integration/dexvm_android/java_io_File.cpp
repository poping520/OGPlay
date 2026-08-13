// java.io.File handlers. Everything resolves through the shared guest VFS
// (ADR-0020); absent paths answer the documented Java values instead of
// faking success.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/runtime/vfs/vfs.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {

[[nodiscard]] std::optional<std::uint64_t> VfsSizeOf(
    const Context& context, const std::string& path) {
    if (context->vfs == nullptr) return std::nullopt;
    try {
        return context->vfs->Stat(path).size;
    } catch (const VfsError&) {
        return std::nullopt;
    }
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

}  // namespace

Decl Declare_java_io_File(const Context& context) {
    // One filesystem view: the VFS already merges the read-only layers with
    // the sandbox overlay, so there is nothing else to consult.
    const auto list_children = [context](const std::string& path)
        -> std::optional<std::vector<std::string>> {
        const auto info = VfsStatOf(context, path);
        if (!info.has_value() || !info->is_directory ||
            context->vfs == nullptr) {
            return std::nullopt;
        }
        std::vector<std::string> names;
        try {
            for (auto& entry : context->vfs->ListDirectory(path)) {
                names.push_back(std::move(entry.name));
            }
        } catch (const VfsError&) {
            return std::nullopt;
        }
        return names;
    };
    dx::IntrinsicClassBuilder builder("Ljava/io/File;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("path", "Ljava/lang/String;", false);
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](dx::IntrinsicContext& call) {
            const auto slots = call.vm.Model().InstanceSlots(call.receiver);
            slots[0] = {call.arguments[0].ref.Value(), dx::SlotTag::ref};
            return dx::VmValue::Void();
        });
    builder.Virtual("<init>", "(Ljava/lang/String;Ljava/lang/String;)V",
        [](dx::IntrinsicContext& call) {
            auto joined = call.vm.StringUtf8(call.arguments[0].ref);
            if (!joined.empty() && joined.back() != '/') joined += '/';
            joined += call.vm.StringUtf8(call.arguments[1].ref);
            const auto slots = call.vm.Model().InstanceSlots(call.receiver);
            slots[0] = {call.vm.NewStringUtf8(joined).Value(),
                        dx::SlotTag::ref};
            return dx::VmValue::Void();
        });
    builder.Virtual("exists", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto path = FilePathOf(call, call.receiver);
            return dx::VmValue::Int(
                VfsStatOf(context, path).has_value() ? 1 : 0);
        });
    builder.Virtual("length", "()J",
        [context](dx::IntrinsicContext& call) {
            const auto size =
                VfsSizeOf(context, FilePathOf(call, call.receiver));
            // 0 is the documented value for nonexistent paths.
            return dx::VmValue::Long(
                size.has_value() ? static_cast<std::int64_t>(*size) : 0);
        });
    const auto get_path = [](dx::IntrinsicContext& call) {
        return MakeString(call, FilePathOf(call, call.receiver));
    };
    builder.Virtual("getPath", "()Ljava/lang/String;", get_path);
    builder.Virtual("getAbsolutePath", "()Ljava/lang/String;", get_path);
    const auto mkdirs = [context](dx::IntrinsicContext& call) {
        // Really creates the levels and really reports failure; the old
        // unconditional true hid save directories that never existed.
        return dx::VmValue::Int(
            VfsMakeDirectories(context, FilePathOf(call, call.receiver))
                ? 1
                : 0);
    };
    builder.Virtual("mkdir", "()Z", mkdirs);
    builder.Virtual("mkdirs", "()Z", mkdirs);
    builder.Virtual("createNewFile", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto path = FilePathOf(call, call.receiver);
            if (VfsStatOf(context, path).has_value()) {
                return dx::VmValue::Int(0);
            }
            VfsWriteAll(context, path, {});
            return dx::VmValue::Int(1);
        });
    builder.Virtual("delete", "()Z",
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
    builder.Virtual("isDirectory", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto info =
                VfsStatOf(context, FilePathOf(call, call.receiver));
            return dx::VmValue::Int(
                info.has_value() && info->is_directory ? 1 : 0);
        });
    builder.Virtual("list", "()[Ljava/lang/String;",
        [list_children](dx::IntrinsicContext& call) {
            auto names = list_children(FilePathOf(call, call.receiver));
            if (!names.has_value()) {
                // Documented value for a path that is not a listable
                // directory.
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            auto& vm = call.vm;
            const auto array_class =
                vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
            const auto element_class =
                vm.Linker().ResolveDescriptor("Ljava/lang/String;");
            const auto array = vm.Model().NewObjectArray(
                array_class, element_class,
                static_cast<JniSize>(names->size()));
            for (std::size_t index = 0; index < names->size(); ++index) {
                vm.Model().SetObjectElement(
                    array, static_cast<JniSize>(index),
                    vm.NewStringUtf8((*names)[index]));
            }
            return dx::VmValue::Ref(array);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_content_res_AssetFileDescriptor.cpp ----
#include <cstdint>
#include <utility>

#include "catalog.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_res_AssetFileDescriptor {

namespace {

void SetWide(dx::IntrinsicContext& call, const dx::VmObjectRef object,
             const std::size_t slot, const std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    auto slots = call.vm.Model().InstanceSlots(object);
    slots[slot] = {static_cast<std::uint32_t>(bits), dx::SlotTag::wide_lo};
    slots[slot + 1] = {static_cast<std::uint32_t>(bits >> 32U),
                       dx::SlotTag::wide_hi};
}

std::int64_t GetWide(dx::IntrinsicContext& call,
                     const dx::VmObjectRef object, const std::size_t slot) {
    const auto slots = call.vm.Model().InstanceSlots(object);
    const auto bits = static_cast<std::uint64_t>(slots[slot].bits) |
                      (static_cast<std::uint64_t>(slots[slot + 1].bits)
                       << 32U);
    return static_cast<std::int64_t>(bits);
}

void Initialize(dx::IntrinsicContext& call, const dx::VmObjectRef descriptor,
                const dx::VmObjectRef pfd, const std::int64_t start_offset,
                const std::int64_t length) {
    if (!pfd.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "fd must not be null"};
    }
    if (length < 0 && start_offset != 0) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalArgumentException;",
            "startOffset must be 0 when using UNKNOWN_LENGTH"};
    }
    auto slots = call.vm.Model().InstanceSlots(descriptor);
    slots[0] = {pfd.Value(), dx::SlotTag::ref};
    SetWide(call, descriptor, 1, start_offset);
    SetWide(call, descriptor, 3, length);
}

}  // namespace

Decl Declare_android_content_res_AssetFileDescriptor(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/res/AssetFileDescriptor;", "Ljava/lang/Object;");
    builder.InstanceField("mFd", "Landroid/os/ParcelFileDescriptor;", 0x0012U);
    builder.InstanceField("mStartOffset", "J", 0x0012U);
    builder.InstanceField("mLength", "J", 0x0012U);
    builder.Constructor("(Landroid/os/ParcelFileDescriptor;JJ)V",
        [](dx::IntrinsicContext& call) {
            Initialize(call, call.receiver, call.arguments[0].ref,
                       call.arguments[1].AsLong(),
                       call.arguments[2].AsLong());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getFileDescriptor", "()Ljava/io/FileDescriptor;",
        [](dx::IntrinsicContext& call) {
            const auto pfd = dx::VmObjectRef(
                call.vm.Model().InstanceSlots(call.receiver)[0].bits);
            if (!pfd.IsValid()) return dx::VmValue::Ref(dx::VmObjectRef{});
            return dx::VmValue::Ref(dx::VmObjectRef(
                call.vm.Model().InstanceSlots(pfd)[0].bits));
        });
    builder.FinalMethod("getStartOffset", "()J",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Long(GetWide(call, call.receiver, 1));
        });
    builder.FinalMethod("getLength", "()J",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Long(GetWide(call, call.receiver, 3));
        });
    builder.FinalMethod("close", "()V", [](dx::IntrinsicContext& call) {
        const auto pfd = dx::VmObjectRef(
            call.vm.Model().InstanceSlots(call.receiver)[0].bits);
        if (pfd.IsValid()) {
            const auto fd = dx::VmObjectRef(
                call.vm.Model().InstanceSlots(pfd)[0].bits);
            if (fd.IsValid()) call.vm.IO().CloseDescriptor(fd);
        }
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_res_AssetFileDescriptor(const Context& context) {
    return dvm80_android_content_res_AssetFileDescriptor::Declare_android_content_res_AssetFileDescriptor(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_res_AssetManager.cpp ----
#include <set>
#include <string>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_res_AssetManager {

Decl Declare_android_content_res_AssetManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/AssetManager;", "Ljava/lang/Object;");
    builder.FinalMethod("openFd",
        "(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;",
        [context](dx::IntrinsicContext& call) {
            const auto path = "assets/" +
                              call.vm.StringUtf8(call.arguments[0].ref);
            const loader::ApkEntry* entry = nullptr;
            for (const auto& candidate : context->archive.entries) {
                if (candidate.name == path) {
                    entry = &candidate;
                    break;
                }
            }
            if (entry == nullptr) {
                throw dx::VmJavaThrow{
                    "Ljava/io/FileNotFoundException;",
                    "APK asset is unavailable: " + path};
            }
            if (entry->compression_method != 0 ||
                entry->compressed_size != entry->uncompressed_size) {
                throw dx::VmJavaThrow{
                    "Ljava/io/FileNotFoundException;",
                    "APK asset is compressed: " + path};
            }
            std::uint64_t data_offset{};
            try {
                data_offset = loader::StoredApkEntryDataOffset(
                    context->apk_bytes, context->archive, path);
            } catch (const std::exception& error) {
                throw dx::VmJavaThrow{
                    "Ljava/io/FileNotFoundException;",
                    "APK asset cannot provide a descriptor: " + path +
                        " (" + error.what() + ")"};
            }
            const auto fd = call.vm.NewIntrinsicInstance(
                "Ljava/io/FileDescriptor;");
            call.vm.IO().SetDescriptor(
                fd, {dx::IoRuntime::DescriptorKind::apk_entry, path,
                     data_offset, false});
            const auto pfd = call.vm.NewIntrinsicInstance(
                "Landroid/os/ParcelFileDescriptor;");
            call.vm.Model().InstanceSlots(pfd)[0] = {
                fd.Value(), dx::SlotTag::ref};
            const auto descriptor = call.vm.NewIntrinsicInstance(
                "Landroid/content/res/AssetFileDescriptor;");
            const auto length =
                static_cast<std::uint64_t>(entry->uncompressed_size);
            dvm80_android_content_res_AssetFileDescriptor::Initialize(
                call, descriptor, pfd, static_cast<std::int64_t>(data_offset),
                static_cast<std::int64_t>(length));
            return dx::VmValue::Ref(descriptor);
        });
    builder.FinalMethod("list",
        "(Ljava/lang/String;)[Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            const auto requested =
                call.vm.StringUtf8(call.arguments[0].ref);
            const auto prefix = requested.empty()
                                    ? std::string{"assets/"}
                                    : "assets/" + requested + "/";
            std::set<std::string> children;
            for (const auto& entry : context->archive.entries) {
                if (!entry.name.starts_with(prefix)) continue;
                const auto remainder =
                    std::string_view(entry.name).substr(prefix.size());
                if (remainder.empty()) continue;
                const auto slash = remainder.find('/');
                children.emplace(remainder.substr(0, slash));
            }
            auto& vm = call.vm;
            const auto array_class =
                vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
            const auto string_class =
                vm.Linker().ResolveDescriptor("Ljava/lang/String;");
            const auto array = vm.Model().NewObjectArray(
                array_class, string_class,
                static_cast<JniSize>(children.size()));
            JniSize index{};
            for (const auto& child : children) {
                vm.Model().SetObjectElement(
                    array, index++, vm.NewStringUtf8(child));
            }
            return dx::VmValue::Ref(array);
        });
    builder.FinalMethod("open", "(Ljava/lang/String;)Ljava/io/InputStream;",
        [context](dx::IntrinsicContext& call) {
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            return dx::VmValue::Ref(OpenStream(
                call, context, ReadApkFile(context, "assets/" + name)));
        });
    builder.FinalMethod("open", "(Ljava/lang/String;I)Ljava/io/InputStream;",
        [context](dx::IntrinsicContext& call) {
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            return dx::VmValue::Ref(OpenStream(
                call, context, ReadApkFile(context, "assets/" + name)));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_res_AssetManager(const Context& context) {
    return dvm80_android_content_res_AssetManager::Declare_android_content_res_AssetManager(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_res_Configuration.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_res_Configuration {

Decl Declare_android_content_res_Configuration(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/Configuration;", "Ljava/lang/Object;");
    builder.InstanceField("keyboard", "I");
    builder.InstanceField("screenLayout", "I");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_res_Configuration(const Context& context) {
    return dvm80_android_content_res_Configuration::Declare_android_content_res_Configuration(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_res_Resources.cpp ----
#include "catalog.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

constexpr std::int32_t kScreenLayoutSizeMask = 0x0f;
constexpr std::int32_t kScreenLayoutSizeSmall = 0x01;
constexpr std::int32_t kScreenLayoutSizeNormal = 0x02;
constexpr std::int32_t kScreenLayoutSizeLarge = 0x03;
constexpr std::int32_t kScreenLayoutSizeXlarge = 0x04;
constexpr std::int32_t kScreenLayoutLongMask = 0x30;
constexpr std::int32_t kScreenLayoutLongNo = 0x10;
constexpr std::int32_t kScreenLayoutLongYes = 0x20;
constexpr std::int32_t kScreenLayoutCompatNeeded = 0x10000000;

std::int32_t Api19ScreenLayout(
    const ogplay::runtime::DexVmAndroidContext& context) {
    if (context.surface_width == 0U || context.surface_height == 0U ||
        !std::isfinite(context.ui_density) || context.ui_density <= 0.0F) {
        throw std::invalid_argument(
            "Configuration requires positive surface metrics and density");
    }
    const auto width_dp = static_cast<std::int32_t>(std::lround(
        static_cast<double>(context.surface_width) / context.ui_density));
    const auto height_dp = static_cast<std::int32_t>(std::lround(
        static_cast<double>(context.surface_height) / context.ui_density));
    const auto long_dp = std::max(width_dp, height_dp);
    const auto short_dp = std::min(width_dp, height_dp);

    std::int32_t size{};
    bool is_long{};
    bool compat_needed{};
    if (long_dp < 470) {
        size = kScreenLayoutSizeSmall;
    } else {
        size = long_dp >= 960 && short_dp >= 720
                   ? kScreenLayoutSizeXlarge
                   : long_dp >= 640 && short_dp >= 480
                         ? kScreenLayoutSizeLarge
                         : kScreenLayoutSizeNormal;
        compat_needed = short_dp > 321 || long_dp > 570;
        is_long = ((long_dp * 3) / 5) >= (short_dp - 1);
    }

    auto layout = kScreenLayoutLongYes | kScreenLayoutSizeXlarge;
    if (!is_long) {
        layout = (layout & ~kScreenLayoutLongMask) | kScreenLayoutLongNo;
    }
    if (compat_needed) layout |= kScreenLayoutCompatNeeded;
    if (size < (layout & kScreenLayoutSizeMask)) {
        layout = (layout & ~kScreenLayoutSizeMask) | size;
    }
    return layout;
}

void SetIntField(ogplay::runtime::dexvm::IntrinsicContext& call,
                 const ogplay::runtime::dexvm::VmObjectRef object,
                 const char* name, const std::int32_t value) {
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(object), name, "I");
    if (!field.has_value()) {
        throw std::logic_error(std::string("Configuration field missing: ") +
                               name);
    }
    const auto slot = call.vm.Linker().Field(*field).slot;
    call.vm.Model().InstanceSlots(object)[slot] = {
        static_cast<std::uint32_t>(value),
        ogplay::runtime::dexvm::SlotTag::cat1};
}

}  // namespace

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_res_Resources {

Decl Declare_android_content_res_Resources(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/Resources;", "Ljava/lang/Object;");
    builder.FinalMethod("getConfiguration", "()Landroid/content/res/Configuration;",
        [context](dx::IntrinsicContext& call) {
            const auto instance = Singleton(call, context, "configuration",
                "Landroid/content/res/Configuration;");
            // keyboard = KEYBOARD_NOKEYS (1): desktop host has no guest
            // keypad.
            SetIntField(call, instance, "keyboard", 1);
            SetIntField(call, instance, "screenLayout",
                        Api19ScreenLayout(*context));
            return dx::VmValue::Ref(instance);
        });
    builder.FinalMethod("getIdentifier",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            const auto entry_name = call.vm.StringUtf8(call.arguments[0].ref);
            const auto type_name = call.vm.StringUtf8(call.arguments[1].ref);
            const auto* entry = context->arsc.FindByName(type_name, entry_name);
            return dx::VmValue::Int(
                entry == nullptr
                    ? 0
                    : static_cast<std::int32_t>(entry->resource_id));
        });
    builder.FinalMethod("openRawResource", "(I)Ljava/io/InputStream;",
        [context](dx::IntrinsicContext& call) {
            const auto resource_id =
                static_cast<std::uint32_t>(call.arguments[0].AsInt());
            const auto* entry = context->arsc.FindById(resource_id);
            if (entry == nullptr || !entry->string_value.has_value()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                    "resource id has no file entry: " +
                        std::to_string(resource_id)};
            }
            return dx::VmValue::Ref(OpenStream(
                call, context, ReadApkFile(context, *entry->string_value)));
        });
    builder.FinalMethod("getString", "(I)Ljava/lang/String;",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "string resources are not provided yet"};
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_res_Resources(const Context& context) {
    return dvm80_android_content_res_Resources::Declare_android_content_res_Resources(context);
}
}  // namespace ogplay::runtime::android_intrinsics

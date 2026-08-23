#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

constexpr std::int32_t kGetMetaData = 0x00000080;
constexpr std::int32_t kGetPermissions = 0x00001000;
constexpr std::int32_t kPermissionGranted = 0;
constexpr std::int32_t kPermissionDenied = -1;

[[nodiscard]] const dx::LinkedField& Field(dx::IntrinsicContext& call,
                                           const dx::VmObjectRef object,
                                           const std::string_view name,
                                           const std::string_view descriptor) {
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(object), std::string(name),
        std::string(descriptor));
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "PackageManager field is not linked: " +
                                 std::string(name));
    }
    return call.vm.Linker().Field(*field);
}

void SetInt(dx::IntrinsicContext& call, const dx::VmObjectRef object,
            const std::string_view name, const std::int32_t value) {
    const auto& field = Field(call, object, name, "I");
    call.vm.Model().InstanceSlots(object)[field.slot] = {
        static_cast<std::uint32_t>(value), dx::SlotTag::cat1};
}

void SetBoolean(dx::IntrinsicContext& call, const dx::VmObjectRef object,
                const std::string_view name, const bool value) {
    const auto& field = Field(call, object, name, "Z");
    call.vm.Model().InstanceSlots(object)[field.slot] = {
        value ? 1U : 0U, dx::SlotTag::cat1};
}

void SetRef(dx::IntrinsicContext& call, const dx::VmObjectRef object,
            const std::string_view name, const std::string_view descriptor,
            const dx::VmObjectRef value) {
    const auto& field = Field(call, object, name, descriptor);
    call.vm.Model().InstanceSlots(object)[field.slot] = {
        value.Value(), dx::SlotTag::ref};
}

[[nodiscard]] dx::VmObjectRef String(dx::IntrinsicContext& call,
                                     const std::string& value) {
    return call.vm.NewStringUtf8(value);
}

[[nodiscard]] std::string RequiredString(dx::IntrinsicContext& call,
                                         const std::size_t argument,
                                         const std::string_view name) {
    dx::IntrinsicCall typed(call);
    return call.vm.StringUtf8(typed.NonNullRef(argument, name));
}

void RequireCurrentPackage(const Context& context,
                           const std::string_view package_name) {
    if (package_name != context->package_name) {
        throw dx::VmJavaThrow{
            "Landroid/content/pm/PackageManager$NameNotFoundException;",
            std::string(package_name)};
    }
}

void RequireFlags(const std::int32_t flags, const std::int32_t supported,
                  const std::string_view method) {
    if ((flags & ~supported) != 0) {
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            std::string(method) + " flags are outside the bounded API19 " +
                "PackageManager surface: " + std::to_string(flags)};
    }
}

[[nodiscard]] dx::VmObjectRef MakeMetaData(dx::IntrinsicContext& call,
                                           const Context& context) {
    const auto bundle =
        call.vm.NewIntrinsicInstance("Landroid/os/Bundle;");
    auto& values = context->bundles[bundle.Value()];
    for (const auto& [name, value] : context->application_meta_data) {
        if (const auto* integer = std::get_if<std::int32_t>(&value)) {
            values.emplace(name, *integer);
        } else {
            values.emplace(name, std::get<std::string>(value));
        }
    }
    return bundle;
}

[[nodiscard]] dx::VmObjectRef MakeApplicationInfo(
    dx::IntrinsicContext& call, const Context& context,
    const std::int32_t flags) {
    const auto info = call.vm.NewIntrinsicInstance(
        "Landroid/content/pm/ApplicationInfo;");
    const auto package = String(call, context->package_name);
    const auto application_name = String(call, context->application_class_name);
    SetRef(call, info, "name", "Ljava/lang/String;", application_name);
    SetRef(call, info, "packageName", "Ljava/lang/String;", package);
    SetRef(call, info, "className", "Ljava/lang/String;", application_name);
    SetRef(call, info, "processName", "Ljava/lang/String;", package);
    SetInt(call, info, "icon", static_cast<std::int32_t>(context->application_icon));
    SetInt(call, info, "uid", static_cast<std::int32_t>(context->application_uid));
    SetInt(call, info, "targetSdkVersion",
           static_cast<std::int32_t>(context->target_sdk_version));
    SetBoolean(call, info, "enabled", true);
    SetRef(call, info, "dataDir", "Ljava/lang/String;",
           String(call, "/data/data/" + context->package_name));
    if (context->application_label.has_value()) {
        if (const auto* resource = std::get_if<std::uint32_t>(
                &*context->application_label)) {
            SetInt(call, info, "labelRes", static_cast<std::int32_t>(*resource));
        } else {
            SetRef(call, info, "nonLocalizedLabel", "Ljava/lang/CharSequence;",
                   String(call, std::get<std::string>(*context->application_label)));
        }
    }
    if ((flags & kGetMetaData) != 0) {
        SetRef(call, info, "metaData", "Landroid/os/Bundle;",
               MakeMetaData(call, context));
    }
    return info;
}

[[nodiscard]] dx::VmObjectRef MakeStringArray(
    dx::IntrinsicContext& call, const std::vector<std::string>& values) {
    const auto array_class =
        call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
    const auto string_class =
        call.vm.Linker().ResolveDescriptor("Ljava/lang/String;");
    const auto array = call.vm.Model().NewObjectArray(
        array_class, string_class, static_cast<JniSize>(values.size()));
    JniSize index{};
    for (const auto& value : values) {
        call.vm.Model().SetObjectElement(array, index++, String(call, value));
    }
    return array;
}

[[nodiscard]] std::string ApplicationPackageName(
    dx::IntrinsicContext& call, const dx::VmObjectRef info) {
    const auto& field = Field(call, info, "packageName", "Ljava/lang/String;");
    const auto slot = call.vm.Model().InstanceSlots(info)[field.slot];
    if (slot.tag != dx::SlotTag::ref || slot.bits == 0U) return {};
    return call.vm.StringUtf8(dx::VmObjectRef{static_cast<std::uint32_t>(slot.bits)});
}

}  // namespace

Decl Declare_android_content_pm_PackageItemInfo(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageItemInfo;", "Ljava/lang/Object;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.InstanceField("name", "Ljava/lang/String;")
        .InstanceField("packageName", "Ljava/lang/String;")
        .InstanceField("labelRes", "I")
        .InstanceField("nonLocalizedLabel", "Ljava/lang/CharSequence;")
        .InstanceField("icon", "I")
        .InstanceField("logo", "I")
        .InstanceField("metaData", "Landroid/os/Bundle;");
    return std::move(builder).Build();
}

Decl Declare_android_content_pm_ApplicationInfo(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/ApplicationInfo;",
        "Landroid/content/pm/PackageItemInfo;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.InstanceField("taskAffinity", "Ljava/lang/String;")
        .InstanceField("permission", "Ljava/lang/String;")
        .InstanceField("processName", "Ljava/lang/String;")
        .InstanceField("className", "Ljava/lang/String;")
        .InstanceField("descriptionRes", "I")
        .InstanceField("theme", "I")
        .InstanceField("flags", "I")
        .InstanceField("sourceDir", "Ljava/lang/String;")
        .InstanceField("publicSourceDir", "Ljava/lang/String;")
        .InstanceField("dataDir", "Ljava/lang/String;")
        .InstanceField("nativeLibraryDir", "Ljava/lang/String;")
        .InstanceField("uid", "I")
        .InstanceField("targetSdkVersion", "I")
        .InstanceField("enabled", "Z");
    return std::move(builder).Build();
}

Decl Declare_android_content_pm_PackageInfo(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageInfo;", "Ljava/lang/Object;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.InstanceField("packageName", "Ljava/lang/String;")
        .InstanceField("versionCode", "I")
        .InstanceField("versionName", "Ljava/lang/String;")
        .InstanceField("applicationInfo", "Landroid/content/pm/ApplicationInfo;")
        .InstanceField("requestedPermissions", "[Ljava/lang/String;");
    return std::move(builder).Build();
}

Decl Declare_android_content_pm_PackageManager_NameNotFoundException(
    const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageManager$NameNotFoundException;",
        "Ljava/lang/Exception;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/String;)V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_content_pm_PackageManager(const Context& context) {
    constexpr std::uint32_t kPublicAbstract = 0x0401U;
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageManager;", "Ljava/lang/Object;", {},
        kPublicAbstract);
    builder.ConstantInt("GET_META_DATA", "I", kGetMetaData, 0x0019U)
        .ConstantInt("GET_PERMISSIONS", "I", kGetPermissions, 0x0019U)
        .ConstantInt("PERMISSION_GRANTED", "I", kPermissionGranted, 0x0019U)
        .ConstantInt("PERMISSION_DENIED", "I", kPermissionDenied, 0x0019U)
        .ConstantString("FEATURE_TOUCHSCREEN", "android.hardware.touchscreen",
                        0x0019U)
        .ConstantString("FEATURE_SCREEN_LANDSCAPE",
                        "android.hardware.screen.landscape", 0x0019U)
        .ConstantString("FEATURE_SCREEN_PORTRAIT",
                        "android.hardware.screen.portrait", 0x0019U);
    builder.VirtualMethod(
        "getApplicationInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;",
        [context](dx::IntrinsicContext& call) {
            const auto package = RequiredString(call, 0U, "packageName");
            const auto flags = call.arguments[1].AsInt();
            RequireCurrentPackage(context, package);
            RequireFlags(flags, kGetMetaData, "getApplicationInfo");
            return dx::VmValue::Ref(MakeApplicationInfo(call, context, flags));
        });
    builder.VirtualMethod(
        "getPackageInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
        [context](dx::IntrinsicContext& call) {
            const auto package = RequiredString(call, 0U, "packageName");
            const auto flags = call.arguments[1].AsInt();
            RequireCurrentPackage(context, package);
            RequireFlags(flags, kGetMetaData | kGetPermissions,
                         "getPackageInfo");
            const auto info = call.vm.NewIntrinsicInstance(
                "Landroid/content/pm/PackageInfo;");
            SetRef(call, info, "packageName", "Ljava/lang/String;",
                   String(call, context->package_name));
            SetInt(call, info, "versionCode",
                   static_cast<std::int32_t>(context->package_version_code));
            SetRef(call, info, "versionName", "Ljava/lang/String;",
                   String(call, context->package_version_name));
            SetRef(call, info, "applicationInfo",
                   "Landroid/content/pm/ApplicationInfo;",
                   MakeApplicationInfo(call, context, flags & kGetMetaData));
            if ((flags & kGetPermissions) != 0) {
                SetRef(call, info, "requestedPermissions", "[Ljava/lang/String;",
                       MakeStringArray(call, context->requested_permissions));
            }
            return dx::VmValue::Ref(info);
        });
    builder.VirtualMethod(
        "getApplicationLabel",
        "(Landroid/content/pm/ApplicationInfo;)Ljava/lang/CharSequence;",
        [context](dx::IntrinsicContext& call) {
            dx::IntrinsicCall typed(call);
            const auto info = typed.NonNullRef(0U, "info");
            RequireCurrentPackage(context, ApplicationPackageName(call, info));
            if (context->application_label.has_value()) {
                if (const auto* literal = std::get_if<std::string>(
                        &*context->application_label)) {
                    return MakeString(call, *literal);
                }
                return dx::VmValue::Ref(call.vm.Model().NewString(
                    ResolveUiString(
                        *context,
                        std::get<std::uint32_t>(*context->application_label))));
            }
            return MakeString(call, context->package_name);
        });
    builder.VirtualMethod(
        "checkPermission", "(Ljava/lang/String;Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            const auto permission = RequiredString(call, 0U, "permissionName");
            const auto package = RequiredString(call, 1U, "packageName");
            return dx::VmValue::Int(
                package == context->package_name &&
                        context->granted_permissions.contains(permission)
                    ? kPermissionGranted
                    : kPermissionDenied);
        });
    builder.VirtualMethod(
        "hasSystemFeature", "(Ljava/lang/String;)Z",
        [context](dx::IntrinsicContext& call) {
            const auto feature = RequiredString(call, 0U, "name");
            return dx::VmValue::Int(
                context->system_features.contains(feature) ? 1 : 0);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

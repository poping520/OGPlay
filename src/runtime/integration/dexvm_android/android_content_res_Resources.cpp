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

namespace ogplay::runtime::android_intrinsics {

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

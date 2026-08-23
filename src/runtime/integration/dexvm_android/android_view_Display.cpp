#include "catalog.h"

#include <bit>
#include <cmath>

namespace ogplay::runtime::android_intrinsics {
namespace {

void WriteMetric(dx::IntrinsicContext& call, const dx::VmObjectRef metrics,
                 const std::string& name, const std::uint32_t bits,
                 const std::string& descriptor) {
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(metrics), name, descriptor);
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "DisplayMetrics field is missing: " + name);
    }
    call.vm.Model().InstanceSlots(metrics)
        [call.vm.Linker().Field(*field).slot] = {bits, dx::SlotTag::cat1};
}

void PopulateMetrics(dx::IntrinsicContext& call, const Context& context) {
    const auto metrics = call.arguments[0].ref;
    if (!metrics.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "Display metrics output is null"};
    }
    if (!std::isfinite(context->ui_density) || context->ui_density <= 0.0F ||
        !std::isfinite(context->ui_scaled_density) ||
        context->ui_scaled_density <= 0.0F) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "display density is invalid");
    }
    const auto density_dpi = static_cast<std::int32_t>(
        std::lround(context->ui_density * 160.0F));
    const auto density_bits = std::bit_cast<std::uint32_t>(context->ui_density);
    const auto scaled_bits =
        std::bit_cast<std::uint32_t>(context->ui_scaled_density);
    const auto dpi_bits =
        std::bit_cast<std::uint32_t>(static_cast<float>(density_dpi));
    const auto write_int = [&](const std::string& name,
                               const std::int32_t value) {
        WriteMetric(call, metrics, name, static_cast<std::uint32_t>(value),
                    "I");
    };
    const auto write_float = [&](const std::string& name,
                                 const std::uint32_t bits) {
        WriteMetric(call, metrics, name, bits, "F");
    };
    write_int("widthPixels", static_cast<std::int32_t>(context->surface_width));
    write_int("heightPixels",
              static_cast<std::int32_t>(context->surface_height));
    write_float("density", density_bits);
    write_int("densityDpi", density_dpi);
    write_float("scaledDensity", scaled_bits);
    write_float("xdpi", dpi_bits);
    write_float("ydpi", dpi_bits);
    write_int("noncompatWidthPixels",
              static_cast<std::int32_t>(context->surface_width));
    write_int("noncompatHeightPixels",
              static_cast<std::int32_t>(context->surface_height));
    write_float("noncompatDensity", density_bits);
    write_int("noncompatDensityDpi", density_dpi);
    write_float("noncompatScaledDensity", scaled_bits);
    write_float("noncompatXdpi", dpi_bits);
    write_float("noncompatYdpi", dpi_bits);
}

}  // namespace

Decl Declare_android_util_DisplayMetrics(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/util/DisplayMetrics;", "Ljava/lang/Object;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.InstanceField("widthPixels", "I");
    builder.InstanceField("heightPixels", "I");
    builder.InstanceField("density", "F");
    builder.InstanceField("densityDpi", "I");
    builder.InstanceField("scaledDensity", "F");
    builder.InstanceField("xdpi", "F");
    builder.InstanceField("ydpi", "F");
    builder.InstanceField("noncompatWidthPixels", "I");
    builder.InstanceField("noncompatHeightPixels", "I");
    builder.InstanceField("noncompatDensity", "F");
    builder.InstanceField("noncompatDensityDpi", "I");
    builder.InstanceField("noncompatScaledDensity", "F");
    builder.InstanceField("noncompatXdpi", "F");
    builder.InstanceField("noncompatYdpi", "F");
    return std::move(builder).Build();
}

Decl Declare_android_view_Display(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/Display;", "Ljava/lang/Object;");
    builder.FinalMethod("getWidth", "()I",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Int(
                static_cast<std::int32_t>(context->surface_width));
        });
    builder.FinalMethod("getHeight", "()I",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Int(
                static_cast<std::int32_t>(context->surface_height));
        });
    // Managed surface coordinates are landscape-natural and never
    // rotate independently from the host window.
    const auto get_rotation = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.FinalMethod("getRotation", "()I", get_rotation);
    builder.FinalMethod("getOrientation", "()I", get_rotation);
    builder.FinalMethod("getDisplayId", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    const auto get_metrics = dx::IntrinsicHandler(
        [context](dx::IntrinsicContext& call) {
            PopulateMetrics(call, context);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getMetrics", "(Landroid/util/DisplayMetrics;)V",
                        get_metrics);
    builder.FinalMethod("getRealMetrics", "(Landroid/util/DisplayMetrics;)V",
                        get_metrics);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

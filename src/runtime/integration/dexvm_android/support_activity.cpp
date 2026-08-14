// Managed surface lifecycle callback dispatch. Intrinsic handlers live in
// their per-class declaration files.

#include "shared.h"

namespace ogplay::runtime {

std::optional<std::string> DispatchSurfaceHolderCallbacks(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const SurfaceHolderPhase phase) {
    namespace dx = dexvm;
    const auto* name = "surfaceCreated";
    std::string descriptor = "(Landroid/view/SurfaceHolder;)V";
    std::vector<dx::VmValue> extra;
    if (phase == SurfaceHolderPhase::changed) {
        name = "surfaceChanged";
        descriptor = "(Landroid/view/SurfaceHolder;III)V";
        // PixelFormat.RGBA_8888: the managed surface really is RGBA8.
        extra = {dx::VmValue::Int(1),
                 dx::VmValue::Int(
                     static_cast<std::int32_t>(context.surface_width)),
                 dx::VmValue::Int(
                     static_cast<std::int32_t>(context.surface_height))};
    } else if (phase == SurfaceHolderPhase::destroyed) {
        name = "surfaceDestroyed";
    }

    auto& linker = vm.Linker();
    std::size_t delivered = 0;
    for (const auto& [holder_handle, callbacks] : context.surface_callbacks) {
        for (const auto callback : callbacks) {
            const auto callback_class = vm.Model().ObjectClass(callback);
            const auto index =
                linker.FindVtableIndex(callback_class, name, descriptor);
            if (!index.has_value()) {
                // A registered callback that cannot receive the event is a
                // real defect in the guest's own class, not something to
                // quietly skip.
                return std::string("SurfaceHolder.Callback has no ") + name +
                       ": " + linker.Class(callback_class).descriptor;
            }
            std::vector<dx::VmValue> arguments{
                dx::VmValue::Ref(callback),
                dx::VmValue::Ref(dx::VmObjectRef(holder_handle))};
            arguments.insert(arguments.end(), extra.begin(), extra.end());
            const auto outcome = vm.Call(
                linker.Class(callback_class).vtable[*index], arguments);
            ++delivered;
            if (!outcome.exception.IsValid()) continue;
            std::string rendered = std::string(name) + " raised " +
                                   linker.Class(outcome.exception_class)
                                       .descriptor +
                                   ": " + outcome.exception_message;
            for (const auto& entry : outcome.exception_stack) {
                rendered += "\n  at " + entry.class_descriptor + "." +
                            entry.method_name + " (pc " +
                            std::to_string(entry.pc) + ")";
            }
            return rendered;
        }
    }
    if (auto* logger = vm.Log(); logger != nullptr && delivered > 0) {
        logger->Write(core::LogLevel::info, "session.dex_lifecycle",
                      std::string("managed surface ") + name +
                          " delivered to " + std::to_string(delivered) +
                          " holder callback(s)");
    }
    return std::nullopt;
}

std::optional<std::string> RetireSurfaceHolderGeneration(
    dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    const auto error = DispatchSurfaceHolderCallbacks(
        vm, context, SurfaceHolderPhase::destroyed);
    if (error.has_value()) return error;
    context.surface_callbacks.clear();
    context.surface_holders.clear();
    return std::nullopt;
}

}  // namespace ogplay::runtime

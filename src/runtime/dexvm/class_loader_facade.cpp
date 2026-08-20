#include "ogplay/runtime/dexvm/class_loader_facade.h"

#include <string>
#include <string_view>

#include "ogplay/runtime/dexvm/class_name_codec.h"

namespace ogplay::runtime::dexvm {

ClassLoaderFacade::ClassLoaderFacade(DexClassLinker& linker,
                                     JavaObjectModel& model)
    : linker_(&linker), model_(&model) {}

VmObjectRef ClassLoaderFacade::AllocateFacade(
    const std::string_view descriptor) {
    const auto java_class = linker_->FindClass(descriptor);
    if (!java_class.has_value()) {
        throw DexVmError(DexVmErrorReason::unknown_class,
                         "class loader facade class is not registered: " +
                             std::string(descriptor));
    }
    linker_->EnsureClassLinked(*java_class);
    const auto& linked = linker_->Class(*java_class);
    return model_->NewInstance(*java_class, linked.instance_slots);
}

VmObjectRef ClassLoaderFacade::BootstrapLoader() {
    if (!bootstrap_loader_.IsValid()) {
        bootstrap_loader_ = AllocateFacade("Ljava/lang/BootClassLoader;");
    }
    return bootstrap_loader_;
}

VmObjectRef ClassLoaderFacade::ApplicationLoader() {
    // Keep the parent alive before publishing the child. This also fixes the
    // deterministic allocation order of the two process-lifetime facades.
    static_cast<void>(BootstrapLoader());
    if (!application_loader_.IsValid()) {
        application_loader_ =
            AllocateFacade("Ldalvik/system/PathClassLoader;");
    }
    return application_loader_;
}

VmObjectRef ClassLoaderFacade::LoaderForClass(const DexClassId java_class) {
    const auto& linked = linker_->Class(java_class);
    if (ClassNameCodec::IsPrimitive(linked.descriptor)) {
        return VmObjectRef(0);
    }
    return linked.defining_loader == kBootstrapLoader
               ? BootstrapLoader()
               : ApplicationLoader();
}

std::optional<VmClassLoaderId> ClassLoaderFacade::RoleOf(
    const VmObjectRef loader_object) {
    if (!loader_object.IsValid()) return std::nullopt;
    if (loader_object == bootstrap_loader_) return kBootstrapLoader;
    if (loader_object == application_loader_) return kApplicationLoader;

    const auto class_loader = linker_->FindClass("Ljava/lang/ClassLoader;");
    if (!class_loader.has_value() ||
        !linker_->IsAssignable(*class_loader,
                               model_->ObjectClass(loader_object))) {
        return std::nullopt;
    }
    // Guest-created subclasses share the one application namespace. They do
    // not become definition authorities merely by being ClassLoader objects.
    return kApplicationLoader;
}

std::optional<VmObjectRef> ClassLoaderFacade::FacadeParent(
    const VmObjectRef loader_object) {
    if (loader_object == application_loader_) return BootstrapLoader();
    if (loader_object == bootstrap_loader_) return VmObjectRef(0);
    return std::nullopt;
}

std::optional<DexClassId> ClassLoaderFacade::FindLoadedClass(
    const VmClassLoaderId loader, const std::string_view binary_name) const {
    const auto descriptor =
        ClassNameCodec::BinaryNameToDescriptor(binary_name);
    const auto java_class = linker_->FindClass(descriptor);
    if (!java_class.has_value() ||
        !linker_->IsInitiatedBy(*java_class, loader)) {
        return std::nullopt;
    }
    return java_class;
}

bool ClassLoaderFacade::BootstrapCanLoad(
    std::string_view descriptor) const {
    while (descriptor.starts_with("[")) descriptor.remove_prefix(1);
    if (ClassNameCodec::IsPrimitive(descriptor)) return descriptor != "V";
    const auto component = linker_->FindClass(descriptor);
    return component.has_value() &&
           linker_->Class(*component).defining_loader == kBootstrapLoader;
}

DexClassId ClassLoaderFacade::LoadClass(const VmClassLoaderId loader,
                                        const std::string_view binary_name) {
    const auto descriptor =
        ClassNameCodec::BinaryNameToDescriptor(binary_name);
    if (loader == kBootstrapLoader && !BootstrapCanLoad(descriptor)) {
        throw DexVmError(DexVmErrorReason::unknown_class,
                         "bootstrap class is not available: " + descriptor);
    }

    DexClassId java_class;
    if (descriptor.starts_with("[")) {
        java_class = linker_->ResolveDescriptor(descriptor);
    } else {
        const auto known = linker_->FindClass(descriptor);
        if (!known.has_value()) {
            throw DexVmError(DexVmErrorReason::unknown_class,
                             "class is not available: " + descriptor);
        }
        java_class = *known;
    }
    try {
        linker_->EnsureClassLinked(java_class);
    } catch (const DexVmError& error) {
        if (error.Reason() == DexVmErrorReason::unknown_class) {
            throw DexVmError(DexVmErrorReason::invalid_hierarchy,
                             error.what());
        }
        throw;
    }
    linker_->MarkInitiatedBy(java_class, loader);
    return java_class;
}

void ClassLoaderFacade::VisitRoots(
    const std::function<void(VmObjectRef)>& visitor) const {
    if (!visitor) return;
    visitor(bootstrap_loader_);
    visitor(application_loader_);
}

}  // namespace ogplay::runtime::dexvm

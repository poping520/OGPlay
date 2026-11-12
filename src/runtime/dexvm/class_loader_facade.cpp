#include "ogplay/runtime/dexvm/class_loader_facade.h"

#include <array>
#include <string>
#include <string_view>

#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm {

ClassLoaderFacade::ClassLoaderFacade(Interpreter& interpreter, DexClassLinker& linker, JavaObjectModel& model)
    : interpreter_(&interpreter), linker_(&linker), model_(&model) {}

VmObjectRef ClassLoaderFacade::CallLoaderSingleton(std::string_view descriptor, std::string_view method,
                                                   std::string_view signature) {
    const auto type = linker_->ResolveDescriptor(descriptor);
    const auto target = linker_->FindDirectMethod(type, std::string(method), std::string(signature));
    if (!target) throw DexVmError(DexVmErrorReason::internal_invariant, "ROM loader singleton method is missing");
    const auto outcome = interpreter_->Call(*target, {});
    if (outcome.exception.IsValid())
        throw VmJavaThrow{linker_->Class(outcome.exception_class).descriptor, outcome.exception_message,
                          outcome.exception};
    return outcome.value.ref;
}

VmObjectRef ClassLoaderFacade::BootstrapLoader() {
    if (!bootstrap_loader_.IsValid())
        bootstrap_loader_ =
            CallLoaderSingleton("Ljava/lang/BootClassLoader;", "getInstance", "()Ljava/lang/BootClassLoader;");
    return bootstrap_loader_;
}

VmObjectRef ClassLoaderFacade::SystemLoader() {
    if (!system_loader_.IsValid())
        system_loader_ =
            CallLoaderSingleton("Ljava/lang/ClassLoader;", "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
    return system_loader_;
}

void ClassLoaderFacade::SetApplicationLoaderFactory(std::function<VmObjectRef()> factory) {
    if (application_loader_.IsValid() || application_loader_factory_)
        throw DexVmError(DexVmErrorReason::internal_invariant, "application loader already configured");
    application_loader_factory_ = std::move(factory);
}

VmObjectRef ClassLoaderFacade::ApplicationLoader() {
    // JNI may materialize the root Thread during the ROM constructor. The
    // integration owner publishes the finished application context loader next.
    if (building_application_loader_) return SystemLoader();

    struct Reset {
        bool& flag;

        ~Reset() { flag = false; }
    };

    building_application_loader_ = true;
    const Reset reset{building_application_loader_};
    if (!application_loader_.IsValid()) {
        if (application_loader_factory_)
            application_loader_ = application_loader_factory_();
        else {
            // Standalone run-dex has no APK. Still execute a real ROM constructor;
            // the sealed namespace is available to Class.forName's VM entry only.
            const auto parent = BootstrapLoader();
            const auto path = interpreter_->NewStringUtf8("");
            const auto roots = interpreter_->ProtectReferences(std::array{parent, path});
            const auto object = interpreter_->NewIntrinsicInstance("Ldalvik/system/PathClassLoader;");
            const auto object_root = interpreter_->ProtectReferences(std::array{object});
            const auto ctor = linker_->FindDirectMethod(model_->ObjectClass(object), "<init>",
                                                        "(Ljava/lang/String;Ljava/lang/ClassLoader;)V");
            if (!ctor)
                throw DexVmError(DexVmErrorReason::internal_invariant, "ROM PathClassLoader constructor is missing");
            const auto outcome =
                interpreter_->Call(*ctor, std::array{VmValue::Ref(object), VmValue::Ref(path), VmValue::Ref(parent)});
            if (outcome.exception.IsValid())
                throw VmJavaThrow{linker_->Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
            application_loader_ = object;
        }
    }
    return application_loader_;
}

bool ClassLoaderFacade::IsRomApplicationLoader(const VmObjectRef object) const {
    return application_loader_factory_ && object.IsValid() && object == application_loader_;
}

VmObjectRef ClassLoaderFacade::LoaderForClass(const DexClassId java_class) {
    const auto& linked = linker_->Class(java_class);
    if (ClassNameCodec::IsPrimitive(linked.descriptor)) {
        return VmObjectRef(0);
    }
    return linked.defining_loader == kBootstrapLoader ? BootstrapLoader() : ApplicationLoader();
}

std::optional<VmClassLoaderId> ClassLoaderFacade::RoleOf(const VmObjectRef loader_object) {
    if (!loader_object.IsValid()) return std::nullopt;
    if (loader_object == bootstrap_loader_ || loader_object == system_loader_) return kBootstrapLoader;
    if (loader_object == application_loader_) return kApplicationLoader;
    // Java may have called getSystemClassLoader directly before the facade's
    // singleton cache was populated. It is never the APK defining loader.
    if (loader_object == SystemLoader()) return kBootstrapLoader;

    const auto class_loader = linker_->FindClass("Ljava/lang/ClassLoader;");
    if (!class_loader.has_value() || !linker_->IsAssignable(*class_loader, model_->ObjectClass(loader_object))) {
        return std::nullopt;
    }
    // Guest-created subclasses share the one application namespace. They do
    // not become definition authorities merely by being ClassLoader objects.
    return kApplicationLoader;
}

std::optional<DexClassId> ClassLoaderFacade::FindLoadedClass(const VmClassLoaderId loader,
                                                             const std::string_view binary_name) const {
    const auto descriptor = ClassNameCodec::BinaryNameToDescriptor(binary_name);
    const auto java_class = linker_->FindClass(descriptor);
    if (!java_class.has_value() || !linker_->IsInitiatedBy(*java_class, loader)) {
        return std::nullopt;
    }
    return java_class;
}

bool ClassLoaderFacade::BootstrapCanLoad(std::string_view descriptor) const {
    while (descriptor.starts_with("["))
        descriptor.remove_prefix(1);
    if (ClassNameCodec::IsPrimitive(descriptor)) return descriptor != "V";
    auto component = linker_->FindClass(descriptor);
    if (!component && !linker_->GapSurveyEnabled()) {
        try {
            component = linker_->ResolveDescriptor(descriptor);
        } catch (const JavaLinkageError& error) {
            if (error.Kind() != JavaLinkageKind::no_class) throw;
        }
    }
    return component.has_value() && linker_->Class(*component).defining_loader == kBootstrapLoader;
}

DexClassId ClassLoaderFacade::LoadClass(const VmClassLoaderId loader, const std::string_view binary_name) {
    const auto descriptor = ClassNameCodec::BinaryNameToDescriptor(binary_name);
    if (loader == kBootstrapLoader && !BootstrapCanLoad(descriptor)) {
        throw DexVmError(DexVmErrorReason::unknown_class, "bootstrap class is not available: " + descriptor);
    }

    DexClassId java_class;
    if (descriptor.starts_with("[")) {
        java_class = linker_->ResolveDescriptor(descriptor);
    } else {
        const auto known = linker_->FindClass(descriptor);
        if (known)
            java_class = *known;
        else if (!linker_->GapSurveyEnabled()) {
            try {
                java_class = linker_->ResolveDescriptor(descriptor);
            } catch (const JavaLinkageError& error) {
                if (error.Kind() != JavaLinkageKind::no_class || linker_->FindClass(descriptor)) throw;
                throw DexVmError(DexVmErrorReason::unknown_class, error.what());
            }
        } else
            throw DexVmError(DexVmErrorReason::unknown_class, "class is not available: " + descriptor);
    }
    try {
        linker_->EnsureClassLinked(java_class);
    } catch (const DexVmError& error) {
        if (error.Reason() == DexVmErrorReason::unknown_class) {
            throw DexVmError(DexVmErrorReason::invalid_hierarchy, error.what());
        }
        throw;
    }
    linker_->MarkInitiatedBy(java_class, loader);
    return java_class;
}

void ClassLoaderFacade::VisitRoots(const std::function<void(VmObjectRef)>& visitor) const {
    if (!visitor) return;
    visitor(bootstrap_loader_);
    visitor(system_loader_);
    visitor(application_loader_);
}

}  // namespace ogplay::runtime::dexvm

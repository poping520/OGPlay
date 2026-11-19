#include "ogplay/runtime/dexvm/class_loader_facade.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm {

ClassLoaderFacade::ClassLoaderFacade(Interpreter& interpreter, DexClassLinker& linker, JavaObjectModel& model)
    : interpreter_(&interpreter), linker_(&linker), model_(&model) {
    linker.SetLoaderResolver(&interpreter.ExecutionLock(), [this](VmClassLoaderId loader, std::string_view descriptor) {
        return InvokeLoadClass(loader, descriptor);
    });
}

ClassLoaderFacade::~ClassLoaderFacade() {
    linker_->SetLoaderResolver(nullptr, {});
}

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
            // Embedders without a configured classpath still use a ROM loader.
            // The pre-registered classes are resolved by the linker.
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

VmObjectRef ClassLoaderFacade::LoaderObject(VmClassLoaderId loader) {
    if (loader == kBootstrapLoader) return BootstrapLoader();
    if (loader == kApplicationLoader) return ApplicationLoader();
    return dynamic_loaders_.at(loader.value - 2U);
}

VmObjectRef ClassLoaderFacade::LoaderForClass(const DexClassId java_class) {
    const auto& linked = linker_->ClassHeader(java_class);
    if (ClassNameCodec::IsPrimitive(linked.descriptor)) return VmObjectRef{};
    return LoaderObject(linked.defining_loader);
}

std::optional<VmClassLoaderId> ClassLoaderFacade::RoleOf(const VmObjectRef loader_object) {
    if (!loader_object.IsValid() || loader_object == bootstrap_loader_) return kBootstrapLoader;
    if (loader_object == application_loader_) return kApplicationLoader;
    if (const auto found = loader_ids_.find(loader_object.Value()); found != loader_ids_.end()) return found->second;

    const auto actual = model_->ObjectClass(loader_object);
    if (linker_->ClassHeader(actual).descriptor == "Ljava/lang/BootClassLoader;" &&
        linker_->ClassHeader(actual).defining_loader == kBootstrapLoader)
        return kBootstrapLoader;
    const auto class_loader = linker_->FindClass(kBootstrapLoader, "Ljava/lang/ClassLoader;");
    if (!class_loader || !linker_->IsAssignable(*class_loader, actual)) return std::nullopt;
    const auto id = VmClassLoaderId{static_cast<std::uint32_t>(dynamic_loaders_.size()) + 2U};
    const auto entry = loader_ids_.emplace(loader_object.Value(), id).first;
    try {
        dynamic_loaders_.push_back(loader_object);
    } catch (...) {
        loader_ids_.erase(entry);
        throw;
    }
    return id;
}

std::optional<DexClassId> ClassLoaderFacade::FindLoadedClass(const VmClassLoaderId loader,
                                                             const std::string_view name) const {
    // Dalvik VMClassLoader.findLoadedClass converts with dvmDotToDescriptor, which only
    // maps '.' to '/' without validating, so JNI-style "java/lang/System" still matches.
    auto descriptor = name.starts_with('[') ? std::string(name) : "L" + std::string(name) + ";";
    std::replace(descriptor.begin(), descriptor.end(), '.', '/');
    return linker_->FindInitiatedClass(loader, descriptor);
}

DexClassId ClassLoaderFacade::LoadClass(const VmClassLoaderId loader, const std::string_view binary_name) {
    const auto descriptor = ClassNameCodec::BinaryNameToDescriptor(binary_name);
    const auto type = linker_->ResolveDescriptor(descriptor, loader);
    linker_->MarkInitiatedBy(type, loader);
    return type;
}

DexClassId ClassLoaderFacade::InvokeLoadClass(VmClassLoaderId loader, std::string_view descriptor) {
    const auto object = LoaderObject(loader);
    const auto actual = model_->ObjectClass(object);
    const auto slot = linker_->FindVtableIndex(actual, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!slot) throw DexVmError(DexVmErrorReason::internal_invariant, "ROM ClassLoader.loadClass is missing");
    const auto name = interpreter_->NewStringUtf8(ClassNameCodec::ClassGetName(descriptor));
    const auto roots = interpreter_->ProtectReferences(std::array{object, name});
    const auto method = linker_->Class(actual).vtable[*slot];
    const auto result = interpreter_->Call(method, std::array{VmValue::Ref(object), VmValue::Ref(name)});
    if (result.exception.IsValid()) {
        const auto cause_root = interpreter_->ProtectReferences(std::array{result.exception});
        constexpr auto no_class = "Ljava/lang/NoClassDefFoundError;";
        const auto failure = interpreter_->MakeThrowable(no_class, descriptor);
        interpreter_->InitThrowableCause(failure, result.exception);
        throw VmJavaThrow{no_class, std::string(descriptor), failure};
    }
    if (!result.value.ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "ClassLoader returned null"};
    const auto type = model_->ClassOfClassObject(result.value.ref);
    if (linker_->ClassHeader(type).descriptor != descriptor)
        throw JavaLinkageError(JavaLinkageKind::no_class,
                               "ClassLoader returned the wrong class for " + std::string(descriptor));
    return type;
}

void ClassLoaderFacade::VisitRoots(const std::function<void(VmObjectRef)>& visitor) const {
    if (!visitor) return;
    visitor(bootstrap_loader_);
    visitor(system_loader_);
    visitor(application_loader_);
    for (const auto loader : dynamic_loaders_)
        visitor(loader);
}

}  // namespace ogplay::runtime::dexvm

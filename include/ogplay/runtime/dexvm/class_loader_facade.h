#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace ogplay::runtime::dexvm {

// Bounded API-19 loader view. It exposes bootstrap/application roles without
// creating a second class directory or granting guest loaders definition
// authority.
class ClassLoaderFacade final {
public:
    ClassLoaderFacade(DexClassLinker& linker, JavaObjectModel& model);

    [[nodiscard]] VmObjectRef BootstrapLoader();
    [[nodiscard]] VmObjectRef ApplicationLoader();
    [[nodiscard]] VmObjectRef LoaderForClass(DexClassId java_class);
    [[nodiscard]] std::optional<VmClassLoaderId> RoleOf(
        VmObjectRef loader_object);
    [[nodiscard]] std::optional<VmObjectRef> FacadeParent(
        VmObjectRef loader_object);

    // findLoadedClass is intentionally side-effect free: no synthesis,
    // linking, initialization, or initiating-state mutation.
    [[nodiscard]] std::optional<DexClassId> FindLoadedClass(
        VmClassLoaderId loader, std::string_view binary_name) const;
    // loadClass searches only the existing linker namespace. Array classes
    // may be synthesized through the normal linker path; no dex/jar is read.
    [[nodiscard]] DexClassId LoadClass(VmClassLoaderId loader,
                                       std::string_view binary_name);

    void VisitRoots(const std::function<void(VmObjectRef)>& visitor) const;

private:
    [[nodiscard]] VmObjectRef AllocateFacade(std::string_view descriptor);
    [[nodiscard]] bool BootstrapCanLoad(std::string_view descriptor) const;

    DexClassLinker* linker_{};
    JavaObjectModel* model_{};
    VmObjectRef bootstrap_loader_;
    VmObjectRef application_loader_;
};

}  // namespace ogplay::runtime::dexvm

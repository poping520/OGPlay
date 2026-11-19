#pragma once

#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace ogplay::runtime::dexvm {

// Binds ROM loader objects to linker namespaces and invokes Java delegation.
class ClassLoaderFacade final {
public:
    ClassLoaderFacade(Interpreter& interpreter, DexClassLinker& linker, JavaObjectModel& model);

    ~ClassLoaderFacade();

    [[nodiscard]] VmObjectRef BootstrapLoader();
    [[nodiscard]] VmObjectRef SystemLoader();
    void SetApplicationLoaderFactory(std::function<VmObjectRef()> factory);
    [[nodiscard]] VmObjectRef ApplicationLoader();

    [[nodiscard]] bool IsRomApplicationLoader(VmObjectRef object) const {
        return application_loader_factory_ && object.IsValid() && object == application_loader_;
    }
    [[nodiscard]] VmObjectRef LoaderForClass(DexClassId java_class);
    [[nodiscard]] std::optional<VmClassLoaderId> RoleOf(VmObjectRef loader_object);

    // findLoadedClass is intentionally side-effect free: no synthesis,
    // linking, initialization, or initiating-state mutation. Like Dalvik
    // dvmDotToDescriptor, the name is not validated; '/' separators also match.
    [[nodiscard]] std::optional<DexClassId> FindLoadedClass(VmClassLoaderId loader, std::string_view name) const;
    [[nodiscard]] DexClassId LoadClass(VmClassLoaderId loader, std::string_view binary_name);

    void VisitRoots(const std::function<void(VmObjectRef)>& visitor) const;

private:
    [[nodiscard]] VmObjectRef CallLoaderSingleton(std::string_view descriptor, std::string_view method,
                                                  std::string_view signature);
    Interpreter* interpreter_{};
    [[nodiscard]] VmObjectRef LoaderObject(VmClassLoaderId loader);
    [[nodiscard]] DexClassId InvokeLoadClass(VmClassLoaderId loader, std::string_view descriptor);

    DexClassLinker* linker_{};
    JavaObjectModel* model_{};
    VmObjectRef bootstrap_loader_;
    VmObjectRef system_loader_;
    VmObjectRef application_loader_;
    std::unordered_map<std::uint32_t, VmClassLoaderId> loader_ids_;
    std::vector<VmObjectRef> dynamic_loaders_;
    bool building_application_loader_{};
    std::function<VmObjectRef()> application_loader_factory_;
};

}  // namespace ogplay::runtime::dexvm

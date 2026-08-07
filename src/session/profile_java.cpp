#include "ogplay/session/profile_java.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ogplay::session {

ProfileJavaAssembly AssembleProfileJava(
    const TitleProfile& profile,
    const std::span<const ProfileJavaImplementation> implementations) {
    std::map<std::string, runtime::JniMethodHandler, std::less<>> handlers;
    for (const auto& implementation : implementations) {
        if (implementation.implementation.empty() || !implementation.handler) {
            throw ProfileJavaError(
                "Profile Java implementation requires an id and handler");
        }
        if (!handlers
                 .emplace(implementation.implementation, implementation.handler)
                 .second) {
            throw ProfileJavaError("duplicate Profile Java implementation: " +
                                   implementation.implementation);
        }
    }

    std::set<std::string, std::less<>> referenced;
    for (const auto& java_class : profile.java_classes) {
        for (const auto& method : java_class.methods) {
            if (!handlers.contains(method.implementation)) {
                throw ProfileJavaError(
                    "Profile Java method has no registered implementation: " +
                    method.implementation);
            }
            referenced.insert(method.implementation);
        }
    }

    auto classes = std::make_unique<runtime::JniClassRegistry>();
    std::vector<ProfileJavaMethodBinding> bindings;
    try {
        for (const auto& java_class : profile.java_classes) {
            std::vector<runtime::JniMethodDeclaration> methods;
            methods.reserve(java_class.methods.size());
            for (const auto& method : java_class.methods) {
                methods.push_back({method.name, method.signature,
                                   method.implementation, method.is_static});
            }
            const auto identity = classes->RegisterClass(
                {java_class.name, {}, std::move(methods), {}});
            for (const auto& method : java_class.methods) {
                const auto id = classes->GetMethodId(
                    identity, method.name, method.signature,
                    method.is_static);
                if (!id.has_value()) {
                    throw ProfileJavaError(
                        "Profile Java method was not registered: " +
                        java_class.name + "." + method.name);
                }
                bindings.push_back({java_class.name, method.name,
                                    method.signature, method.implementation,
                                    identity, *id});
            }
        }
    } catch (const runtime::JniClassRegistryError& error) {
        throw ProfileJavaError("Profile Java class registration failed: " +
                               std::string(error.what()));
    }

    auto invocations =
        std::make_unique<runtime::JniInvocationEngine>(*classes);
    try {
        for (const auto& implementation : referenced) {
            invocations->RegisterHandler(implementation,
                                         handlers.at(implementation));
        }
    } catch (const runtime::JniInvocationError& error) {
        throw ProfileJavaError("Profile Java handler registration failed: " +
                               std::string(error.what()));
    }
    return {std::move(classes), std::move(invocations), std::move(bindings)};
}

}  // namespace ogplay::session

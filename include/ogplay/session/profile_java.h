#pragma once

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::session {

struct ProfileJavaImplementation final {
    std::string implementation;
    runtime::JniMethodHandler handler;
};

struct ProfileJavaMethodBinding final {
    std::string class_name;
    std::string method_name;
    std::string signature;
    std::string implementation;
    runtime::JniObjectIdentity class_identity;
    runtime::JniMethodId method_id;
};

struct ProfileJavaAssembly final {
    std::unique_ptr<runtime::JniClassRegistry> classes;
    std::unique_ptr<runtime::JniInvocationEngine> invocations;
    std::vector<ProfileJavaMethodBinding> bindings;
};

class ProfileJavaError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] ProfileJavaAssembly AssembleProfileJava(
    const TitleProfile& profile,
    std::span<const ProfileJavaImplementation> implementations);

}  // namespace ogplay::session

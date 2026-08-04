#include "ogplay/runtime/framework_package.h"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace ogplay::runtime {
namespace {

[[noreturn]] void Fail(const FrameworkPackageErrorReason reason,
                       std::string message) {
    throw FrameworkPackageError(reason, std::move(message));
}

[[nodiscard]] bool IsPackageStart(const char value) {
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z') || value == '_';
}

[[nodiscard]] bool IsPackagePart(const char value) {
    return IsPackageStart(value) || (value >= '0' && value <= '9');
}

[[nodiscard]] std::vector<JniChar> AsciiText(const std::string& value) {
    std::vector<JniChar> result;
    result.reserve(value.size());
    for (const auto character : value) {
        result.push_back(static_cast<JniChar>(
            static_cast<unsigned char>(character)));
    }
    return result;
}

[[nodiscard]] std::vector<JniChar> Utf16Text(const std::u16string& value) {
    std::vector<JniChar> result;
    result.reserve(value.size());
    for (const auto character : value) {
        result.push_back(static_cast<JniChar>(character));
    }
    return result;
}

}  // namespace

FrameworkPackageError::FrameworkPackageError(
    const FrameworkPackageErrorReason reason, std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

FrameworkPackageErrorReason FrameworkPackageError::Reason() const noexcept {
    return reason_;
}

class FrameworkPackageHle::Impl final {
public:
    Impl(JniClassRegistry& classes, JniInvocationEngine& invocations,
         JniEnvironment& environment, JniStringStore& strings,
         JniFieldStore& fields, FrameworkPackageConfig config)
        : classes_(&classes),
          invocations_(&invocations),
          environment_(&environment),
          strings_(&strings),
          fields_(&fields),
          config_(std::move(config)),
          manager_(AllocateJniHostObjectIdentity()) {}

    [[nodiscard]] FrameworkPackageClassSet Install() {
        if (installed_) {
            Fail(FrameworkPackageErrorReason::duplicate_install,
                 "framework package HLE is already installed");
        }
        if (!classes_->FindClass("java/lang/Object").has_value() ||
            !classes_->FindClass("android/content/Context").has_value()) {
            Fail(FrameworkPackageErrorReason::missing_framework,
                 "framework lifecycle classes must be installed first");
        }
        ValidateConfig();
        const auto info = classes_->RegisterClass(
            {"android/content/pm/PackageInfo",
             "java/lang/Object",
             {},
             {{"versionName", "Ljava/lang/String;",
               "framework.package_info.version_name", false},
              {"versionCode", "I", "framework.package_info.version_code",
               false}}});
        const auto manager = classes_->RegisterClass(
            {"android/content/pm/PackageManager",
             "java/lang/Object",
             {{"getPackageInfo",
               "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
               "framework.package_manager.get_package_info", false}},
             {}});
        package_name_string_ = strings_->Create(AsciiText(config_.package_name));
        version_name_string_ = strings_->Create(Utf16Text(config_.version_name));
        version_name_field_ = *classes_->GetFieldId(
            info, "versionName", "Ljava/lang/String;", false);
        version_code_field_ =
            *classes_->GetFieldId(info, "versionCode", "I", false);
        invocations_->RegisterHandler(
            "framework.context.get_package_name",
            [this](const JniInvocation& call) { return GetPackageName(call); });
        invocations_->RegisterHandler(
            "framework.context.get_package_manager",
            [this](const JniInvocation& call) { return GetManager(call); });
        invocations_->RegisterHandler(
            "framework.package_manager.get_package_info",
            [this](const JniInvocation& call) { return GetPackageInfo(call); });
        installed_ = true;
        classes_set_ = {manager, info};
        return classes_set_;
    }

private:
    void ValidateConfig() const {
        if (config_.package_name.empty() || config_.package_name.size() > 255 ||
            config_.version_name.empty() || config_.version_code < 0) {
            InvalidConfig();
        }
        std::size_t cursor = 0;
        while (cursor < config_.package_name.size()) {
            const auto end = config_.package_name.find('.', cursor);
            const auto limit = end == std::string::npos
                                   ? config_.package_name.size()
                                   : end;
            if (limit == cursor ||
                !IsPackageStart(config_.package_name[cursor]) ||
                !std::all_of(config_.package_name.begin() +
                                 static_cast<std::ptrdiff_t>(cursor + 1),
                             config_.package_name.begin() +
                                 static_cast<std::ptrdiff_t>(limit),
                             IsPackagePart)) {
                InvalidConfig();
            }
            if (end == std::string::npos) break;
            cursor = end + 1;
        }
        if (std::find(config_.version_name.begin(), config_.version_name.end(),
                      u'\0') != config_.version_name.end()) {
            InvalidConfig();
        }
    }

    [[noreturn]] static void InvalidConfig() {
        Fail(FrameworkPackageErrorReason::invalid_config,
             "package identity configuration is invalid");
    }

    [[nodiscard]] JniObjectIdentity Resolve(
        const std::uint64_t thread_id, const JniReference reference) const {
        const auto identity =
            environment_->ResolveObjectForHle(thread_id, reference);
        if (!identity.has_value()) {
            Fail(FrameworkPackageErrorReason::invalid_argument,
                 "package HLE object argument cannot be null");
        }
        return *identity;
    }

    [[nodiscard]] JniValue GetPackageName(const JniInvocation& invocation) {
        static_cast<void>(Resolve(invocation.thread_id, invocation.receiver));
        return JniValue{environment_->PublishLocalObject(
            invocation.thread_id, package_name_string_)};
    }

    [[nodiscard]] JniValue GetManager(const JniInvocation& invocation) {
        static_cast<void>(Resolve(invocation.thread_id, invocation.receiver));
        return JniValue{environment_->PublishLocalObject(invocation.thread_id,
                                                         manager_)};
    }

    [[nodiscard]] JniReference VersionNameGlobal(
        const std::uint64_t thread_id) {
        std::scoped_lock lock(mutex_);
        if (version_name_global_.IsNull()) {
            version_name_global_ = environment_->PublishGlobalObjectForHle(
                thread_id, version_name_string_);
        }
        return version_name_global_;
    }

    [[nodiscard]] JniValue GetPackageInfo(const JniInvocation& invocation) {
        if (Resolve(invocation.thread_id, invocation.receiver) != manager_) {
            Fail(FrameworkPackageErrorReason::unknown_manager,
                 "PackageManager receiver is unknown");
        }
        if (std::get<JniInt>(invocation.arguments[1]) != 0) {
            Fail(FrameworkPackageErrorReason::unsupported_flags,
                 "PackageManager flags are not supported");
        }
        const auto name = Resolve(
            invocation.thread_id,
            std::get<JniReference>(invocation.arguments[0]));
        if (strings_->Region(name, 0, strings_->Length(name)) !=
            AsciiText(config_.package_name)) {
            Fail(FrameworkPackageErrorReason::unknown_package,
                 "PackageManager only exposes the current package");
        }
        const auto info = AllocateJniHostObjectIdentity();
        fields_->SetInstance(info, classes_set_.package_info_class,
                             version_name_field_,
                             VersionNameGlobal(invocation.thread_id));
        fields_->SetInstance(info, classes_set_.package_info_class,
                             version_code_field_, config_.version_code);
        try {
            return JniValue{environment_->PublishLocalObject(
                invocation.thread_id, info)};
        } catch (...) {
            fields_->DeleteInstanceFields(info);
            throw;
        }
    }

    JniClassRegistry* classes_{};
    JniInvocationEngine* invocations_{};
    JniEnvironment* environment_{};
    JniStringStore* strings_{};
    JniFieldStore* fields_{};
    FrameworkPackageConfig config_;
    JniObjectIdentity manager_;
    JniObjectIdentity package_name_string_;
    JniObjectIdentity version_name_string_;
    JniReference version_name_global_;
    JniFieldId version_name_field_;
    JniFieldId version_code_field_;
    FrameworkPackageClassSet classes_set_{};
    std::mutex mutex_;
    bool installed_{};
};

FrameworkPackageHle::FrameworkPackageHle(
    JniClassRegistry& classes, JniInvocationEngine& invocations,
    JniEnvironment& environment, JniStringStore& strings,
    JniFieldStore& fields, FrameworkPackageConfig config)
    : impl_(std::make_unique<Impl>(classes, invocations, environment, strings,
                                   fields, std::move(config))) {}
FrameworkPackageHle::~FrameworkPackageHle() = default;
FrameworkPackageHle::FrameworkPackageHle(FrameworkPackageHle&&) noexcept =
    default;
FrameworkPackageHle& FrameworkPackageHle::operator=(
    FrameworkPackageHle&&) noexcept = default;
FrameworkPackageClassSet FrameworkPackageHle::Install() {
    return impl_->Install();
}

}  // namespace ogplay::runtime

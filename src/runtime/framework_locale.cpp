#include "ogplay/runtime/framework_locale.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace ogplay::runtime {
namespace {

[[noreturn]] void Fail(const FrameworkLocaleErrorReason reason,
                       std::string message) {
    throw FrameworkLocaleError(reason, std::move(message));
}

[[nodiscard]] bool AllInRange(const std::string& value, const char first,
                              const char last) {
    return std::all_of(value.begin(), value.end(),
                       [first, last](const char character) {
                           return character >= first && character <= last;
                       });
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

}  // namespace

FrameworkLocaleError::FrameworkLocaleError(
    const FrameworkLocaleErrorReason reason, std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

FrameworkLocaleErrorReason FrameworkLocaleError::Reason() const noexcept {
    return reason_;
}

class FrameworkLocaleHle::Impl final {
public:
    Impl(JniClassRegistry& classes, JniInvocationEngine& invocations,
         JniEnvironment& environment, JniStringStore& strings,
         FrameworkLocaleConfig config)
        : classes_(&classes),
          invocations_(&invocations),
          environment_(&environment),
          strings_(&strings),
          config_(std::move(config)),
          locale_(AllocateJniHostObjectIdentity()) {}

    ~Impl() {
        for (const auto identity : strings_owned_) {
            try {
                strings_->Delete(identity);
            } catch (...) {
            }
        }
    }

    [[nodiscard]] JniObjectIdentity Install() {
        if (installed_) {
            Fail(FrameworkLocaleErrorReason::duplicate_install,
                 "framework Locale HLE is already installed");
        }
        if (!classes_->FindClass("java/lang/Object").has_value()) {
            Fail(FrameworkLocaleErrorReason::missing_framework,
                 "framework lifecycle classes must be installed first");
        }
        ValidateConfig();
        const auto locale_class = classes_->RegisterClass(
            {"java/util/Locale",
             "java/lang/Object",
             {{"getDefault", "()Ljava/util/Locale;",
               "framework.locale.get_default", true},
              {"getLanguage", "()Ljava/lang/String;",
               "framework.locale.get_language", false},
              {"getCountry", "()Ljava/lang/String;",
               "framework.locale.get_country", false},
              {"toString", "()Ljava/lang/String;",
               "framework.locale.to_string", false}},
             {}});
        language_ = CreateText(config_.language);
        country_ = CreateText(config_.country);
        const auto tag = config_.country.empty()
                             ? config_.language
                             : config_.language + "_" + config_.country;
        tag_ = CreateText(tag);
        invocations_->RegisterHandler(
            "framework.locale.get_default",
            [this](const JniInvocation& invocation) {
                return JniValue{environment_->PublishLocalObject(
                    invocation.thread_id, locale_)};
            });
        BindText("framework.locale.get_language", language_);
        BindText("framework.locale.get_country", country_);
        BindText("framework.locale.to_string", tag_);
        installed_ = true;
        locale_class_ = locale_class;
        return locale_class_;
    }

private:
    void ValidateConfig() const {
        if ((config_.language.size() != 2 && config_.language.size() != 3) ||
            !AllInRange(config_.language, 'a', 'z')) {
            Fail(FrameworkLocaleErrorReason::invalid_config,
                 "Locale language must be two or three lowercase ASCII letters");
        }
        if ((!config_.country.empty() && config_.country.size() != 2) ||
            !AllInRange(config_.country, 'A', 'Z')) {
            Fail(FrameworkLocaleErrorReason::invalid_config,
                 "Locale country must be empty or two uppercase ASCII letters");
        }
    }

    [[nodiscard]] JniObjectIdentity CreateText(const std::string& value) {
        const auto identity = strings_->Create(AsciiText(value));
        strings_owned_.push_back(identity);
        return identity;
    }

    void RequireLocale(const JniInvocation& invocation) const {
        const auto identity = environment_->ResolveObjectForHle(
            invocation.thread_id, invocation.receiver);
        if (!identity.has_value() || *identity != locale_) {
            Fail(FrameworkLocaleErrorReason::unknown_locale,
                 "Locale receiver is unknown");
        }
    }

    void BindText(const char* implementation,
                  const JniObjectIdentity text) {
        invocations_->RegisterHandler(
            implementation,
            [this, text](const JniInvocation& invocation) {
                RequireLocale(invocation);
                return JniValue{environment_->PublishLocalObject(
                    invocation.thread_id, text)};
            });
    }

    JniClassRegistry* classes_{};
    JniInvocationEngine* invocations_{};
    JniEnvironment* environment_{};
    JniStringStore* strings_{};
    FrameworkLocaleConfig config_;
    JniObjectIdentity locale_;
    JniObjectIdentity locale_class_;
    JniObjectIdentity language_;
    JniObjectIdentity country_;
    JniObjectIdentity tag_;
    std::vector<JniObjectIdentity> strings_owned_;
    bool installed_{};
};

FrameworkLocaleHle::FrameworkLocaleHle(
    JniClassRegistry& classes, JniInvocationEngine& invocations,
    JniEnvironment& environment, JniStringStore& strings,
    FrameworkLocaleConfig config)
    : impl_(std::make_unique<Impl>(classes, invocations, environment, strings,
                                   std::move(config))) {}
FrameworkLocaleHle::~FrameworkLocaleHle() = default;
FrameworkLocaleHle::FrameworkLocaleHle(FrameworkLocaleHle&&) noexcept =
    default;
FrameworkLocaleHle& FrameworkLocaleHle::operator=(
    FrameworkLocaleHle&&) noexcept = default;
JniObjectIdentity FrameworkLocaleHle::Install() { return impl_->Install(); }

}  // namespace ogplay::runtime

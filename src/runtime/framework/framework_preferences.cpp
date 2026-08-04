#include "ogplay/runtime/framework/framework_preferences.h"

#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace ogplay::runtime {
namespace {

using Text = std::vector<JniChar>;
using PreferenceValue =
    std::variant<JniInt, JniLong, JniBoolean, Text>;

[[noreturn]] void Fail(const FrameworkPreferencesErrorReason reason,
                       std::string message) {
    throw FrameworkPreferencesError(reason, std::move(message));
}

[[nodiscard]] JniValue VoidResult() { return JniValue{std::monostate{}}; }

}  // namespace

FrameworkPreferencesError::FrameworkPreferencesError(
    const FrameworkPreferencesErrorReason reason, std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

FrameworkPreferencesErrorReason FrameworkPreferencesError::Reason() const
    noexcept {
    return reason_;
}

class FrameworkPreferencesHle::Impl final {
public:
    Impl(JniClassRegistry& classes, JniInvocationEngine& invocations,
         JniEnvironment& environment, JniStringStore& strings)
        : classes_(&classes),
          invocations_(&invocations),
          environment_(&environment),
          strings_(&strings) {}

    ~Impl() {
        for (const auto identity : returned_strings_) {
            try {
                strings_->Delete(identity);
            } catch (...) {
            }
        }
    }

    [[nodiscard]] FrameworkPreferencesClassSet Install() {
        if (installed_) {
            Fail(FrameworkPreferencesErrorReason::duplicate_install,
                 "framework preferences HLE is already installed");
        }
        if (!classes_->FindClass("java/lang/Object").has_value() ||
            !classes_->FindClass("android/content/Context").has_value()) {
            Fail(FrameworkPreferencesErrorReason::missing_framework,
                 "framework lifecycle classes must be installed first");
        }
        const auto preferences = classes_->RegisterClass(
            {"android/content/SharedPreferences",
             "java/lang/Object",
             {{"getInt", "(Ljava/lang/String;I)I",
               "framework.preferences.get_int", false},
              {"getLong", "(Ljava/lang/String;J)J",
               "framework.preferences.get_long", false},
              {"getBoolean", "(Ljava/lang/String;Z)Z",
               "framework.preferences.get_boolean", false},
              {"getString",
               "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
               "framework.preferences.get_string", false},
              {"contains", "(Ljava/lang/String;)Z",
               "framework.preferences.contains", false},
              {"edit", "()Landroid/content/SharedPreferences$Editor;",
               "framework.preferences.edit", false}},
             {}});
        const auto editor = classes_->RegisterClass(
            {"android/content/SharedPreferences$Editor",
             "java/lang/Object",
             {{"putInt",
               "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;",
               "framework.preferences.editor.put_int", false},
              {"putLong",
               "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;",
               "framework.preferences.editor.put_long", false},
              {"putBoolean",
               "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;",
               "framework.preferences.editor.put_boolean", false},
              {"putString",
               "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
               "framework.preferences.editor.put_string", false},
              {"remove",
               "(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
               "framework.preferences.editor.remove", false},
              {"clear", "()Landroid/content/SharedPreferences$Editor;",
               "framework.preferences.editor.clear", false},
              {"commit", "()Z", "framework.preferences.editor.commit", false},
              {"apply", "()V", "framework.preferences.editor.apply", false}},
             {}});
        BindHandlers();
        installed_ = true;
        classes_set_ = {preferences, editor};
        return classes_set_;
    }

private:
    struct Change final {
        std::optional<PreferenceValue> value;
    };

    struct Editor final {
        std::uint64_t preferences{};
        bool clear{};
        std::map<Text, Change> changes;
    };

    [[nodiscard]] JniObjectIdentity Resolve(
        const std::uint64_t thread_id, const JniReference reference) const {
        const auto identity =
            environment_->ResolveObjectForHle(thread_id, reference);
        if (!identity.has_value()) {
            Fail(FrameworkPreferencesErrorReason::invalid_argument,
                 "preferences object argument cannot be null");
        }
        return *identity;
    }

    [[nodiscard]] Text ReadText(const std::uint64_t thread_id,
                                const JniReference reference) const {
        const auto identity = Resolve(thread_id, reference);
        return strings_->Region(identity, 0, strings_->Length(identity));
    }

    [[nodiscard]] std::uint64_t PreferencesId(
        const JniInvocation& invocation) const {
        const auto identity =
            Resolve(invocation.thread_id, invocation.receiver);
        std::scoped_lock lock(mutex_);
        if (identity.domain != JniObjectDomain::host ||
            !preference_names_.contains(identity.value)) {
            Fail(FrameworkPreferencesErrorReason::unknown_preferences,
                 "SharedPreferences receiver is unknown");
        }
        return identity.value;
    }

    [[nodiscard]] Editor& RequireEditor(const JniInvocation& invocation) {
        const auto identity =
            Resolve(invocation.thread_id, invocation.receiver);
        if (identity.domain != JniObjectDomain::host) {
            Fail(FrameworkPreferencesErrorReason::unknown_editor,
                 "SharedPreferences.Editor receiver is unknown");
        }
        const auto found = editors_.find(identity.value);
        if (found == editors_.end()) {
            Fail(FrameworkPreferencesErrorReason::unknown_editor,
                 "SharedPreferences.Editor receiver is unknown");
        }
        return found->second;
    }

    [[nodiscard]] JniValue GetPreferences(const JniInvocation& invocation) {
        static_cast<void>(
            Resolve(invocation.thread_id, invocation.receiver));
        const auto name = ReadText(
            invocation.thread_id,
            std::get<JniReference>(invocation.arguments[0]));
        if (std::get<JniInt>(invocation.arguments[1]) != 0) {
            Fail(FrameworkPreferencesErrorReason::unsupported_mode,
                 "only Context.MODE_PRIVATE preferences are supported");
        }
        std::scoped_lock lock(mutex_);
        auto found = preference_identities_.find(name);
        if (found == preference_identities_.end()) {
            const auto identity = AllocateJniHostObjectIdentity();
            found = preference_identities_.emplace(name, identity).first;
            preference_names_.emplace(identity.value, name);
            values_.try_emplace(identity.value);
        }
        return JniValue{environment_->PublishLocalObject(invocation.thread_id,
                                                         found->second)};
    }

    template <typename Value>
    [[nodiscard]] JniValue GetPrimitive(const JniInvocation& invocation) {
        const auto preferences = PreferencesId(invocation);
        const auto key = ReadText(
            invocation.thread_id,
            std::get<JniReference>(invocation.arguments[0]));
        const auto fallback = std::get<Value>(invocation.arguments[1]);
        std::scoped_lock lock(mutex_);
        const auto found = values_.at(preferences).find(key);
        if (found == values_.at(preferences).end()) return JniValue{fallback};
        const auto stored = std::get_if<Value>(&found->second);
        if (stored == nullptr) WrongType();
        return JniValue{*stored};
    }

    [[nodiscard]] JniValue GetString(const JniInvocation& invocation) {
        const auto preferences = PreferencesId(invocation);
        const auto key = ReadText(
            invocation.thread_id,
            std::get<JniReference>(invocation.arguments[0]));
        const auto fallback =
            std::get<JniReference>(invocation.arguments[1]);
        Text stored;
        {
            std::scoped_lock lock(mutex_);
            const auto found = values_.at(preferences).find(key);
            if (found == values_.at(preferences).end()) {
                return JniValue{fallback};
            }
            const auto text = std::get_if<Text>(&found->second);
            if (text == nullptr) WrongType();
            stored = *text;
        }
        const auto identity = strings_->Create(stored);
        {
            std::scoped_lock lock(mutex_);
            returned_strings_.push_back(identity);
        }
        return JniValue{
            environment_->PublishLocalObject(invocation.thread_id, identity)};
    }

    [[nodiscard]] JniValue Contains(const JniInvocation& invocation) {
        const auto preferences = PreferencesId(invocation);
        const auto key = ReadText(
            invocation.thread_id,
            std::get<JniReference>(invocation.arguments[0]));
        std::scoped_lock lock(mutex_);
        return JniValue{static_cast<JniBoolean>(
            values_.at(preferences).contains(key) ? 1 : 0)};
    }

    [[nodiscard]] JniValue Edit(const JniInvocation& invocation) {
        const auto preferences = PreferencesId(invocation);
        const auto identity = AllocateJniHostObjectIdentity();
        {
            std::scoped_lock lock(mutex_);
            editors_.emplace(identity.value, Editor{preferences, false, {}});
        }
        return JniValue{environment_->PublishLocalObject(invocation.thread_id,
                                                         identity)};
    }

    template <typename Value>
    [[nodiscard]] JniValue PutPrimitive(const JniInvocation& invocation) {
        const auto key = ReadText(
            invocation.thread_id,
            std::get<JniReference>(invocation.arguments[0]));
        const auto value = std::get<Value>(invocation.arguments[1]);
        std::scoped_lock lock(mutex_);
        RequireEditor(invocation).changes[key] = Change{PreferenceValue{value}};
        return JniValue{invocation.receiver};
    }

    [[nodiscard]] JniValue PutString(const JniInvocation& invocation) {
        const auto key = ReadText(
            invocation.thread_id,
            std::get<JniReference>(invocation.arguments[0]));
        const auto reference =
            std::get<JniReference>(invocation.arguments[1]);
        std::optional<PreferenceValue> value;
        if (!reference.IsNull()) {
            value = PreferenceValue{ReadText(invocation.thread_id, reference)};
        }
        std::scoped_lock lock(mutex_);
        RequireEditor(invocation).changes[key] = Change{std::move(value)};
        return JniValue{invocation.receiver};
    }

    [[nodiscard]] JniValue Remove(const JniInvocation& invocation) {
        const auto key = ReadText(
            invocation.thread_id,
            std::get<JniReference>(invocation.arguments[0]));
        std::scoped_lock lock(mutex_);
        RequireEditor(invocation).changes[key] = Change{std::nullopt};
        return JniValue{invocation.receiver};
    }

    [[nodiscard]] JniValue Clear(const JniInvocation& invocation) {
        std::scoped_lock lock(mutex_);
        RequireEditor(invocation).clear = true;
        return JniValue{invocation.receiver};
    }

    void ApplyChanges(Editor& editor) {
        auto& target = values_.at(editor.preferences);
        if (editor.clear) target.clear();
        for (auto& [key, change] : editor.changes) {
            if (change.value.has_value()) {
                target[key] = std::move(*change.value);
            } else {
                target.erase(key);
            }
        }
        editor.clear = false;
        editor.changes.clear();
    }

    [[nodiscard]] JniValue Commit(const JniInvocation& invocation) {
        std::scoped_lock lock(mutex_);
        ApplyChanges(RequireEditor(invocation));
        return JniValue{JniBoolean{1}};
    }

    [[nodiscard]] JniValue Apply(const JniInvocation& invocation) {
        std::scoped_lock lock(mutex_);
        ApplyChanges(RequireEditor(invocation));
        return VoidResult();
    }

    [[noreturn]] static void WrongType() {
        Fail(FrameworkPreferencesErrorReason::type_mismatch,
             "stored preference type does not match getter");
    }

    void BindHandlers() {
        invocations_->RegisterHandler(
            "framework.context.get_shared_preferences",
            [this](const JniInvocation& call) { return GetPreferences(call); });
        invocations_->RegisterHandler(
            "framework.preferences.get_int",
            [this](const JniInvocation& call) {
                return GetPrimitive<JniInt>(call);
            });
        invocations_->RegisterHandler(
            "framework.preferences.get_long",
            [this](const JniInvocation& call) {
                return GetPrimitive<JniLong>(call);
            });
        invocations_->RegisterHandler(
            "framework.preferences.get_boolean",
            [this](const JniInvocation& call) {
                return GetPrimitive<JniBoolean>(call);
            });
        invocations_->RegisterHandler(
            "framework.preferences.get_string",
            [this](const JniInvocation& call) { return GetString(call); });
        invocations_->RegisterHandler(
            "framework.preferences.contains",
            [this](const JniInvocation& call) { return Contains(call); });
        invocations_->RegisterHandler(
            "framework.preferences.edit",
            [this](const JniInvocation& call) { return Edit(call); });
        invocations_->RegisterHandler(
            "framework.preferences.editor.put_int",
            [this](const JniInvocation& call) {
                return PutPrimitive<JniInt>(call);
            });
        invocations_->RegisterHandler(
            "framework.preferences.editor.put_long",
            [this](const JniInvocation& call) {
                return PutPrimitive<JniLong>(call);
            });
        invocations_->RegisterHandler(
            "framework.preferences.editor.put_boolean",
            [this](const JniInvocation& call) {
                return PutPrimitive<JniBoolean>(call);
            });
        invocations_->RegisterHandler(
            "framework.preferences.editor.put_string",
            [this](const JniInvocation& call) { return PutString(call); });
        invocations_->RegisterHandler(
            "framework.preferences.editor.remove",
            [this](const JniInvocation& call) { return Remove(call); });
        invocations_->RegisterHandler(
            "framework.preferences.editor.clear",
            [this](const JniInvocation& call) { return Clear(call); });
        invocations_->RegisterHandler(
            "framework.preferences.editor.commit",
            [this](const JniInvocation& call) { return Commit(call); });
        invocations_->RegisterHandler(
            "framework.preferences.editor.apply",
            [this](const JniInvocation& call) { return Apply(call); });
    }

    JniClassRegistry* classes_{};
    JniInvocationEngine* invocations_{};
    JniEnvironment* environment_{};
    JniStringStore* strings_{};
    bool installed_{};
    FrameworkPreferencesClassSet classes_set_{};
    mutable std::mutex mutex_;
    std::map<Text, JniObjectIdentity> preference_identities_;
    std::map<std::uint64_t, Text> preference_names_;
    std::map<std::uint64_t, std::map<Text, PreferenceValue>> values_;
    std::map<std::uint64_t, Editor> editors_;
    std::vector<JniObjectIdentity> returned_strings_;
};

FrameworkPreferencesHle::FrameworkPreferencesHle(
    JniClassRegistry& classes, JniInvocationEngine& invocations,
    JniEnvironment& environment, JniStringStore& strings)
    : impl_(std::make_unique<Impl>(classes, invocations, environment,
                                   strings)) {}
FrameworkPreferencesHle::~FrameworkPreferencesHle() = default;
FrameworkPreferencesHle::FrameworkPreferencesHle(
    FrameworkPreferencesHle&&) noexcept = default;
FrameworkPreferencesHle& FrameworkPreferencesHle::operator=(
    FrameworkPreferencesHle&&) noexcept = default;
FrameworkPreferencesClassSet FrameworkPreferencesHle::Install() {
    return impl_->Install();
}

}  // namespace ogplay::runtime

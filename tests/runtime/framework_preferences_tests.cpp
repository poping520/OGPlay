#include <array>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/framework/framework_lifecycle.h"
#include "ogplay/runtime/framework/framework_preferences.h"

namespace {

using namespace ogplay::runtime;

[[nodiscard]] JniMethodId Method(const JniClassRegistry& classes,
                                 const JniObjectIdentity type,
                                 const char* name, const char* descriptor) {
    return *classes.GetMethodId(type, name, descriptor, false);
}

[[nodiscard]] JniReference ReferenceResult(const JniValue& value) {
    return std::get<JniReference>(value);
}

struct TextReference final {
    JniObjectIdentity identity;
    JniReference reference;
};

[[nodiscard]] TextReference Text(JniStringStore& strings,
                                 JniEnvironment& environment,
                                 const std::uint64_t thread,
                                 const std::vector<JniChar>& value) {
    const auto identity = strings.Create(value);
    return {identity, environment.PublishLocalObject(thread, identity)};
}

}  // namespace

TEST_CASE("framework SharedPreferences commits typed values by name") {
    JniClassRegistry classes;
    JniInvocationEngine invocations(classes);
    JniEnvironment environment;
    JniStringStore strings;
    FrameworkLifecycleHle lifecycle(classes, invocations);
    const auto framework = lifecycle.Install();
    FrameworkPreferencesHle preferences_hle(classes, invocations, environment,
                                            strings);
    const auto preference_classes = preferences_hle.Install();

    constexpr std::uint64_t thread = 51;
    environment.AttachThread(thread, 32);
    const auto activity = environment.PublishLocalObject(
        thread, AllocateJniHostObjectIdentity());
    const auto name = Text(strings, environment, thread,
                           {'p', 'l', 'a', 'y', 'e', 'r'});
    const auto score = Text(strings, environment, thread,
                            {'s', 'c', 'o', 'r', 'e'});
    const auto enabled = Text(strings, environment, thread,
                              {'e', 'n', 'a', 'b', 'l', 'e', 'd'});
    const auto title = Text(strings, environment, thread,
                            {'t', 'i', 't', 'l', 'e'});
    const auto title_value = Text(strings, environment, thread,
                                  {'O', 'G', 'P', 'l', 'a', 'y'});
    const std::array<JniValue, 2> context_arguments{name.reference, JniInt{0}};
    const auto preferences = ReferenceResult(invocations.InvokeVirtual(
        thread, activity, framework.activity_class,
        Method(classes, framework.activity_class, "getSharedPreferences",
               "(Ljava/lang/String;I)Landroid/content/SharedPreferences;"),
        context_arguments, JniArgumentSource::value_array));

    const auto get_int = Method(classes,
                                preference_classes.shared_preferences_class,
                                "getInt", "(Ljava/lang/String;I)I");
    const std::array<JniValue, 2> default_score{score.reference, JniInt{7}};
    CHECK(std::get<JniInt>(invocations.InvokeVirtual(
              thread, preferences,
              preference_classes.shared_preferences_class, get_int,
              default_score, JniArgumentSource::value_array)) == 7);
    const auto editor = ReferenceResult(invocations.InvokeVirtual(
        thread, preferences, preference_classes.shared_preferences_class,
        Method(classes, preference_classes.shared_preferences_class, "edit",
               "()Landroid/content/SharedPreferences$Editor;"),
        {}, JniArgumentSource::value_array));

    const std::array<JniValue, 2> put_score{score.reference, JniInt{99}};
    CHECK(ReferenceResult(invocations.InvokeVirtual(
              thread, editor, preference_classes.editor_class,
              Method(classes, preference_classes.editor_class, "putInt",
                     "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;"),
              put_score, JniArgumentSource::value_array)) == editor);
    const std::array<JniValue, 2> put_enabled{enabled.reference,
                                              JniBoolean{1}};
    static_cast<void>(invocations.InvokeVirtual(
        thread, editor, preference_classes.editor_class,
        Method(classes, preference_classes.editor_class, "putBoolean",
               "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;"),
        put_enabled, JniArgumentSource::value_array));
    const std::array<JniValue, 2> put_title{title.reference,
                                            title_value.reference};
    static_cast<void>(invocations.InvokeVirtual(
        thread, editor, preference_classes.editor_class,
        Method(classes, preference_classes.editor_class, "putString",
               "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;"),
        put_title, JniArgumentSource::value_array));
    CHECK(std::get<JniBoolean>(invocations.InvokeVirtual(
              thread, editor, preference_classes.editor_class,
              Method(classes, preference_classes.editor_class, "commit",
                     "()Z"),
              {}, JniArgumentSource::value_array)) == 1);

    CHECK(std::get<JniInt>(invocations.InvokeVirtual(
              thread, preferences,
              preference_classes.shared_preferences_class, get_int,
              default_score, JniArgumentSource::value_array)) == 99);
    const std::array<JniValue, 2> default_enabled{enabled.reference,
                                                  JniBoolean{0}};
    CHECK(std::get<JniBoolean>(invocations.InvokeVirtual(
              thread, preferences,
              preference_classes.shared_preferences_class,
              Method(classes, preference_classes.shared_preferences_class,
                     "getBoolean", "(Ljava/lang/String;Z)Z"),
              default_enabled, JniArgumentSource::value_array)) == 1);
    const std::array<JniValue, 2> get_title{title.reference, JniReference{}};
    const auto stored_title = ReferenceResult(invocations.InvokeVirtual(
        thread, preferences, preference_classes.shared_preferences_class,
        Method(classes, preference_classes.shared_preferences_class,
               "getString",
               "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"),
        get_title, JniArgumentSource::value_array));
    const auto stored_identity =
        *environment.ResolveObjectForHle(thread, stored_title);
    CHECK(strings.Region(stored_identity, 0, strings.Length(stored_identity)) ==
          std::vector<JniChar>{'O', 'G', 'P', 'l', 'a', 'y'});

    environment.DetachThread(thread);
    for (const auto identity : {name.identity, score.identity, enabled.identity,
                                title.identity, title_value.identity}) {
        strings.Delete(identity);
    }
}

TEST_CASE("framework SharedPreferences applies remove clear and type checks") {
    JniClassRegistry classes;
    JniInvocationEngine invocations(classes);
    JniEnvironment environment;
    JniStringStore strings;
    FrameworkLifecycleHle lifecycle(classes, invocations);
    const auto framework = lifecycle.Install();
    FrameworkPreferencesHle preferences_hle(classes, invocations, environment,
                                            strings);
    const auto preference_classes = preferences_hle.Install();
    CHECK_THROWS_AS(static_cast<void>(preferences_hle.Install()),
                    FrameworkPreferencesError);

    constexpr std::uint64_t thread = 52;
    environment.AttachThread(thread, 24);
    const auto activity = environment.PublishLocalObject(
        thread, AllocateJniHostObjectIdentity());
    const auto activity_global = environment.NewGlobalRef(thread, activity);
    const auto name = Text(strings, environment, thread, {'s', 't', 'a', 't', 'e'});
    const auto key = Text(strings, environment, thread, {'k', 'e', 'y'});
    const std::array<JniValue, 2> bad_mode{name.reference, JniInt{1}};
    const auto get_preferences = Method(
        classes, framework.activity_class, "getSharedPreferences",
        "(Ljava/lang/String;I)Landroid/content/SharedPreferences;");
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            thread, JniReference{999}, framework.activity_class,
            get_preferences, bad_mode, JniArgumentSource::value_array)),
        JniReferenceError);
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            thread, activity, framework.activity_class, get_preferences,
            bad_mode, JniArgumentSource::value_array)),
        FrameworkPreferencesError);
    const std::array<JniValue, 2> private_mode{name.reference, JniInt{0}};
    const auto preferences = ReferenceResult(invocations.InvokeVirtual(
        thread, activity_global, framework.activity_class, get_preferences,
        private_mode, JniArgumentSource::value_array));
    const auto editor = ReferenceResult(invocations.InvokeVirtual(
        thread, preferences, preference_classes.shared_preferences_class,
        Method(classes, preference_classes.shared_preferences_class, "edit",
               "()Landroid/content/SharedPreferences$Editor;"),
        {}, JniArgumentSource::value_array));
    const std::array<JniValue, 2> put{key.reference, JniInt{3}};
    static_cast<void>(invocations.InvokeVirtual(
        thread, editor, preference_classes.editor_class,
        Method(classes, preference_classes.editor_class, "putInt",
               "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;"),
        put, JniArgumentSource::value_array));
    static_cast<void>(invocations.InvokeVirtual(
        thread, editor, preference_classes.editor_class,
        Method(classes, preference_classes.editor_class, "apply", "()V"), {},
        JniArgumentSource::value_array));
    const std::array<JniValue, 1> key_argument{key.reference};
    CHECK(std::get<JniBoolean>(invocations.InvokeVirtual(
              thread, preferences,
              preference_classes.shared_preferences_class,
              Method(classes, preference_classes.shared_preferences_class,
                     "contains", "(Ljava/lang/String;)Z"),
              key_argument, JniArgumentSource::value_array)) == 1);
    const std::array<JniValue, 2> wrong_get{key.reference, JniBoolean{0}};
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            thread, preferences,
            preference_classes.shared_preferences_class,
            Method(classes, preference_classes.shared_preferences_class,
                   "getBoolean", "(Ljava/lang/String;Z)Z"),
            wrong_get, JniArgumentSource::value_array)),
        FrameworkPreferencesError);
    static_cast<void>(invocations.InvokeVirtual(
        thread, editor, preference_classes.editor_class,
        Method(classes, preference_classes.editor_class, "clear",
               "()Landroid/content/SharedPreferences$Editor;"),
        {}, JniArgumentSource::value_array));
    static_cast<void>(invocations.InvokeVirtual(
        thread, editor, preference_classes.editor_class,
        Method(classes, preference_classes.editor_class, "commit", "()Z"),
        {}, JniArgumentSource::value_array));
    CHECK(std::get<JniBoolean>(invocations.InvokeVirtual(
              thread, preferences,
              preference_classes.shared_preferences_class,
              Method(classes, preference_classes.shared_preferences_class,
                     "contains", "(Ljava/lang/String;)Z"),
              key_argument, JniArgumentSource::value_array)) == 0);

    environment.DeleteGlobalRef(thread, activity_global);
    environment.DetachThread(thread);
    strings.Delete(name.identity);
    strings.Delete(key.identity);
}

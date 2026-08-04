#include "ogplay/runtime/headless_jni_contract.h"

#include <array>
#include <cstddef>
#include <exception>
#include <optional>
#include <utility>
#include <vector>

#include "ogplay/hal/thread.h"
#include "ogplay/runtime/framework_asset.h"
#include "ogplay/runtime/framework_lifecycle.h"
#include "ogplay/runtime/framework_locale.h"
#include "ogplay/runtime/framework_package.h"
#include "ogplay/runtime/framework_preferences.h"
#include "ogplay/runtime/jni_array.h"
#include "ogplay/runtime/jni_environment.h"
#include "ogplay/runtime/jni_field_store.h"
#include "ogplay/runtime/jni_java_vm.h"
#include "ogplay/runtime/jni_native_registry.h"
#include "ogplay/runtime/jni_object.h"
#include "ogplay/runtime/jni_object_array.h"

namespace ogplay::runtime {
namespace {

constexpr std::uint64_t kContractThread = 1;
constexpr memory::GuestAddress kNativeStep{0x72001000};

[[noreturn]] void Fail(const HeadlessJniContractErrorReason reason,
                       const char* message) {
    throw HeadlessJniContractError(reason, message);
}

[[nodiscard]] JniMethodId Method(const JniClassRegistry& classes,
                                 const JniObjectIdentity java_class,
                                 const char* name, const char* descriptor,
                                 const bool is_static = false) {
    const auto method =
        classes.GetMethodId(java_class, name, descriptor, is_static);
    if (!method.has_value()) {
        Fail(HeadlessJniContractErrorReason::invariant_failed,
             "headless JNI contract method is missing");
    }
    return *method;
}

[[nodiscard]] JniReference ReferenceResult(const JniValue& value) {
    return std::get<JniReference>(value);
}

void InvokeLifecycle(const JniClassRegistry& classes,
                     const JniInvocationEngine& invocations,
                     const JniObjectIdentity activity_class,
                     const JniReference activity, const char* name,
                     const char* descriptor,
                     std::span<const JniValue> arguments = {}) {
    static_cast<void>(invocations.InvokeVirtual(
        kContractThread, activity, activity_class,
        Method(classes, activity_class, name, descriptor), arguments,
        JniArgumentSource::value_array));
}

class DetachGuard final {
public:
    explicit DetachGuard(JniJavaVm& vm) : vm_(&vm) {}
    ~DetachGuard() {
        if (vm_ != nullptr) {
            static_cast<void>(vm_->DetachCurrentThread(kContractThread));
        }
    }
    void Disarm() noexcept { vm_ = nullptr; }

private:
    JniJavaVm* vm_{};
};

}  // namespace

HeadlessJniContractError::HeadlessJniContractError(
    const HeadlessJniContractErrorReason reason, const char* message)
    : std::runtime_error(message), reason_(reason) {}

HeadlessJniContractErrorReason HeadlessJniContractError::Reason()
    const noexcept {
    return reason_;
}

HeadlessJniContractReport RunHeadlessJniContract(
    const GuestNativeExecutor& execute_native) {
    if (!execute_native) {
        Fail(HeadlessJniContractErrorReason::missing_executor,
             "headless JNI contract requires a guest native executor");
    }

    HeadlessJniContractReport report;
    JniEnvironment environment;
    JniJavaVm vm(environment);
    const auto attached =
        vm.AttachCurrentThread(kContractThread, kJniVersion1_6, 64);
    if (attached.status != JniStatus::ok || attached.environment.IsNull()) {
        Fail(HeadlessJniContractErrorReason::invariant_failed,
             "headless JNI contract could not attach JavaVM thread");
    }
    DetachGuard detach(vm);
    report.trace.emplace_back("vm.attach");

    JniClassRegistry classes;
    JniInvocationEngine invocations(classes);
    FrameworkLifecycleHle lifecycle(classes, invocations);
    const auto framework = lifecycle.Install();
    JniStringStore strings;
    JniPrimitiveArrayStore primitive_arrays;
    JniFieldStore fields(classes);
    VirtualFileSystem vfs;
    const std::vector<VfsMountEntry> apk{{
        "assets/contract.bin", {std::byte{4}, std::byte{2}},
    }};
    vfs.Mount(VfsSource::apk, "/apk", apk);
    FrameworkAssetHle assets(classes, invocations, environment, strings,
                             primitive_arrays, vfs);
    const auto asset_classes = assets.Install();
    FrameworkPreferencesHle preferences(classes, invocations, environment,
                                        strings);
    const auto preference_classes = preferences.Install();
    FrameworkLocaleHle locale(classes, invocations, environment, strings,
                              {"zh", "CN"});
    const auto locale_class = locale.Install();
    FrameworkPackageHle packages(
        classes, invocations, environment, strings, fields,
        {"org.ogplay.contract", u"3.0", JniInt{3}});
    const auto package_classes = packages.Install();
    const auto peer_class = classes.RegisterClass(
        {"org/ogplay/contract/HeadlessPeer", "java/lang/Object",
         {{"nativeStep", "(I)I", "contract.native_step", false}},
         {{"result", "I", "contract.result", false},
          {"runs", "I", "contract.runs", true}}});

    const auto activity_object = AllocateJniHostObjectIdentity();
    const auto peer_object = AllocateJniHostObjectIdentity();
    const auto throwable_object = AllocateJniHostObjectIdentity();
    const auto activity = environment.PublishLocalObject(
        kContractThread, activity_object);
    const auto peer =
        environment.PublishLocalObject(kContractThread, peer_object);
    const auto throwable = environment.PublishLocalObject(
        kContractThread, throwable_object);

    static_cast<void>(invocations.InvokeNonvirtual(
        kContractThread, activity, framework.activity_class,
        framework.activity_class,
        Method(classes, framework.activity_class, "<init>", "()V"), {},
        JniArgumentSource::value_array));
    report.trace.emplace_back("hle.construct");
    const std::array create_arguments{JniValue{JniReference{}}};
    InvokeLifecycle(classes, invocations, framework.activity_class, activity,
                    "onCreate", "(Landroid/os/Bundle;)V", create_arguments);
    report.trace.emplace_back("hle.create");

    std::vector<JniObjectIdentity> service_strings;
    const auto publish_text = [&](const std::vector<JniChar>& text) {
        const auto identity = strings.Create(text);
        service_strings.push_back(identity);
        return environment.PublishLocalObject(kContractThread, identity);
    };
    const auto read_text = [&](const JniReference reference) {
        const auto identity =
            environment.ResolveObjectForHle(kContractThread, reference);
        if (!identity.has_value()) {
            Fail(HeadlessJniContractErrorReason::invariant_failed,
                 "framework service returned a null string");
        }
        return strings.Region(*identity, 0, strings.Length(*identity));
    };

    const auto asset_name = publish_text(
        {'c', 'o', 'n', 't', 'r', 'a', 'c', 't', '.', 'b', 'i', 'n'});
    const auto service_bytes =
        primitive_arrays.New(JniPrimitiveKind::byte, 2);
    const auto service_bytes_reference =
        environment.PublishLocalObject(kContractThread, service_bytes);
    const auto asset_manager = ReferenceResult(invocations.InvokeVirtual(
        kContractThread, activity, framework.activity_class,
        Method(classes, framework.activity_class, "getAssets",
               "()Landroid/content/res/AssetManager;"),
        {}, JniArgumentSource::value_array));
    const std::array<JniValue, 1> open_arguments{asset_name};
    const auto stream = ReferenceResult(invocations.InvokeVirtual(
        kContractThread, asset_manager, asset_classes.asset_manager_class,
        Method(classes, asset_classes.asset_manager_class, "open",
               "(Ljava/lang/String;)Ljava/io/InputStream;"),
        open_arguments, JniArgumentSource::value_array));
    const std::array<JniValue, 1> read_arguments{service_bytes_reference};
    const auto read_count = std::get<JniInt>(invocations.InvokeVirtual(
        kContractThread, stream, asset_classes.input_stream_class,
        Method(classes, asset_classes.input_stream_class, "read", "([B)I"),
        read_arguments, JniArgumentSource::value_array));
    static_cast<void>(invocations.InvokeVirtual(
        kContractThread, stream, asset_classes.input_stream_class,
        Method(classes, asset_classes.input_stream_class, "close", "()V"),
        {}, JniArgumentSource::value_array));
    report.asset_round_trip =
        read_count == 2 &&
        std::get<std::vector<JniByte>>(
            primitive_arrays.Region(service_bytes, 0, 2)) ==
            std::vector<JniByte>{4, 2};
    report.trace.emplace_back("framework.asset");

    const auto preferences_name =
        publish_text({'c', 'o', 'n', 't', 'r', 'a', 'c', 't'});
    const auto score_key = publish_text({'s', 'c', 'o', 'r', 'e'});
    const std::array<JniValue, 2> preferences_arguments{preferences_name,
                                                        JniInt{0}};
    const auto preferences_object = ReferenceResult(invocations.InvokeVirtual(
        kContractThread, activity, framework.activity_class,
        Method(classes, framework.activity_class, "getSharedPreferences",
               "(Ljava/lang/String;I)Landroid/content/SharedPreferences;"),
        preferences_arguments, JniArgumentSource::value_array));
    const auto editor = ReferenceResult(invocations.InvokeVirtual(
        kContractThread, preferences_object,
        preference_classes.shared_preferences_class,
        Method(classes, preference_classes.shared_preferences_class, "edit",
               "()Landroid/content/SharedPreferences$Editor;"),
        {}, JniArgumentSource::value_array));
    const std::array<JniValue, 2> put_score{score_key, JniInt{42}};
    static_cast<void>(invocations.InvokeVirtual(
        kContractThread, editor, preference_classes.editor_class,
        Method(classes, preference_classes.editor_class, "putInt",
               "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;"),
        put_score, JniArgumentSource::value_array));
    const auto committed = std::get<JniBoolean>(invocations.InvokeVirtual(
        kContractThread, editor, preference_classes.editor_class,
        Method(classes, preference_classes.editor_class, "commit", "()Z"),
        {}, JniArgumentSource::value_array));
    const std::array<JniValue, 2> get_score{score_key, JniInt{0}};
    const auto stored_score = std::get<JniInt>(invocations.InvokeVirtual(
        kContractThread, preferences_object,
        preference_classes.shared_preferences_class,
        Method(classes, preference_classes.shared_preferences_class, "getInt",
               "(Ljava/lang/String;I)I"),
        get_score, JniArgumentSource::value_array));
    report.preferences_round_trip = committed == 1 && stored_score == 42;
    report.trace.emplace_back("framework.preferences");

    const auto locale_object = ReferenceResult(invocations.InvokeStatic(
        kContractThread, locale_class,
        Method(classes, locale_class, "getDefault", "()Ljava/util/Locale;",
               true),
        {}, JniArgumentSource::value_array));
    const auto locale_text = ReferenceResult(invocations.InvokeVirtual(
        kContractThread, locale_object, locale_class,
        Method(classes, locale_class, "toString", "()Ljava/lang/String;"),
        {}, JniArgumentSource::value_array));
    report.locale_round_trip =
        read_text(locale_text) ==
        std::vector<JniChar>{'z', 'h', '_', 'C', 'N'};
    report.trace.emplace_back("framework.locale");

    const auto package_name = ReferenceResult(invocations.InvokeVirtual(
        kContractThread, activity, framework.activity_class,
        Method(classes, framework.activity_class, "getPackageName",
               "()Ljava/lang/String;"),
        {}, JniArgumentSource::value_array));
    const auto package_manager = ReferenceResult(invocations.InvokeVirtual(
        kContractThread, activity, framework.activity_class,
        Method(classes, framework.activity_class, "getPackageManager",
               "()Landroid/content/pm/PackageManager;"),
        {}, JniArgumentSource::value_array));
    const std::array<JniValue, 2> package_arguments{package_name, JniInt{0}};
    const auto package_info_reference = ReferenceResult(
        invocations.InvokeVirtual(
            kContractThread, package_manager,
            package_classes.package_manager_class,
            Method(classes, package_classes.package_manager_class,
                   "getPackageInfo",
                   "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;"),
            package_arguments, JniArgumentSource::value_array));
    const auto package_info = *environment.ResolveObjectForHle(
        kContractThread, package_info_reference);
    const auto version_name = *classes.GetFieldId(
        package_classes.package_info_class, "versionName",
        "Ljava/lang/String;", false);
    const auto version_code = *classes.GetFieldId(
        package_classes.package_info_class, "versionCode", "I", false);
    const auto version_name_reference = std::get<JniReference>(
        fields.GetInstance(package_info, package_classes.package_info_class,
                           version_name));
    report.package_round_trip =
        read_text(package_name) ==
            std::vector<JniChar>{'o', 'r', 'g', '.', 'o', 'g', 'p', 'l', 'a',
                                 'y', '.', 'c', 'o', 'n', 't', 'r', 'a', 'c',
                                 't'} &&
        read_text(version_name_reference) ==
            std::vector<JniChar>{'3', '.', '0'} &&
        std::get<JniInt>(fields.GetInstance(
            package_info, package_classes.package_info_class,
            version_code)) == 3;
    report.trace.emplace_back("framework.package");

    constexpr std::array<std::uint64_t, 2> worker_ids{2, 3};
    std::array<bool, worker_ids.size()> worker_results{};
    std::array<std::exception_ptr, worker_ids.size()> worker_errors{};
    std::array<std::unique_ptr<hal::HostThread>, worker_ids.size()>
        worker_threads;
    for (std::size_t index = 0; index < worker_ids.size(); ++index) {
        worker_threads[index] = hal::StartHostThread([&, index] {
            try {
                const auto thread_id = worker_ids[index];
                const auto worker = vm.AttachCurrentThreadAsDaemon(
                    thread_id, kJniVersion1_6, 8);
                if (worker.status != JniStatus::ok ||
                    worker.environment.IsNull() ||
                    vm.GetEnv(thread_id, kJniVersion1_6).environment !=
                        worker.environment) {
                    Fail(HeadlessJniContractErrorReason::invariant_failed,
                         "native worker could not attach to JavaVM");
                }
                const auto worker_locale =
                    ReferenceResult(invocations.InvokeStatic(
                        thread_id, locale_class,
                        Method(classes, locale_class, "getDefault",
                               "()Ljava/util/Locale;", true),
                        {}, JniArgumentSource::value_array));
                const auto language =
                    ReferenceResult(invocations.InvokeVirtual(
                        thread_id, worker_locale, locale_class,
                        Method(classes, locale_class, "getLanguage",
                               "()Ljava/lang/String;"),
                        {}, JniArgumentSource::value_array));
                const auto language_identity =
                    environment.ResolveObjectForHle(thread_id, language);
                worker_results[index] =
                    language_identity.has_value() &&
                    strings.Region(*language_identity, 0,
                                   strings.Length(*language_identity)) ==
                        std::vector<JniChar>{'z', 'h'} &&
                    vm.IsDaemon(thread_id);
                if (vm.DetachCurrentThread(thread_id) != JniStatus::ok) {
                    Fail(HeadlessJniContractErrorReason::invariant_failed,
                         "native worker could not detach from JavaVM");
                }
            } catch (...) {
                worker_errors[index] = std::current_exception();
            }
        });
    }
    for (auto& thread : worker_threads) thread->Join();
    for (const auto& error : worker_errors) {
        if (error) std::rethrow_exception(error);
    }
    report.native_threads_closed =
        worker_results[0] && worker_results[1] &&
        vm.AttachedThreadCount() == 1;
    report.trace.emplace_back("jni.native_threads");

    JniNativeRegistry natives;
    const std::array native_methods{
        JniNativeMethod{"nativeStep", "(I)I", kNativeStep}};
    natives.RegisterNatives(peer_class, native_methods);
    const auto target =
        natives.Resolve(peer_class, "nativeStep", "(I)I");
    if (!target.has_value()) {
        Fail(HeadlessJniContractErrorReason::unresolved_native,
             "headless JNI contract native method did not resolve");
    }
    const std::array native_arguments{JniValue{JniInt{40}}};
    const auto native_value = execute_native(*target, native_arguments);
    if (!std::holds_alternative<JniInt>(native_value)) {
        Fail(HeadlessJniContractErrorReason::native_return_mismatch,
             "headless JNI contract native result is not jint");
    }
    report.native_result = std::get<JniInt>(native_value);
    report.java_to_native = true;
    report.trace.emplace_back("java.nativeStep");

    InvokeLifecycle(classes, invocations, framework.activity_class, activity,
                    "onStart", "()V");
    InvokeLifecycle(classes, invocations, framework.activity_class, activity,
                    "onResume", "()V");
    report.trace.emplace_back("native.lifecycle.enter");

    const auto result_field =
        *classes.GetFieldId(peer_class, "result", "I", false);
    const auto runs_field =
        *classes.GetFieldId(peer_class, "runs", "I", true);
    fields.SetInstance(peer_object, peer_class, result_field,
                       report.native_result);
    fields.SetStatic(peer_class, runs_field, JniInt{1});

    constexpr std::array<JniChar, 4> text{0x004F, 0x0047, 0x0000, 0x4E2D};
    const auto string = strings.Create(text);
    const auto numbers =
        primitive_arrays.New(JniPrimitiveKind::integer, 2);
    const JniPrimitiveArrayData number_values =
        std::vector<JniInt>{40, report.native_result};
    primitive_arrays.SetRegion(numbers, 0, number_values);
    JniObjectArrayStore object_arrays(classes);
    const auto objects = object_arrays.New(framework.object_class, 2);
    object_arrays.Set(objects, 0,
                      JniObjectValue{activity_object,
                                     framework.activity_class});
    object_arrays.Set(objects, 1,
                      JniObjectValue{peer_object, peer_class});

    const auto stored_numbers =
        std::get<std::vector<JniInt>>(primitive_arrays.Region(numbers, 0, 2));
    report.data_round_trip =
        std::get<JniInt>(fields.GetInstance(peer_object, peer_class,
                                            result_field)) ==
            report.native_result &&
        std::get<JniInt>(fields.GetStatic(peer_class, runs_field)) == 1 &&
        strings.Length(string) == 4 && strings.ModifiedUtf8Length(string) == 7 &&
        stored_numbers == std::vector<JniInt>{40, report.native_result} &&
        object_arrays.Get(objects, 0)->object == activity_object &&
        object_arrays.Get(objects, 1)->object == peer_object;
    report.trace.emplace_back("jni.data");

    const auto global = environment.NewGlobalRef(kContractThread, peer);
    report.reference_closed =
        environment.IsSameObject(kContractThread, peer, global);
    environment.DeleteGlobalRef(kContractThread, global);
    report.trace.emplace_back("jni.reference");

    environment.Throw(kContractThread, throwable);
    const auto occurred = environment.ExceptionOccurred(kContractThread);
    const bool had_pending = environment.ExceptionCheck(kContractThread);
    environment.ExceptionClear(kContractThread);
    const bool same_throwable =
        environment.IsSameObject(kContractThread, throwable, occurred);
    environment.DeleteLocalRef(kContractThread, occurred);
    report.exception_closed = had_pending && same_throwable &&
                              !environment.ExceptionCheck(kContractThread);
    report.trace.emplace_back("jni.exception");

    InvokeLifecycle(classes, invocations, framework.activity_class, activity,
                    "onPause", "()V");
    InvokeLifecycle(classes, invocations, framework.activity_class, activity,
                    "onStop", "()V");
    InvokeLifecycle(classes, invocations, framework.activity_class, activity,
                    "onDestroy", "()V");
    report.native_to_java =
        lifecycle.State(activity) == FrameworkActivityState::destroyed;
    report.lifecycle_event_count = lifecycle.Events().size();
    report.trace.emplace_back("native.lifecycle.exit");

    fields.DeleteInstanceFields(peer_object);
    object_arrays.Delete(objects);
    primitive_arrays.Delete(numbers);
    strings.Delete(string);
    environment.DeleteLocalRef(kContractThread, throwable);
    environment.DeleteLocalRef(kContractThread, peer);
    environment.DeleteLocalRef(kContractThread, activity);
    if (natives.UnregisterNatives(peer_class) != 1 ||
        !report.data_round_trip || !report.reference_closed ||
        !report.exception_closed || !report.asset_round_trip ||
        !report.preferences_round_trip || !report.locale_round_trip ||
        !report.package_round_trip || !report.native_threads_closed) {
        Fail(HeadlessJniContractErrorReason::invariant_failed,
             "headless JNI contract did not close every resource");
    }
    if (vm.DetachCurrentThread(kContractThread) != JniStatus::ok) {
        Fail(HeadlessJniContractErrorReason::invariant_failed,
             "headless JNI contract could not detach JavaVM thread");
    }
    primitive_arrays.Delete(service_bytes);
    for (const auto identity : service_strings) strings.Delete(identity);
    detach.Disarm();
    report.trace.emplace_back("vm.detach");
    return report;
}

}  // namespace ogplay::runtime

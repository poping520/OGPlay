#include "ogplay/runtime/headless_jni_contract.h"

#include <array>
#include <optional>
#include <utility>
#include <vector>

#include "ogplay/runtime/framework_lifecycle.h"
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
                                 const char* name, const char* descriptor) {
    const auto method =
        classes.GetMethodId(java_class, name, descriptor, false);
    if (!method.has_value()) {
        Fail(HeadlessJniContractErrorReason::invariant_failed,
             "headless JNI contract method is missing");
    }
    return *method;
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
        vm.AttachCurrentThread(kContractThread, kJniVersion1_6);
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

    JniFieldStore fields(classes);
    const auto result_field =
        *classes.GetFieldId(peer_class, "result", "I", false);
    const auto runs_field =
        *classes.GetFieldId(peer_class, "runs", "I", true);
    fields.SetInstance(peer_object, peer_class, result_field,
                       report.native_result);
    fields.SetStatic(peer_class, runs_field, JniInt{1});

    JniStringStore strings;
    constexpr std::array<JniChar, 4> text{0x004F, 0x0047, 0x0000, 0x4E2D};
    const auto string = strings.Create(text);
    JniPrimitiveArrayStore primitive_arrays;
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
        !report.exception_closed) {
        Fail(HeadlessJniContractErrorReason::invariant_failed,
             "headless JNI contract did not close every resource");
    }
    if (vm.DetachCurrentThread(kContractThread) != JniStatus::ok) {
        Fail(HeadlessJniContractErrorReason::invariant_failed,
             "headless JNI contract could not detach JavaVM thread");
    }
    detach.Disarm();
    report.trace.emplace_back("vm.detach");
    return report;
}

}  // namespace ogplay::runtime

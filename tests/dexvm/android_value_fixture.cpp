#include "android_value_fixture.h"

#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "runtime/integration/dexvm_android/shared.h"
#include "../fixtures/bitmap_decode_data.h"

namespace ogplay::test::android_value {

std::optional<std::string> DispatchFixtureMessages(Interpreter& vm, DexVmAndroidContext& context) {
    namespace intrinsics = runtime::android_intrinsics;
    const VmExecutionLockScope execution(vm.ExecutionLock());
    if (context.scheduler_shutdown) return std::nullopt;
    const auto main = intrinsics::EnsureMainLooper(vm, {&context, [](DexVmAndroidContext*) {}});
    DispatchAndroidSurfaceStreamEvents(vm, context);
    try {
        intrinsics::PumpNativeLooperEvents(vm, context, main);
    } catch (const std::exception& error) {
        return error.what();
    }
    const auto slot = [&](VmObjectRef object, const char* name, const char* type) {
        const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, type);
        REQUIRE(field);
        return vm.Linker().Field(*field).slot;
    };
    const auto bits = [&](VmObjectRef object, const char* name, const char* type) {
        return vm.Model().InstanceSlots(object)[slot(object, name, type)].bits;
    };
    const auto invoke = [&](VmObjectRef object, const char* name, const char* type,
                            std::vector<VmValue> arguments = {}) {
        const auto owner = vm.Model().ObjectClass(object);
        const auto index = vm.Linker().FindVtableIndex(owner, name, type);
        REQUIRE(index);
        arguments.insert(arguments.begin(), VmValue::Ref(object));
        return vm.Call(vm.Linker().Class(owner).vtable[*index], arguments);
    };
    const auto failure = [&](const VmCallOutcome& result, std::string_view operation) {
        return std::string(operation) + " raised: " + vm.Linker().Class(result.exception_class).descriptor + ": " +
               result.exception_message + vm.DescribeThrowableCauses(result.exception);
    };
    const auto queue = VmObjectRef{bits(main, "mQueue", "Landroid/os/MessageQueue;")};
    for (std::size_t count = 0; count < 10'000; ++count) {
        auto head = VmObjectRef{bits(queue, "mMessages", "Landroid/os/Message;")};
        if (head.IsValid() && bits(head, "target", "Landroid/os/Handler;") == 0) {
            do {
                head = VmObjectRef{bits(head, "next", "Landroid/os/Message;")};
            } while (head.IsValid() && !(bits(head, "flags", "I") & 2U));
        }
        if (!head.IsValid()) break;
        const auto when_slot = slot(head, "when", "J");
        const auto fields = vm.Model().InstanceSlots(head);
        const auto when = static_cast<std::int64_t>(fields[when_slot].bits |
                                                    (static_cast<std::uint64_t>(fields[when_slot + 1].bits) << 32));
        if (when > context.uptime_millis.load()) break;
        const auto take = invoke(queue, "next", "()Landroid/os/Message;");
        if (take.exception.IsValid()) return failure(take, "MessageQueue.next");
        const auto message = take.value.ref;
        const auto root = vm.ProtectReferences(std::array{message});
        const auto target = VmObjectRef{bits(message, "target", "Landroid/os/Handler;")};
        const auto result = invoke(target, "dispatchMessage", "(Landroid/os/Message;)V", {VmValue::Ref(message)});
        if (result.exception.IsValid()) return failure(result, "scheduled callback");
        const auto recycle = invoke(message, "recycle", "()V");
        if (recycle.exception.IsValid()) return failure(recycle, "Message.recycle");
    }
    return context.threads == nullptr ? std::nullopt : context.threads->TakeFailure();
}

AndroidValueVm::AndroidValueVm(InterpreterBackend backend, const std::vector<IntrinsicClassDecl>& extras,
                               bool force_all_bridge, const char* dex_fixture, ogplay::gles::AngleBackend graphics)
    : context(std::make_shared<DexVmAndroidContext>()), runtime([&] {
          context->surface_width = 800;
          context->surface_height = 480;
          ogplay::test::BindBootDexArchive(*context);
          auto catalog = AndroidIntrinsicCatalog(context);
          catalog.insert(catalog.end(), extras.begin(), extras.end());
          InterpreterConfig config{
              .backend = backend,
              .force_all_bridge = force_all_bridge,
              .diagnostics = {.trace_capacity = 8192U, .event_mask = DexVmTraceBit(DexVmTraceKind::method_enter)}};
          return std::make_unique<ogplay::test::container_traversal::AndroidTestRuntime>(
              context, ledger, logger, std::move(catalog),
              ogplay::test::native_library::ReadDexFixture(dex_fixture ? dex_fixture : "application.dex"), config,
              nullptr, std::vector<ogplay::loader::ApkNativeLibrary>{}, graphics);
      }()),
      model(runtime->bridge->Model()), linker(runtime->bridge->Linker()), vm(runtime->bridge->Vm()),
      threads(runtime->bridge->Threads()) {}

VmObjectRef AndroidValueVm::New(const char* descriptor, const char* constructor, std::vector<VmValue> arguments) {
    ogplay::test::MountResourceFixture(*context, vm);
    if (!arguments.empty() && std::string_view(constructor).starts_with("(Landroid/content/Context;"))
        ogplay::test::AttachViewTestContext(vm, arguments.front().ref, context);
    if (std::string_view(descriptor) == "Landroid/content/Context;")
        return ogplay::test::MakeRomAppContext(vm, context);
    const auto klass = linker.ResolveDescriptor(descriptor);
    const auto initialized = vm.EnsureClassInitialized(klass);
    REQUIRE_MESSAGE(!initialized.exception.IsValid(), initialized.exception_message);
    const auto object = vm.NewIntrinsicInstance(descriptor);
    const auto roots = vm.ProtectReferences(std::array{object});
    if (std::string_view(constructor).starts_with("(Landroid/content/Context;") && !arguments.empty() &&
        linker.IsAssignable(linker.ResolveDescriptor("Landroid/view/View;"), klass)) {
        ogplay::test::AttachViewTestContext(vm, arguments.front().ref, context);
    }
    const auto method = linker.FindDirectMethod(klass, "<init>", constructor);
    REQUIRE(method.has_value());
    arguments.insert(arguments.begin(), VmValue::Ref(object));
    const auto outcome = vm.Call(*method, arguments);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return object;
}

VmValue AndroidValueVm::Static(const char* descriptor, const char* name, const char* signature,
                               std::vector<VmValue> arguments) {
    const auto outcome = StaticOutcome(descriptor, name, signature, std::move(arguments));
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return outcome.value;
}

VmCallOutcome AndroidValueVm::StaticOutcome(const char* descriptor, const char* name, const char* signature,
                                            std::vector<VmValue> arguments) {
    ogplay::test::MountResourceFixture(*context, vm);
    const auto klass = linker.ResolveDescriptor(descriptor);
    const auto method = linker.FindDirectMethod(klass, name, signature);
    REQUIRE(method.has_value());
    return vm.Call(*method, arguments);
}

VmValue AndroidValueVm::On(const VmObjectRef receiver, const char* name, const char* signature,
                           std::vector<VmValue> arguments) {
    const auto outcome = OnOutcome(receiver, name, signature, std::move(arguments));
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return outcome.value;
}

VmCallOutcome AndroidValueVm::OnOutcome(const VmObjectRef receiver, const char* name, const char* signature,
                                        std::vector<VmValue> arguments) {
    ogplay::test::MountResourceFixture(*context, vm);
    const auto klass = model.ObjectClass(receiver);
    const auto index = linker.FindVtableIndex(klass, name, signature);
    REQUIRE(index.has_value());
    arguments.insert(arguments.begin(), VmValue::Ref(receiver));
    return vm.Call(linker.Class(klass).vtable[*index], arguments);
}

VmObjectRef AndroidValueVm::Bytes(const std::string& value) {
    const auto array = model.NewPrimitiveArray(linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                               static_cast<JniSize>(value.size()));
    std::vector<std::byte> bytes(value.size());
    for (std::size_t index = 0; index < value.size(); ++index)
        bytes[index] = static_cast<std::byte>(value[index]);
    model.WriteByteRegion(array, 0, bytes);
    return array;
}

std::string AndroidValueVm::BytesOf(const VmObjectRef array) {
    const auto bytes = model.ReadByteRegion(array, 0, model.ArrayLength(array));
    std::string result(bytes.size(), '\0');
    for (std::size_t index = 0; index < bytes.size(); ++index)
        result[index] = static_cast<char>(bytes[index]);
    return result;
}

}  // namespace ogplay::test::android_value

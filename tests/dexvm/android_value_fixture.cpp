#include "android_value_fixture.h"

#include "ogplay/runtime/integration/bitmap_pixels.h"
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
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/integration/host_image_decode.h"
#include "../fixtures/bitmap_decode_data.h"

namespace ogplay::test::android_value {

AndroidValueVm::AndroidValueVm(InterpreterBackend backend, const std::vector<IntrinsicClassDecl>& extras,
                               bool force_all_bridge, const char* dex_fixture, bool include_skeleton,
                               AndroidUiTraversalOwner owner, const char* test_bootdex)
    : context(std::make_shared<DexVmAndroidContext>(owner)),
      vm(
          [this, &extras, dex_fixture, include_skeleton, owner, test_bootdex]() -> DexClassLinker& {
              if ((owner == AndroidUiTraversalOwner::JavaTraversal) != (test_bootdex != nullptr))
                  throw std::invalid_argument("test View owner/payload must be paired before VM creation");
              linker.RegisterIntrinsics(CoreIntrinsicCatalog(
                  AndroidCoreIntrinsicServices(context)));
              linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
              if (test_bootdex) {
                  ogplay::test::BindBootDexPlatformNatives(linker);
                  std::ifstream stream(test_bootdex, std::ios::binary);
                  REQUIRE(stream.good());
                  const std::vector<char> raw{std::istreambuf_iterator<char>(stream), {}};
                  const std::span<const std::byte> archive{reinterpret_cast<const std::byte*>(raw.data()), raw.size()};
                  context->boot_classpath_bytes.assign(archive.begin(), archive.end());
                  context->boot_classpath_archive = ogplay::loader::ParseApkArchive(archive);
                  const auto dex =
                      ogplay::loader::ReadApkEntry(archive, ogplay::loader::ParseApkArchive(archive), "classes.dex");
                  std::vector<std::uint8_t> bytes(dex.size());
                  std::memcpy(bytes.data(), dex.data(), dex.size());
                  linker.RegisterBootDex(std::move(bytes), DexClassLinker::BootDexMemberSources(archive));
              } else {
                  ogplay::test::RegisterBootDex(linker);
              }
              if (include_skeleton) ogplay::test::RegisterBootDexSkeleton(linker);
              linker.RegisterIntrinsics(extras);
              if (dex_fixture != nullptr) {
                  const auto path = std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + dex_fixture;
                  std::ifstream input(path, std::ios::binary);
                  REQUIRE_MESSAGE(input.good(), "missing fixture: ", path);
                  linker.RegisterDex(std::vector<std::uint8_t>(
                      std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()));
              }
              linker.Link();
              return linker;
          }(),
          model, nullptr, ledger,
          {.backend = backend,
           .force_all_bridge = force_all_bridge,
           .diagnostics = {.trace_capacity = owner == AndroidUiTraversalOwner::JavaTraversal ? 8192U : 0U,
                           .event_mask = DexVmTraceBit(DexVmTraceKind::method_enter)}}),
      threads(vm) {
    context->threads = &threads;
    vm.SetLogger(&logger);
    RegisterAndroidValueStateTables(vm, context);
}

VmObjectRef AndroidValueVm::New(const char* descriptor,
                const char* constructor,
                std::vector<VmValue> arguments) {
    const auto klass = linker.ResolveDescriptor(descriptor);
    const auto initialized = vm.EnsureClassInitialized(klass);
    REQUIRE_MESSAGE(!initialized.exception.IsValid(), initialized.exception_message);
    const auto object = vm.NewIntrinsicInstance(descriptor);
    const auto roots = vm.ProtectReferences(std::array{object});
    if (std::string_view(constructor).starts_with("(Landroid/content/Context;") && !arguments.empty() &&
        linker.IsAssignable(linker.ResolveDescriptor("Landroid/view/View;"), klass)) {
        ogplay::test::AttachViewTestContext(vm, arguments.front().ref);
    }
    const auto method = linker.FindDirectMethod(klass, "<init>", constructor);
    REQUIRE(method.has_value());
    arguments.insert(arguments.begin(), VmValue::Ref(object));
    const auto outcome = vm.Call(*method, arguments);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return object;
}

VmValue AndroidValueVm::Static(const char* descriptor, const char* name,
               const char* signature,
               std::vector<VmValue> arguments) {
    const auto outcome = StaticOutcome(
        descriptor, name, signature, std::move(arguments));
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return outcome.value;
}

VmCallOutcome AndroidValueVm::StaticOutcome(const char* descriptor, const char* name,
                            const char* signature,
                            std::vector<VmValue> arguments) {
    const auto klass = linker.ResolveDescriptor(descriptor);
    const auto method = linker.FindDirectMethod(klass, name, signature);
    REQUIRE(method.has_value());
    return vm.Call(*method, arguments);
}

VmValue AndroidValueVm::On(const VmObjectRef receiver, const char* name,
           const char* signature,
           std::vector<VmValue> arguments) {
    const auto outcome = OnOutcome(receiver, name, signature,
                                   std::move(arguments));
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return outcome.value;
}

VmCallOutcome AndroidValueVm::OnOutcome(const VmObjectRef receiver, const char* name,
                        const char* signature,
                        std::vector<VmValue> arguments) {
    const auto klass = model.ObjectClass(receiver);
    const auto index = linker.FindVtableIndex(klass, name, signature);
    REQUIRE(index.has_value());
    arguments.insert(arguments.begin(), VmValue::Ref(receiver));
    return vm.Call(linker.Class(klass).vtable[*index], arguments);
}

VmObjectRef AndroidValueVm::Bytes(const std::string& value) {
    const auto array = model.NewPrimitiveArray(
        linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
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

} // namespace ogplay::test::android_value

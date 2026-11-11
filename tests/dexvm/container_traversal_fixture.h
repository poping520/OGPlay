#pragma once
#include "../runtime/native_library_fixture.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include <cstring>
#include "boot_dex.h"
#include <doctest/doctest.h>
#include <array>
#include <fstream>
#include <string>
#include <vector>

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

namespace ogplay::test::container_traversal {
struct AndroidTestRuntime;

struct ContainerVm {
    VirtualFileSystem filesystem;
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::shared_ptr<DexVmAndroidContext> context{std::make_shared<DexVmAndroidContext>()};
    std::unique_ptr<AndroidTestRuntime> runtime;
    Interpreter& vm;
    DexClassLinker& linker;
    JavaObjectModel& model;

    std::unique_ptr<AndroidTestRuntime> Create(InterpreterBackend backend, bool native_activity, bool sealed_apk);
    explicit ContainerVm(InterpreterBackend backend);
    ContainerVm(InterpreterBackend backend, bool native_activity, bool sealed_apk = false);
    VmValue Checked(const VmCallOutcome& result);
    VmCallOutcome OnOutcome(VmObjectRef object, const char* name, const char* descriptor,
                            std::vector<VmValue> args = {});
    VmValue On(VmObjectRef object, const char* name, const char* descriptor, std::vector<VmValue> args = {});
    VmValue Static(const char* owner, const char* name, const char* descriptor, std::vector<VmValue> args = {});
    VmObjectRef New(const char* owner, const char* descriptor = "()V", std::vector<VmValue> args = {});
};
}  // namespace ogplay::test::container_traversal

// Native-backed VM for the Android fixture family. Uses the pinned JNI payload,
// including ROM constructors' Rect/Pattern initialization, without opening a window.
namespace ogplay::test::container_traversal {
struct AndroidTestRuntime final {
    VirtualFileSystem owned_filesystem;
    ogplay::loader::ApkNativeLibraryInventory inventory;
    ogplay::loader::ApkSelectedNativeLibraries selected;
    std::unique_ptr<AndroidGuestCallSession> session;
    std::unique_ptr<NativeLibraryLoader> libraries;
    std::unique_ptr<DexVmGuestBridge> bridge;

    AndroidTestRuntime(const std::shared_ptr<DexVmAndroidContext>& context, ogplay::core::CapabilityLedger& ledger,
                       ogplay::core::Logger& logger, std::vector<IntrinsicClassDecl> catalog,
                       std::vector<std::uint8_t> dex, InterpreterConfig config = {},
                       VirtualFileSystem* filesystem = nullptr,
                       std::vector<ogplay::loader::ApkNativeLibrary> apk_libraries = {},
                       ogplay::gles::AngleBackend graphics = {});
    ~AndroidTestRuntime();
};
}  // namespace ogplay::test::container_traversal

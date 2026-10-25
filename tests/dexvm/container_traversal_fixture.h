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
struct ContainerVm {
    VirtualFileSystem filesystem;
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::shared_ptr<DexVmAndroidContext> context{
        std::make_shared<DexVmAndroidContext>(AndroidUiTraversalOwner::JavaTraversal)};
    std::unique_ptr<ogplay::session::AndroidAppProcess> app;
    Interpreter& vm;
    DexClassLinker& linker;
    JavaObjectModel& model;

    std::unique_ptr<ogplay::session::AndroidAppProcess> Create(InterpreterBackend backend);
    std::unique_ptr<ogplay::session::AndroidAppProcess> Create(InterpreterBackend backend, bool include_skeleton);
    explicit ContainerVm(InterpreterBackend backend);
    ContainerVm(InterpreterBackend backend, bool include_skeleton);
    VmValue Checked(const VmCallOutcome& result);
    VmCallOutcome OnOutcome(VmObjectRef object, const char* name, const char* descriptor,
                            std::vector<VmValue> args = {});
    VmValue On(VmObjectRef object, const char* name, const char* descriptor, std::vector<VmValue> args = {});
    VmValue Static(const char* owner, const char* name, const char* descriptor, std::vector<VmValue> args = {});
    VmObjectRef New(const char* owner, const char* descriptor = "()V", std::vector<VmValue> args = {});
};
}  // namespace ogplay::test::container_traversal

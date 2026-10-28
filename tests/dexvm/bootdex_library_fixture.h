#pragma once
#include "ogplay/runtime/jni/host_copy_payload_provider.h"

#include "boot_dex.h"

#include <cstdint>
#include <algorithm>
#include <memory>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "tls_fixtures.h"

namespace ogplay::test::bootdex_library {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

std::vector<std::uint8_t> Dvm102BootDex();

struct Dvm87Vm final {
    ogplay::runtime::HostCopyJavaPayloadProvider strings_payload;
    JniStringStore strings{strings_payload};
    ogplay::runtime::HostCopyJavaPayloadProvider arrays_payload;
    JniPrimitiveArrayStore arrays{arrays_payload};
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    std::shared_ptr<DexVmAndroidContext> context{std::make_shared<DexVmAndroidContext>()};
    Interpreter vm;
    VmThreadRuntime threads;
    VirtualFileSystem vfs;
    DexVmIoVfsAdapter io_file_system{vfs};

    explicit Dvm87Vm(const InterpreterBackend backend = InterpreterBackend::switch_dispatch,
                     const std::string& language = "zh", const std::string& region = "CN",
                     const std::string& default_timezone = "GMT", const std::vector<IntrinsicClassDecl>& extras = {});

    ~Dvm87Vm();

    [[nodiscard]] VmCallOutcome Static(const std::string_view owner, const std::string_view name,
                                       const std::string_view descriptor, std::vector<VmValue> arguments = {});

    [[nodiscard]] VmCallOutcome Virtual(const VmObjectRef receiver, const std::string_view name,
                                        const std::string_view descriptor, std::vector<VmValue> arguments = {});

    void Construct(const VmObjectRef object, const std::string_view owner, const std::string_view descriptor,
                   std::vector<VmValue> arguments = {});

    static void RequireOk(const VmCallOutcome& outcome);
};

}  // namespace ogplay::test::bootdex_library

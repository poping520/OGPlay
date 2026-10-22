#pragma once
#include "ogplay/runtime/jni/host_copy_payload_provider.h"

#include <memory>
#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"

namespace ogplay::test::android_value {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;


struct AndroidValueVm final {
    JniStringStore strings;
    ogplay::runtime::HostCopyJavaPayloadProvider arrays_payload;
    JniPrimitiveArrayStore arrays{arrays_payload};
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::shared_ptr<DexVmAndroidContext> context{
        std::make_shared<DexVmAndroidContext>()};
    Interpreter vm;
    VmThreadRuntime threads;

    AndroidValueVm(InterpreterBackend backend = InterpreterBackend::switch_dispatch,
                   const std::vector<IntrinsicClassDecl>& extras = {},
                   bool force_all_bridge = false,
                   const char* dex_fixture = nullptr,
                   bool include_skeleton = false);

    VmObjectRef New(const char* descriptor,
                    const char* constructor = "()V",
                    std::vector<VmValue> arguments = {});

    VmValue Static(const char* descriptor, const char* name,
                   const char* signature,
                   std::vector<VmValue> arguments = {});

    VmCallOutcome StaticOutcome(const char* descriptor, const char* name,
                                const char* signature,
                                std::vector<VmValue> arguments = {});

    VmValue On(const VmObjectRef receiver, const char* name,
               const char* signature,
               std::vector<VmValue> arguments = {});

    VmCallOutcome OnOutcome(const VmObjectRef receiver, const char* name,
                            const char* signature,
                            std::vector<VmValue> arguments = {});

    VmObjectRef Bytes(const std::string& value);

    std::string BytesOf(const VmObjectRef array);
};

} // namespace ogplay::test::android_value

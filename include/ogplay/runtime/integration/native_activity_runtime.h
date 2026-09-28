#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/jni/jni.h"

namespace ogplay::core { class Logger; }
namespace ogplay::runtime {
class AndroidGuestCallSession;
struct DexVmAndroidContext;

// Owns NDK activity allocations and JNI global references inside an existing
// process. Does not own a VM, CPU, address space, library namespace or window.
class NativeActivityRuntime final {
public:
    using Publish = std::function<JniReference(dexvm::VmObjectRef, std::uint64_t)>;
    using Resolve = std::function<dexvm::VmObjectRef(JniReference, std::uint64_t)>;
    using Thread = std::function<std::uint64_t(std::uint64_t)>;
    NativeActivityRuntime(AndroidGuestCallSession&, std::weak_ptr<DexVmAndroidContext>,
                          Publish, Resolve, Thread, core::Logger* logger = nullptr);
    ~NativeActivityRuntime();
    dexvm::VmValue Call(dexvm::IntrinsicContext&, std::string_view method);
    // Call after Java workers join and before the session detaches JNI.
    // Idempotent; reports cleanup failures to the caller.
    void Release();
    memory::GuestAddress InputQueuePointer(dexvm::VmObjectRef owner) const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ogplay::runtime

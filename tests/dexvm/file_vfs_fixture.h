#pragma once
#include "ogplay/runtime/jni/host_copy_payload_provider.h"

#include "boot_dex.h"
// java.io.File through the shared VFS (SBX-5, ADR-0020). Two things matter
// here: a Java save and a native read see one filesystem, and File.mkdirs
// reports the truth. The old handler returned true unconditionally without
// creating anything, so the mkdirs case below fails if that ever returns.


#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/audio/encoded_music.h"
#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/audio/pcm_mix.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::test::file_vfs {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

inline constexpr const char* kPackage = "com.example.game";
extern const std::vector<std::string> kWritableRoots;
void Append16(std::vector<std::byte>& bytes, const std::uint16_t value);

void Append32(std::vector<std::byte>& bytes, const std::uint32_t value);

std::vector<std::byte> MakeStoredZip(const std::string_view name,
                                     const std::span<const std::byte> payload);

struct TemporaryRoot final {
    std::filesystem::path path;

    explicit TemporaryRoot(const std::string& name);
    ~TemporaryRoot();
    TemporaryRoot(const TemporaryRoot&) = delete;
    TemporaryRoot& operator=(const TemporaryRoot&) = delete;
};

struct FileVm final {
    ogplay::runtime::HostCopyJavaPayloadProvider strings_payload;
    JniStringStore strings{strings_payload};
    ogplay::runtime::HostCopyJavaPayloadProvider arrays_payload;
    JniPrimitiveArrayStore arrays{arrays_payload};
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::shared_ptr<DexVmAndroidContext> context;
    VirtualFileSystem vfs;
    DexVmIoVfsAdapter io_file_system;
    Interpreter interpreter;

    explicit FileVm(SandboxStore* sandbox = nullptr,
                    const bool include_android_catalog = true,
                    const InterpreterConfig config = {},
                    const std::span<const IntrinsicClassDecl>
                        extra_intrinsics = {},
                    const bool include_boot_dex = true);

    [[nodiscard]] VmObjectRef NewFile(const std::string& path);

    [[nodiscard]] VmObjectRef NewUri(const std::string& spec);

    [[nodiscard]] VmObjectRef NewFile(const VmObjectRef parent,
                                      const std::string& name);

    [[nodiscard]] VmObjectRef NewFileFromUri(const VmObjectRef uri);

    [[nodiscard]] std::uint64_t StaticField(
        const std::string_view owner, const std::string_view name,
        const std::string_view descriptor);

    VmCallOutcome CallOnOutcome(const VmObjectRef receiver,
                                const std::string& name,
                                const std::string& descriptor,
                                std::vector<VmValue> arguments = {});

    VmValue CallOn(const VmObjectRef receiver, const std::string& name,
                   const std::string& descriptor,
                   std::vector<VmValue> arguments = {});

    VmValue CallStatic(const std::string& owner, const std::string& name,
                       const std::string& descriptor);

    [[nodiscard]] bool BoolOn(const VmObjectRef receiver,
                              const std::string& name);

    // Writes through java.io.FileOutputStream, the channel these games use.
    void JavaWrite(const std::string& path, const std::string& text);

    // Reads the same path the way native code would: straight off the VFS.
    [[nodiscard]] std::string NativeRead(const std::string& path);
};


} // namespace ogplay::test::file_vfs

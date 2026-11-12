#pragma once

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/integration/dexvm_android.h"

// Private helpers shared by the android.* per-class declaration units. This
// is not part of the module's public surface.

namespace ogplay::runtime::android_intrinsics {

namespace dx = dexvm;
[[nodiscard]] std::optional<std::array<float, 9>> AndroidMatrixSnapshot(DexVmAndroidContext& context, int token);
void RetireAndroidNinePatch(dx::Interpreter& vm, DexVmAndroidContext& context, int bits, dx::VmObjectRef owner);
void RegisterGraphicsStateTables(dx::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context);
inline constexpr std::int32_t kGuestProcessId = 1;

using Decl = dexvm::IntrinsicClassDecl;
using Context = std::shared_ptr<DexVmAndroidContext>;
dx::VmObjectRef CreateApplicationResources(dx::IntrinsicContext& call, const Context& context);
void InitializeAndroidSystemProperties(dx::Interpreter& vm, const Context& context);
void EnsureLocalConnectivityBroadcast(dx::IntrinsicContext& call, const Context& context);

// Normal guest dispatch; preserve Java exception identity across the boundary.
void RejectUnsupportedServiceMethod(dx::IntrinsicClassBuilder& builder, std::string_view owner, const char* name,
                                    const char* descriptor);
// The caller is always the current APK. Platform-only permissions are denied
// by the declaration even if the APK requested them.
void RequireServicePermission(const Context& context, const char* permission);
void DenyServiceMethod(dx::IntrinsicClassBuilder& builder, const char* name, const char* descriptor,
                       const char* permission);
[[nodiscard]] dx::VmObjectRef NewServiceArray(dx::Interpreter& vm, const char* descriptor, JniSize size = 0);
[[nodiscard]] dx::VmObjectRef NewPlatformObject(dx::Interpreter& vm, const char* type, const char* signature,
                                                std::vector<dx::VmValue> arguments);
[[nodiscard]] dx::VmObjectRef GetLocalServiceInterface(dx::Interpreter& vm, const char* name, const char* stub,
                                                       const char* interface_type);
[[nodiscard]] dx::VmValue CallAndroidMethod(dx::Interpreter& vm, dx::VmObjectRef receiver, const char* name,
                                            const char* signature, std::vector<dx::VmValue> arguments = {});
[[nodiscard]] dx::VmObjectRef NewAndroidComponentName(dx::Interpreter& vm, dx::VmObjectRef package,
                                                      dx::VmObjectRef class_name);
[[nodiscard]] dx::VmObjectRef NewAndroidBundle(dx::Interpreter& vm, dx::VmObjectRef source = dx::VmObjectRef{});
// The managed ANGLE window path guarantees ES 2.0.
inline constexpr std::int32_t kManagedGlesVersion = 2 << 16;
[[nodiscard]] std::int32_t DeviceDensityDpi(const Context& context);
void PopulateAndroidDisplayMetrics(dx::IntrinsicContext& call, const Context& context, dx::VmObjectRef metrics);

// Helpers shared across handler batches; batch-local helpers stay private to
// their translation unit.
[[nodiscard]] dx::VmValue Self(dx::IntrinsicContext& call);
// Per-context instance cache: platform singletons (window, locale, service
// managers) must answer the same object every call.
[[nodiscard]] dx::VmObjectRef ResourceObject(dx::IntrinsicContext& call, const Context& context, bool system);
[[nodiscard]] dx::VmValue MakeString(dx::IntrinsicContext& call, const std::string& value);
// Materialize the current package's ApplicationInfo from sealed process
// facts. Context keeps one object for the LoadedApk lifetime; bounded
// PackageManager queries may request fresh copies with optional metadata.
enum class PackageInfoKind : std::uint8_t { installed, archive };
[[nodiscard]] dx::VmObjectRef MakeApplicationInfo(dx::IntrinsicContext& call, const Context& context,
                                                  bool include_meta_data,
                                                  PackageInfoKind kind = PackageInfoKind::installed);
void PutAndroidMetaData(dx::IntrinsicContext& call, const Context& context, dx::VmObjectRef bundle,
                        const std::string& name, const loader::AndroidManifestMetaDataValue& value);
[[nodiscard]] dx::IntrinsicHandler NeutralHandler(char shorty);
void GuestLog(dx::IntrinsicContext& call, core::LogLevel level, const std::string& line);
// A display-only UI request was ignored instead of failing the game: warns once
// per id and counts the rest in the ledger. Not for anything guest logic reads back.
void RecordUiDegraded(dx::Interpreter& vm, std::string_view what);
// Missing or damaged entries throw the Java IOException the glue catches.
void RetireAndroidTypeface(DexVmAndroidContext& context, int bits);
std::vector<std::byte> ReadAndroidApkAsset(dx::Interpreter& vm, const Context& context, dx::VmObjectRef manager,
                                           const std::string& name);
[[nodiscard]] dx::VmObjectRef EnsureMainLooper(dx::IntrinsicContext& call, const Context& context);
[[nodiscard]] dx::VmObjectRef EnsureMainLooper(dx::Interpreter& vm, const Context& context);
[[nodiscard]] dx::VmObjectRef CurrentLooper(const Context& context, std::uint64_t context_token);
[[nodiscard]] dx::VmObjectRef RegisterCurrentRomLooper(dx::Interpreter& vm, const Context& context);
void PumpNativeLooperEvents(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef looper);
// Resolve attachment from Java parents and a live managed window generation.

struct DisabledWebTarget final {
    bool javascript{};
    bool http_or_https{};
    std::string url;
    std::string host;
    std::string path;
};

[[nodiscard]] DisabledWebTarget ParseDisabledWebTarget(std::string_view url);
void LogDisabledWeb(dx::Interpreter& vm, std::string_view action, const DisabledWebTarget& target = {});
// Video view state shared by the VideoView intrinsics and the guest video
// pump (android_media.cpp): lookup, playback-position math, and the
// onCompletion listener invocation (fresh MediaPlayer argument, matching
// device behaviour; returns a rendered message on guest exceptions).
[[nodiscard]] DexVmAndroidContext::VideoViewState* VideoStateOf(const Context& context, std::uint64_t handle);
[[nodiscard]] std::int64_t VideoPositionOf(const DexVmAndroidContext::VideoViewState& state, std::int64_t uptime_ms);
[[nodiscard]] std::optional<std::string> InvokeVideoEvent(dx::Interpreter& vm, DexVmAndroidContext& context,
                                                          std::uint64_t handle, std::int32_t generation,
                                                          std::int32_t event);

[[nodiscard]] dx::IntrinsicHandler WidgetNoopHandler();
[[nodiscard]] dx::IntrinsicHandler PlatformSystemCurrentTimeMillisHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformRuntimeNativeExitHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformSystemLoadHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformSystemLoadLibraryHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformSystemNanoTimeHandler(const Context& context);

}  // namespace ogplay::runtime::android_intrinsics

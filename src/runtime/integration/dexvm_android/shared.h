#pragma once

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/integration/dexvm_android.h"

// Private helpers shared by the android.* per-class declaration units. This
// is not part of the module's public surface.

namespace ogplay::runtime::android_intrinsics {
void ProjectNinePatchGeometry(dexvm::Interpreter&, DexVmAndroidContext&, dexvm::VmObjectRef, ui::UiBitmap&);

namespace dx = dexvm;
[[nodiscard]] dx::VmObjectRef CreateAndroidBitmap(dx::Interpreter& vm, DexVmAndroidContext& context, int width,
                                                  int height, std::span<const std::uint32_t> argb,
                                                  bool mutable_pixels = true, int density = -1);
[[nodiscard]] std::optional<std::array<float, 9>> AndroidMatrixSnapshot(DexVmAndroidContext& context, int token);
[[nodiscard]] bool AndroidMatrixIsIdentity(dx::Interpreter& vm, const DexVmAndroidContext& context,
                                           dx::VmObjectRef matrix);

[[nodiscard]] std::u16string ReadAndroidTextDisplay(dx::Interpreter&, DexVmAndroidContext&, dx::VmObjectRef);
void InitializeCanvasPaint(dx::IntrinsicContext& call, DexVmAndroidContext& context);
void RetireCanvasPaint(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef paint);
void AddCanvasPaintMethods(dx::IntrinsicClassBuilder& builder, const std::shared_ptr<DexVmAndroidContext>& context);

struct AndroidPaintProjection {
    float size;
    int style;
    int flags;
};

AndroidPaintProjection ReadAndroidPaintProjection(dx::Interpreter& vm, DexVmAndroidContext& context,
                                                  dx::VmObjectRef owner);
void AddManagedCanvasNatives(dx::IntrinsicClassBuilder& builder, const std::shared_ptr<DexVmAndroidContext>& context);
void ResetManagedCanvas(DexVmAndroidContext& context, std::uint32_t holder);
void RetireAndroidNinePatch(dx::Interpreter& vm, DexVmAndroidContext& context, int bits, dx::VmObjectRef owner);
void RegisterGraphicsStateTables(dx::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context);
inline constexpr std::int32_t kGuestProcessId = 1;
[[nodiscard]] dx::VmObjectRef ManagedViewTreeObserver(dx::Interpreter& vm, DexVmAndroidContext& context,
                                                      dx::VmObjectRef view);
[[nodiscard]] dx::VmObjectRef ManagedWindowToken(dx::Interpreter&, DexVmAndroidContext&, dx::VmObjectRef);
void MergeManagedViewTreeObservers(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef subtree);

[[nodiscard]] bool AndroidAlphaOnly(dx::Interpreter&, DexVmAndroidContext&, dx::VmObjectRef);
void RequireAndroidAlphaOnly(dx::Interpreter&, DexVmAndroidContext&, dx::VmObjectRef);

using Decl = dexvm::IntrinsicClassDecl;
using Context = std::shared_ptr<DexVmAndroidContext>;
dx::VmObjectRef CreateApplicationResources(dx::IntrinsicContext& call, const Context& context);
void InitializeAndroidSystemProperties(dx::Interpreter& vm, const Context& context);
[[nodiscard]] bool ScrollAndroidDialog(DexVmAndroidContext& context, dx::VmObjectRef dialog, std::int32_t direction);
void ShowAndroidDialog(dx::IntrinsicContext& call, const Context& context, dx::IntrinsicFieldHandle generation);
void DismissAndroidDialog(DexVmAndroidContext& context, dx::VmObjectRef dialog);
void SetAndroidDialogVisible(DexVmAndroidContext& context, dx::VmObjectRef dialog, bool visible);
[[nodiscard]] std::int32_t FocusAndroidDialog(DexVmAndroidContext& context, dx::VmObjectRef dialog,
                                              std::int32_t direction);
[[nodiscard]] dx::VmValue StartAndroidActivity(dx::IntrinsicContext& call, const Context& context,
                                               std::int32_t request_code);

// One guest Rect per holder. Publish managed dimensions before callbacks;
// destruction retains the last frame. The API19 Rect is the View window frame,
// independent of the producer buffer dimensions.
[[nodiscard]] dx::VmObjectRef GetSurfaceHolderFrame(dx::Interpreter& vm, DexVmAndroidContext& context,
                                                    dx::VmObjectRef holder);
[[nodiscard]] ui::Size ManagedSurfaceBufferSize(dx::Interpreter&, DexVmAndroidContext&, dx::VmObjectRef);
void PublishSurfaceHolderFrame(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef holder);

// Private, GC-owned MotionEvent snapshot encoding. Header followed by (id, tool)
// pairs and samples: timestamp + pointerCount * 64 axis bit patterns. Sample 0
// is current; samples 1..N are historical in chronological order.
struct MotionSnapshotLayout final {
    enum : std::size_t {
        action,
        device,
        source,
        flags,
        meta,
        buttons,
        edges,
        down_time,
        x_offset,
        y_offset,
        x_precision,
        y_precision,
        pointer_count,
        history_count,
        header_size
    };

    static constexpr std::size_t axis_count = 64;

    static constexpr std::size_t Sample(std::size_t pointers, std::size_t sample) {
        return header_size + 2 * pointers + sample * (1 + axis_count * pointers);
    }
};

// Normal guest dispatch; preserve Java exception identity across the boundary.
void RejectUnsupportedServiceMethod(dx::IntrinsicClassBuilder& builder, std::string_view owner, const char* name,
                                    const char* descriptor);
[[nodiscard]] dx::VmObjectRef NewPlatformObject(dx::Interpreter& vm, const char* type, const char* signature,
                                                std::vector<dx::VmValue> arguments);
[[nodiscard]] dx::VmObjectRef GetLocalServiceInterface(dx::Interpreter& vm, const char* name, const char* stub,
                                                       const char* interface_type);
[[nodiscard]] dx::VmValue CallAndroidMethod(dx::Interpreter& vm, dx::VmObjectRef receiver, const char* name,
                                            const char* signature, std::vector<dx::VmValue> arguments = {});
[[nodiscard]] dx::VmObjectRef NewAndroidComponentName(dx::Interpreter& vm, dx::VmObjectRef package,
                                                      dx::VmObjectRef class_name);
[[nodiscard]] dx::VmObjectRef NewAndroidBundle(dx::Interpreter& vm, dx::VmObjectRef source = dx::VmObjectRef{});
[[nodiscard]] dx::VmObjectRef NewAndroidLayoutParams(dx::Interpreter& vm, const char* descriptor, std::int32_t width,
                                                     std::int32_t height);

// The managed ANGLE window path guarantees ES 2.0.
inline constexpr std::int32_t kManagedGlesVersion = 2 << 16;
[[nodiscard]] std::int32_t DeviceDensityDpi(const Context& context);
void PopulateAndroidDisplayMetrics(dx::IntrinsicContext& call, const Context& context, dx::VmObjectRef metrics);
[[nodiscard]] ui::LayoutParams ReadAndroidLayoutParams(dx::Interpreter& vm, dx::VmObjectRef params);

// Helpers shared across handler batches; batch-local helpers stay private to
// their translation unit.
[[nodiscard]] dx::VmValue Self(dx::IntrinsicContext& call);
// Per-context instance cache: platform singletons (window, locale, service
// managers) must answer the same object every call.
[[nodiscard]] dx::VmObjectRef Singleton(dx::IntrinsicContext& call, const Context& context, const std::string& key,
                                        const char* descriptor);
[[nodiscard]] dx::VmObjectRef ResourceObject(dx::IntrinsicContext& call, const Context& context, bool system);
[[nodiscard]] dx::VmObjectRef ResourceAssets(dx::IntrinsicContext& call, const Context& context, bool system);
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
[[nodiscard]] dx::IntrinsicHandler PlaceholderString(std::string value = {});
void GuestLog(dx::IntrinsicContext& call, core::LogLevel level, const std::string& line);
// A display-only UI request was ignored instead of failing the game: warns once
// per id and counts the rest in the ledger. Not for anything guest logic reads back.
void RecordUiDegraded(dx::Interpreter& vm, std::string_view what);
// Handler for a void display request that is ignored with a warning.
[[nodiscard]] dx::IntrinsicHandler IgnoredUiRequest(std::string what);
// Missing or damaged entries throw the Java IOException the glue catches.
void RetireAndroidTypeface(DexVmAndroidContext& context, int bits);
std::vector<std::byte> ReadAndroidApkAsset(dx::Interpreter& vm, const Context& context, dx::VmObjectRef manager,
                                           const std::string& name);
// The session thread runtime; absent only if the platform context was never
// wired to a bridge, which is a host assembly defect rather than a gap.
[[nodiscard]] dx::VmThreadRuntime& ThreadRuntime(const Context& context);

[[nodiscard]] dx::VmObjectRef EnsureMainLooper(dx::IntrinsicContext& call, const Context& context);
[[nodiscard]] dx::VmObjectRef EnsureMainLooper(dx::Interpreter& vm, const Context& context);
[[nodiscard]] dx::VmObjectRef CurrentLooper(const Context& context, std::uint64_t context_token);
[[nodiscard]] dx::VmObjectRef RegisterCurrentRomLooper(dx::Interpreter& vm, const Context& context);
void PumpNativeLooperEvents(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef looper);
[[nodiscard]] bool EnqueueHandlerWork(dx::Interpreter& vm, const Context& context, dx::VmObjectRef looper,
                                      dx::VmObjectRef handler, dx::VmObjectRef payload, dx::VmObjectRef token,
                                      std::int32_t what, bool runnable, std::int64_t deadline_millis,
                                      bool front = false);
// Resolve attachment from Java parents and a live managed window generation.
[[nodiscard]] dx::VmObjectRef ManagedViewWindowRoot(dx::Interpreter& vm, DexVmAndroidContext& context,
                                                    dx::VmObjectRef view);
[[nodiscard]] bool PostViewRunnable(dx::IntrinsicContext& call, const Context& context, dx::VmObjectRef view,
                                    dx::VmObjectRef runnable, std::int64_t delay_millis);

struct DisabledWebTarget final {
    bool javascript{};
    bool http_or_https{};
    std::string url;
    std::string host;
    std::string path;
};

[[nodiscard]] DisabledWebTarget ParseDisabledWebTarget(std::string_view url);
void LogDisabledWeb(dx::Interpreter& vm, std::string_view action, const DisabledWebTarget& target = {});
void RemoveHandlerWork(dx::Interpreter& vm, const Context& context, dx::VmObjectRef handler,
                       std::optional<std::int32_t> what, dx::VmObjectRef payload, bool runnable, dx::VmObjectRef token,
                       bool match_token);
// Video view state shared by the VideoView intrinsics and the guest video
// pump (android_media.cpp): lookup, playback-position math, and the
// onCompletion listener invocation (fresh MediaPlayer argument, matching
// device behaviour; returns a rendered message on guest exceptions).
[[nodiscard]] DexVmAndroidContext::VideoViewState* VideoStateOf(const Context& context, std::uint64_t handle);
[[nodiscard]] std::int64_t VideoPositionOf(const DexVmAndroidContext::VideoViewState& state, std::int64_t uptime_ms);
[[nodiscard]] std::optional<std::string> InvokeVideoEvent(dx::Interpreter& vm, DexVmAndroidContext& context,
                                                          std::uint64_t handle, std::int32_t generation,
                                                          std::int32_t event);

// Non-goal SMS/network actions fail with accounting instead of pretending.
// The guest path stored in a java.io.File instance (slot 0).
[[nodiscard]] std::string FilePathOf(dx::IntrinsicContext& call, dx::VmObjectRef file);
inline constexpr std::int32_t kVisible = 0;
inline constexpr std::int32_t kInvisible = 4;
inline constexpr std::int32_t kGone = 8;
[[nodiscard]] std::int32_t VisibilityOf(const DexVmAndroidContext& context, std::uint64_t handle);
[[nodiscard]] ui::UiClass UiClassForDescriptor(std::string_view descriptor);
// Resolves the platform widget kind by walking the receiver's real class
// chain, so guest subclasses of framework widgets keep the base behaviour.
[[nodiscard]] ui::UiClass UiClassForObject(dx::Interpreter& vm, const dx::VmObjectRef view);
// Shared handler factories: cross-class handlers built on demand by the
// per-class declaration units. Factories that bind session state take the
// platform context; stateless ones take nothing.
[[nodiscard]] dx::IntrinsicHandler EditableClearHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler EditableLengthHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler EditableReplaceHandler(const Context& context);

// Single writer for TextView/EditText mutations. Every source (host keys,
// setText and Editable methods) uses the same filters and guest TextWatcher
// transaction.
bool ApplyTextEdit(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef view, std::int32_t start,
                   std::int32_t before_count, const std::u16string& replacement, bool collapse_selection);
[[nodiscard]] dx::IntrinsicHandler GraphicsNoopHandler();
// Interface surface with no checked storage or callback truth: records the
// named gap and fails instead of accepting a silent no-op.
[[nodiscard]] dx::IntrinsicHandler SurfaceHolderAddCallbackHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler SurfaceHolderRemoveCallbackHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler SurfaceHolderSetFormatHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler SurfaceHolderSetTypeHandler();
[[nodiscard]] dx::IntrinsicHandler ViewInitHandler(const Context& context);
[[nodiscard]] dx::VmObjectRef SurfaceHolderForView(dx::Interpreter& vm, DexVmAndroidContext& context,
                                                   dx::VmObjectRef view);
[[nodiscard]] dx::IntrinsicHandler SurfaceViewInitHandler(const Context& context, bool with_attributes = false);
bool RefreshSurfaceViewVisibility(dx::Interpreter&, DexVmAndroidContext&, dx::VmObjectRef,
                                  std::optional<int> window_visibility = std::nullopt);
[[nodiscard]] bool SurfaceViewReadyForRendering(const DexVmAndroidContext& context, dx::VmObjectRef view);
// Programmatic View(Context, AttributeSet) with attrs == null. Non-null
// AttributeSet is an explicit XML/theme gap, not a silent default.
[[nodiscard]] dx::IntrinsicHandler ViewNullAttributeSetInitHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler ViewSetIdHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler WidgetNoopHandler();
[[nodiscard]] dx::VmObjectRef DefaultAndroidDisplay(dx::IntrinsicContext& call, const Context& context);
[[nodiscard]] dx::IntrinsicHandler WindowmanagerGetDefaultDisplayHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformSystemCurrentTimeMillisHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformRuntimeNativeExitHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformSystemLoadHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformSystemLoadLibraryHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformClassLoaderFindLibraryHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformSystemNanoTimeHandler(const Context& context);

}  // namespace ogplay::runtime::android_intrinsics

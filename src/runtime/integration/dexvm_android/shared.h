#pragma once

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/integration/dexvm_android.h"

// Private helpers shared by the android.* per-class declaration units. This
// is not part of the module's public surface.

namespace ogplay::runtime::android_intrinsics {

namespace dx = dexvm;

using Decl = dexvm::IntrinsicClassDecl;
using Context = std::shared_ptr<DexVmAndroidContext>;

// Helpers shared across handler batches; batch-local helpers stay private to
// their translation unit.
[[nodiscard]] dx::VmValue Self(dx::IntrinsicContext& call);
// Per-context instance cache: platform singletons (window, locale, service
// managers) must answer the same object every call.
[[nodiscard]] dx::VmObjectRef Singleton(dx::IntrinsicContext& call,
                                        const Context& context,
                                        const std::string& key,
                                        const char* descriptor);
[[nodiscard]] dx::VmValue MakeString(dx::IntrinsicContext& call,
                                     const std::string& value);
[[nodiscard]] dx::IntrinsicHandler NeutralHandler(char shorty);
[[nodiscard]] dx::IntrinsicHandler PlaceholderString(std::string value = {});
void GuestLog(dx::IntrinsicContext& call, core::LogLevel level,
              const std::string& line);
dx::VmObjectRef OpenStream(dx::IntrinsicContext& call, const Context& context,
                           std::vector<std::byte> bytes,
                           const char* descriptor = "Ljava/io/InputStream;");
// Missing or damaged entries throw the Java IOException the glue catches.
[[nodiscard]] std::vector<std::byte> ReadApkFile(const Context& context,
                                                 const std::string& path);
// Interprets the target's run() to completion on the calling host thread.
// Used for cooperative java.util.Timer tasks, whose delay collapses to the
// next lifecycle frame boundary; java.lang.Thread goes to ThreadRuntime.
// Returns a rendered message when the body raised an uncaught exception.
[[nodiscard]] std::optional<std::string> RunJavaThreadNow(
    dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef thread);

// The session thread runtime; absent only if the platform context was never
// wired to a bridge, which is a host assembly defect rather than a gap.
[[nodiscard]] dx::VmThreadRuntime& ThreadRuntime(const Context& context);

// Video view state shared by the VideoView intrinsics and the guest video
// pump (android_media.cpp): lookup, playback-position math, and the
// onCompletion listener invocation (fresh MediaPlayer argument, matching
// device behaviour; returns a rendered message on guest exceptions).
[[nodiscard]] DexVmAndroidContext::VideoViewState* VideoStateOf(
    const Context& context, std::uint64_t handle);
[[nodiscard]] std::int64_t VideoPositionOf(
    const DexVmAndroidContext::VideoViewState& state, std::int64_t uptime_ms);
[[nodiscard]] std::optional<std::string> InvokeVideoCompletionListener(
    dx::Interpreter& vm, DexVmAndroidContext& context, std::uint64_t handle);

// Non-goal SMS/network actions fail with accounting instead of pretending.
dx::VmValue UnsupportedNetwork(dx::IntrinsicContext&);
[[nodiscard]] std::string PreferencesPathOf(const Context& context,
                                            const std::string& name);
// The guest path stored in a java.io.File instance (slot 0).
[[nodiscard]] std::string FilePathOf(dx::IntrinsicContext& call,
                                     dx::VmObjectRef file);
inline constexpr std::int32_t kVisible = 0;
inline constexpr std::int32_t kInvisible = 4;
inline constexpr std::int32_t kGone = 8;
[[nodiscard]] std::int32_t VisibilityOf(const DexVmAndroidContext& context,
                                        std::uint64_t handle);
[[nodiscard]] ui::UiClass UiClassForDescriptor(std::string_view descriptor);
void DeliverMessage(dx::IntrinsicContext& call, dx::VmObjectRef handler,
                    dx::VmObjectRef message);
[[nodiscard]] dx::VmObjectRef MakeMessage(dx::IntrinsicContext& call,
                                          std::int32_t what,
                                          dx::VmObjectRef object,
                                          dx::VmObjectRef target);

// Shared handler factories: cross-class handlers built on demand by the
// per-class declaration units. Factories that bind session state take the
// platform context; stateless ones take nothing.
[[nodiscard]] dx::IntrinsicHandler EditableClearHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler EditableLengthHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler EditableReplaceHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler GraphicsNoopHandler();
[[nodiscard]] dx::IntrinsicHandler PrefsEditHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PrefsEditorCommitHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler PrefsEditorPutBooleanHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler PrefsEditorPutIntHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler PrefsEditorPutLongHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler PrefsEditorPutStringHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler PrefsGetBooleanHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler PrefsGetIntHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PrefsGetLongHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler PrefsGetStringHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler SurfaceHolderAddCallbackHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler SurfaceHolderSetFormatHandler();
[[nodiscard]] dx::IntrinsicHandler SurfaceHolderSetTypeHandler();
[[nodiscard]] dx::IntrinsicHandler TelephonyEmptyStringHandler();
[[nodiscard]] dx::IntrinsicHandler TelephonyFalseHandler();
[[nodiscard]] dx::IntrinsicHandler ViewInitHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler ViewSetIdHandler(const Context& context);
[[nodiscard]] dx::IntrinsicHandler WidgetNoopHandler();
[[nodiscard]] dx::IntrinsicHandler WindowmanagerGetDefaultDisplayHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformDateGetTimeHandler();
[[nodiscard]] dx::IntrinsicHandler PlatformDateGetYearHandler();
[[nodiscard]] dx::IntrinsicHandler PlatformDateInitHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformSystemCurrentTimeMillisHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformSystemExitHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformSystemLoadHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformSystemLoadLibraryHandler(
    const Context& context);
[[nodiscard]] dx::IntrinsicHandler PlatformSystemNanoTimeHandler(
    const Context& context);

}  // namespace ogplay::runtime::android_intrinsics

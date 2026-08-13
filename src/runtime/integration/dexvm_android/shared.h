#pragma once

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/integration/dexvm_android.h"

// Private helpers shared by the android.* per-class declaration units. This
// is not part of the module's public surface.

namespace ogplay::runtime::android_intrinsics {

namespace dx = dexvm;

using Decl = dexvm::IntrinsicClassDecl;
using Context = std::shared_ptr<DexVmAndroidContext>;

#include "handlers.inc"

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
[[nodiscard]] DexVmAndroidContext::Stream& StreamOf(dx::IntrinsicContext& call,
                                                    const Context& context);
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

void PopulateContextActivity(AndroidHandlers& handlers, const Context& context);
void PopulateViewSurface(AndroidHandlers& handlers, const Context& context);
void PopulateResources(AndroidHandlers& handlers, const Context& context);
void PopulateStreams(AndroidHandlers& handlers, const Context& context);
void PopulateFiles(AndroidHandlers& handlers, const Context& context);
void PopulateDeviceServices(AndroidHandlers& handlers, const Context& context);
void PopulateAudioVideo(AndroidHandlers& handlers, const Context& context);
void PopulateSharedPreferences(AndroidHandlers& handlers,
                               const Context& context);
void PopulateGraphicsBitmaps(AndroidHandlers& handlers, const Context& context);
void PopulateWidgets(AndroidHandlers& handlers, const Context& context);
void PopulateVideoViews(AndroidHandlers& handlers, const Context& context);
void PopulateWidgetDispatch(AndroidHandlers& handlers, const Context& context);
void PopulateMisc(AndroidHandlers& handlers, const Context& context);

}  // namespace ogplay::runtime::android_intrinsics

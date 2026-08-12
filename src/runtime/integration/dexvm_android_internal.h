#pragma once

#include "ogplay/runtime/integration/dexvm_android.h"

// Private glue shared by the android.* intrinsic translation units
// (dexvm_android_catalog_*.cpp declare classes, dexvm_android_*.cpp bind
// handlers). Not part of the module's public surface: only the catalog and
// RegisterAndroidBuiltins in dexvm_android.h are.

namespace ogplay::runtime::android_intrinsics {

namespace dx = dexvm;

using Decl = dexvm::IntrinsicClassDecl;
using Context = std::shared_ptr<DexVmAndroidContext>;

// Catalog batches, appended in this order by AndroidIntrinsicCatalog().
void AppendCoreClasses(std::vector<Decl>& catalog);
void AppendIoClasses(std::vector<Decl>& catalog);
void AppendDeviceClasses(std::vector<Decl>& catalog);
void AppendGraphicsClasses(std::vector<Decl>& catalog);
void AppendWidgetClasses(std::vector<Decl>& catalog);

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
// Interprets the target's run() to completion on the calling host thread;
// returns a rendered message when the body raised an uncaught exception.
[[nodiscard]] std::optional<std::string> RunJavaThreadNow(
    dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef thread);

// Handler batches, installed together by RegisterAndroidBuiltins.
void RegisterContextActivity(dx::IntrinsicRegistry& registry,
                             const Context& context);
void RegisterViewSurface(dx::IntrinsicRegistry& registry,
                         const Context& context);
void RegisterResources(dx::IntrinsicRegistry& registry,
                       const Context& context);
void RegisterStreams(dx::IntrinsicRegistry& registry, const Context& context);
void RegisterFiles(dx::IntrinsicRegistry& registry, const Context& context);
void RegisterDeviceServices(dx::IntrinsicRegistry& registry,
                            const Context& context);
void RegisterAudioVideo(dx::IntrinsicRegistry& registry,
                        const Context& context);
void RegisterSharedPreferences(dx::IntrinsicRegistry& registry,
                               const Context& context);
void RegisterGraphicsBitmaps(dx::IntrinsicRegistry& registry,
                             const Context& context);
void RegisterWidgets(dx::IntrinsicRegistry& registry, const Context& context);
void RegisterVideoViews(dx::IntrinsicRegistry& registry,
                        const Context& context);
void RegisterWidgetDispatch(dx::IntrinsicRegistry& registry,
                            const Context& context);
void RegisterMisc(dx::IntrinsicRegistry& registry, const Context& context);

}  // namespace ogplay::runtime::android_intrinsics

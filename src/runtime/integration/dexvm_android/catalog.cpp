#include "catalog.h"
#include "runtime/integration/guest_network_environment.h"
#include "ogplay/runtime/integration/guest_process_environment.h"
#include "ogplay/runtime/boundary/device_locale.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "ogplay/hal/host_environment.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "shared.h"

namespace ogplay::runtime {

dexvm::CoreIntrinsicServices AndroidCoreIntrinsicServices(const std::shared_ptr<DexVmAndroidContext>& context) {
    dexvm::CoreIntrinsicServices services;
    services.secure_random = hal::FillSecureRandom;
    if (context == nullptr) return services;
    services.create_dex_buffer = [context](dexvm::Interpreter& vm, std::span<const std::uint8_t> bytes) {
        if (!context->session || bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                     "DEX guest memory provider is unavailable"};
        auto& process = context->session->Process();
        const auto memory = process.GuestMemoryAccess();
        const auto size = static_cast<std::uint32_t>(bytes.size());
        const auto address = memory.allocate(size);
        try {
            memory.write(address, std::as_bytes(bytes));
            const auto buffer = vm.NewIntrinsicInstance("Ljava/nio/DirectByteBuffer;");
            const auto root = vm.ProtectReferences(std::array{buffer});
            // Exactly the external-memory path used by JNI NewDirectByteBuffer.
            vm.NIO().WrapDirect(vm.Model().ToIdentity(buffer), address, static_cast<std::int32_t>(size));
            const auto read_only =
                android_intrinsics::CallAndroidMethod(vm, buffer, "asReadOnlyBuffer", "()Ljava/nio/ByteBuffer;").ref;
            process.ProtectGuestReadOnly(address, size);
            return read_only;
        } catch (...) {
            memory.release(address, size);
            throw;
        }
    };
    services.process_id = [] { return android_intrinsics::kGuestProcessId; };
    services.process_uid = [context] { return static_cast<std::int32_t>(context->application_uid); };
    services.current_thread_id = [context](dexvm::Interpreter& vm) {
        const auto token = vm.CurrentContextToken();
        const auto tid = context->native_thread_for_context ? context->native_thread_for_context(token) : token;
        if (tid > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
            throw std::overflow_error("guest TID exceeds API19 integer range");
        return static_cast<std::int32_t>(tid);
    };
    if (context->offline_connectivity && !context->network_policy.enabled && !context->network_environment) {
        auto environment = std::make_shared<GuestNetworkEnvironment>(*context->offline_connectivity);
        if (context->vfs) environment->Publish(*context->vfs);
        context->network_environment = std::move(environment);
    }
    services.network_interfaces = [context](dexvm::Interpreter& vm) -> const std::vector<GuestNetworkInterface>& {
        if (!context->offline_connectivity || context->network_policy.enabled || !context->network_environment) {
            if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.connectivity_offline", 0);
            throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                     "guest interface topology is unavailable"};
        }
        return context->network_environment->Interfaces();
    };
    static_cast<void>(DeviceLocale(context->locale_language, context->locale_region));
    services.initialize_system_properties = [context](dexvm::Interpreter& vm) {
        android_intrinsics::InitializeAndroidSystemProperties(vm, context);
    };
    services.boot_classpath_size = [context] { return static_cast<std::int32_t>(context->boot_classpath.size()); };
    services.find_boot_classpath_resource =
        [context](std::string_view name,
                  std::int32_t index) -> std::optional<dexvm::CoreIntrinsicServices::ClasspathResource> {
        if (index < 0 || static_cast<std::size_t>(index) >= context->boot_classpath.size()) return std::nullopt;
        const auto& jar = context->boot_classpath[static_cast<std::size_t>(index)];
        if (std::none_of(jar.archive.entries.begin(), jar.archive.entries.end(),
                         [name](const auto& entry) { return entry.name == name; }))
            return std::nullopt;
        return dexvm::CoreIntrinsicServices::ClasspathResource{jar.guest_path, std::string(name)};
    };
    services.read_classpath_resource =
        [context](
            const dexvm::CoreIntrinsicServices::ClasspathResource& resource) -> std::optional<std::vector<std::byte>> {
        const auto boot =
            std::find_if(context->boot_classpath.begin(), context->boot_classpath.end(),
                         [&resource](const BootClasspathJar& jar) { return jar.guest_path == resource.archive_path; });
        const bool is_boot = boot != context->boot_classpath.end();
        if (!is_boot &&
            (context->package_resource_path.empty() || resource.archive_path != context->package_resource_path)) {
            throw std::invalid_argument("classpath archive is not registered: " + resource.archive_path);
        }
        const auto& archive = is_boot ? boot->archive : context->archive;
        const auto found =
            std::find_if(archive.entries.begin(), archive.entries.end(),
                         [&resource](const loader::ApkEntry& entry) { return entry.name == resource.entry_name; });
        if (found == archive.entries.end()) return std::nullopt;
        return loader::ReadApkEntry(is_boot ? std::span<const std::byte>(boot->bytes) : context->ApkBytes(), archive,
                                    resource.entry_name);
    };
    services.singleton = [context](dexvm::Interpreter& vm, const std::string_view key,
                                   const std::string_view descriptor) {
        const auto found = context->singletons.find(std::string(key));
        if (found != context->singletons.end()) return found->second;
        const auto instance = vm.NewIntrinsicInstance(std::string(descriptor));
        context->singletons.emplace(std::string(key), instance);
        return instance;
    };
    services.current_time_millis = [context] {
        return 1'400'000'000'000LL + context->uptime_millis.load(std::memory_order_relaxed);
    };
    services.environment_value = [context](const std::string_view name) {
        if (context->session == nullptr) {
            const auto environment = GuestProcessEnvironment::Api19(context->external_storage_root);
            const auto value = environment.Find(name);
            return value ? std::optional<std::string>{*value} : std::nullopt;
        }
        return context->session->Process().ProcessEnvironmentValue(name);
    };
    services.environment_entries = [context] {
        if (context->session == nullptr) {
            std::vector<std::pair<std::string, std::string>> result;
            const auto environment = GuestProcessEnvironment::Api19(context->external_storage_root);
            for (const auto& entry : environment.Entries())
                result.emplace_back(entry.name, entry.value);
            return result;
        }
        std::vector<std::pair<std::string, std::string>> result;
        for (const auto& entry : context->session->Process().ProcessEnvironmentEntries()) {
            result.emplace_back(entry.name, entry.value);
        }
        return result;
    };
    return services;
}

std::vector<dexvm::IntrinsicClassDecl> AndroidIntrinsicCatalog(const std::shared_ptr<DexVmAndroidContext>& context) {
    using namespace android_intrinsics;
    DebugMemoryInfoFields memory_fields;
    auto debug_memory_info = Declare_android_os_Debug_MemoryInfo_Fields(context, memory_fields);
    auto debug = Declare_android_os_Debug_Natives(context, memory_fields);
    std::vector<dexvm::IntrinsicClassDecl> result{
        std::move(debug_memory_info),
        std::move(debug),
        Declare_android_app_NativeActivity_Natives(context),
        Declare_android_view_InputQueue_Natives(context),
        Declare_android_view_Surface_Natives(context),
        Declare_android_graphics_SurfaceTexture_Natives(context),
        Declare_local_ActivityService_Class(context),
        Declare_dalvik_system_DexFile_Natives(context),
        Declare_local_PackageService_Class(context),
        Declare_org_apache_harmony_dalvik_ddmc_DdmServer_Natives(),
        Declare_local_BackupService_Class(context),
        Declare_local_NotificationService_Class(context),
        Declare_local_WindowService_Class(context),
        Declare_local_IntentSender_Class(context),
        Declare_local_AlarmService_Class(context),
        Declare_local_ContentService_Class(context),
        Declare_local_ClipboardService_Class(context),
        Declare_local_UiModeService_Class(context),
        Declare_local_ContentChange_Class(context),
        Declare_android_content_res_AssetManager_Natives(context),
        Declare_android_database_CursorWindow_Natives(context),
        Declare_android_database_sqlite_SQLiteConnection_Natives(context),
        Declare_android_database_sqlite_SQLiteConnection_Operation_Overrides(context),
        Declare_android_database_sqlite_SQLiteDebug_Natives(context),
        Declare_android_database_sqlite_SQLiteGlobal_Natives(context),
        Declare_android_graphics_Bitmap_Natives(context),
        Declare_android_graphics_BitmapFactory_Options_Natives(context),
        Declare_android_graphics_BitmapFactory_Natives(context),
        Declare_android_graphics_NinePatch_Natives(context),
        Declare_android_graphics_Canvas_Natives(context),
        Declare_android_graphics_DrawFilter_Natives(context),
        Declare_android_graphics_PaintFlagsDrawFilter_Natives(context),
        Declare_android_graphics_Matrix_Natives(context),
        Declare_android_graphics_Paint_Natives(context),
        Declare_android_graphics_Path_Natives(context),
        Declare_android_graphics_Color_Natives(context),
        Declare_android_graphics_Region_Natives(context),
        Declare_android_graphics_RegionIterator_Natives(context),
        Declare_android_graphics_PathMeasure_Natives(context),
        Declare_android_view_GLES20Canvas_Natives(context),
        Declare_android_graphics_Typeface_Natives(context),
        Declare_android_graphics_drawable_Drawable_Shell(context),
        Declare_android_graphics_Camera_Natives(context),
        Declare_android_hardware_SystemSensorManager_Natives(context),
        Declare_local_UsbService_Class(context),
        Declare_local_LocationService_Class(context),
        Declare_android_media_AudioSystem_Natives(context),
        Declare_local_AudioService_Class(context),
        Declare_android_media_AudioTrack_Natives(context),
        Declare_android_media_MediaPlayer_Natives(context),
        Declare_android_media_SoundPool_SoundPoolImpl_Natives(context),
        Declare_local_ConnectivityService_Class(context),
        Declare_local_WifiService_Class(context),
        Declare_android_opengl_GLES10_Class(context),
        Declare_android_opengl_GLES10Ext_Class(context),
        Declare_android_opengl_GLES11_Class(context),
        Declare_android_opengl_GLES11Ext_Class(context),
        Declare_android_opengl_GLES20_Class(context),
        Declare_android_opengl_GLES30_Class(context),
        Declare_android_opengl_GLUtils_Class(context),
        Declare_android_opengl_GLU_Class(context),
        Declare_android_os_SystemProperties_Natives(context),
        Declare_android_os_Binder_Natives(context),
        Declare_com_android_internal_os_BinderInternal_Natives(context),
        Declare_local_ServiceManager_Class(context),
        Declare_local_MountService_Class(context),
        Declare_android_os_MessageQueue_Natives(context),
        Declare_android_os_SystemClock_Natives(context),
        Declare_android_os_Parcel_Natives(context),
        Declare_local_PowerService_Class(context),
        Declare_local_VibratorService_Class(context),
        Declare_android_os_Process_Natives(context),
        Declare_local_SettingsProvider_Class(context),
        Declare_local_SmsService_Class(context),
        Declare_local_PhoneSubscriberService_Class(context),
        Declare_local_TelephonyService_Class(context),
        Declare_local_TelephonyRegistry_Class(context),
        Declare_android_text_AndroidBidi_Natives(context),
        Declare_android_emoji_EmojiFactory_Natives(),
        Declare_android_text_format_Time_Natives(context),
        Declare_android_util_Log_Natives(context),
        Declare_android_util_EventLog_Natives(context),
        Declare_android_util_AttributeSet_Shell(context),
        Declare_android_util_FloatMath_Natives(context),
        Declare_android_animation_PropertyValuesHolder_Natives(context),
        Declare_android_view_DisplayEventReceiver_Natives(context),
        Declare_android_view_HardwareRenderer_Natives(),
        Declare_local_DisplayService_Class(context),
        Declare_local_AccessibilityService_Class(context),
        Declare_local_TextServices_Class(),
        Declare_android_view_ContextThemeWrapper_Shell(context),
        Declare_android_view_KeyEvent_Natives(context),
        Declare_android_view_KeyCharacterMap_Natives(context),
        Declare_android_view_MotionEvent_Natives(context),
        Declare_android_view_ViewTreeObserver_OnGlobalLayoutListener_Shell(context),
        Declare_android_view_ViewTreeObserver_Shell(context),
        Declare_android_widget_Button_Shell(context),
        Declare_android_widget_FrameLayout_Shell(context),
        Declare_android_widget_ImageButton_Shell(context),
        Declare_android_widget_ImageView_ScaleType_Shell(context),
        Declare_android_widget_LinearLayout_Shell(context),
        Declare_android_widget_ProgressBar_Shell(context),
        Declare_android_widget_RelativeLayout_Shell(context),
        Declare_android_opengl_EGL14_Class(context),
        Declare_android_opengl_EGLObjectHandle_Class(context),
        Declare_android_opengl_EGLConfig_Class(context),
        Declare_android_opengl_EGLContext_Class(context),
        Declare_android_opengl_EGLDisplay_Class(context),
        Declare_android_opengl_EGLSurface_Class(context),
        // Historical compatibility tail. DVM-61 decouples Java identity hash
        // from linker ids and object handles, so future catalog insertion no
        // longer changes Object.hashCode/default toString identity.
        Declare_com_google_android_gles_jni_GLImpl_Natives(context),
        Declare_com_google_android_gles_jni_EGLImpl_Natives(context),
    };
    {
        result.push_back(Declare_android_graphics_drawable_StateListDrawable_Shell(context));
        result.push_back(Declare_android_os_Trace_Natives(context));
        result.push_back(Declare_android_content_res_XmlBlock_Natives(context));
        result.push_back(Declare_android_content_res_StringBlock_Natives(context));
    }
    for (auto& declaration : Declare_android_graphics_Effects_Natives(context))
        result.push_back(std::move(declaration));
    for (auto& declaration : Declare_android_graphics_StateObjects_Natives(context))
        result.push_back(std::move(declaration));
    {
        result.push_back(Declare_local_WindowSession_Class(context));
        result.push_back(Declare_android_view_SurfaceControl_Natives(context));
        result.push_back(Declare_android_view_SurfaceSession_Natives(context));
        result.push_back(Declare_android_view_InputChannel_Natives(context));
        result.push_back(Declare_android_view_InputEventReceiver_Natives(context));
        result.push_back(Declare_android_view_InputEventSender_Natives(context));
        result.push_back(Declare_local_InputMethodService_Class(context));
        result.push_back(Declare_local_InputMethodSession_Class(context));
        result.push_back(Declare_local_InputService_Class(context));
        result.push_back(Declare_android_view_VelocityTracker_Natives(context));
    }
    return result;
}

}  // namespace ogplay::runtime

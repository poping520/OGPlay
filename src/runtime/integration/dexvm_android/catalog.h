#pragma once

#include "shared.h"
#include <vector>

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_animation_PropertyValuesHolder_Natives(const Context&);
Decl Declare_android_view_DisplayEventReceiver_Natives(const Context&);
Decl Declare_android_view_HardwareRenderer_Natives();
Decl Declare_local_DisplayService_Class(const Context&);
Decl Declare_dalvik_system_DexFile_Natives(const Context&);
void AddLocalActivityBindingMethods(dx::IntrinsicClassBuilder&, const Context&);
void AddLocalIntentSenderMethods(dx::IntrinsicClassBuilder&, const Context&);
void AddLocalProviderMethods(dx::IntrinsicClassBuilder&, const Context&);
dx::VmObjectRef ApplicationProviders(dx::IntrinsicContext&, const Context&, dx::VmObjectRef application_info);
Decl Declare_com_google_android_gles_jni_EGLImpl_Natives(const Context&);
void PumpAndroidHostRequests(dx::Interpreter&, const Context&);
void PumpRomToastQueue(dx::Interpreter&, const Context&);
Decl Declare_local_AccessibilityService_Class(const Context&);
Decl Declare_local_TextServices_Class();
Decl Declare_android_emoji_EmojiFactory_Natives();
Decl Declare_android_os_Trace_Natives(const Context& context);
Decl Declare_android_content_res_XmlBlock_Natives(const Context& context);
Decl Declare_android_content_res_StringBlock_Natives(const Context& context);
using DebugMemoryInfoFields = std::vector<dx::IntrinsicFieldHandle>;
[[nodiscard]] Decl Declare_android_os_Debug_MemoryInfo_Fields(const Context&, DebugMemoryInfoFields&);
[[nodiscard]] Decl Declare_android_os_Debug_Natives(const Context&, const DebugMemoryInfoFields&);

[[nodiscard]] Decl Declare_android_app_NativeActivity_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_view_Surface_Natives(const Context& context);
Decl Declare_android_view_InputQueue_Natives(const Context& context);
Decl Declare_local_WindowSession_Class(const Context& context);
Decl Declare_android_view_SurfaceControl_Natives(const Context& context);
Decl Declare_android_view_SurfaceSession_Natives(const Context& context);
void AddRomActivityMethods(dx::IntrinsicClassBuilder& builder, const Context& context);
void SetRomApplicationThread(const Context& context, dx::VmObjectRef thread);
Decl Declare_android_view_InputChannel_Natives(const Context& context);
Decl Declare_android_view_InputEventReceiver_Natives(const Context& context);
Decl Declare_android_view_InputEventSender_Natives(const Context& context);
Decl Declare_local_InputMethodService_Class(const Context& context);
Decl Declare_local_InputMethodSession_Class(const Context& context);
Decl Declare_local_InputService_Class(const Context& context);
Decl Declare_android_view_VelocityTracker_Natives(const Context& context);
dx::VmValue MakeInputDevice(dx::IntrinsicContext& call, const Context& context);
std::optional<DexVmAndroidContext::ActivityCommand>
ResolveAndroidActivity(dx::IntrinsicContext& call, const Context& context, std::int32_t request_code);
[[nodiscard]] Decl Declare_local_ActivityService_Class(const Context& context);
[[nodiscard]] Decl Declare_local_PackageService_Class(const Context& context);
[[nodiscard]] Decl Declare_org_apache_harmony_dalvik_ddmc_DdmServer_Natives();
[[nodiscard]] Decl Declare_local_BackupService_Class(const Context& context);
[[nodiscard]] Decl Declare_local_NotificationService_Class(const Context& context);
[[nodiscard]] Decl Declare_local_WindowService_Class(const Context& context);
[[nodiscard]] Decl Declare_local_IntentSender_Class(const Context& context);
[[nodiscard]] Decl Declare_local_AlarmService_Class(const Context& context);
Decl Declare_local_ContentService_Class(const Context&);
Decl Declare_local_ClipboardService_Class(const Context&);
Decl Declare_local_UiModeService_Class(const Context&);
Decl Declare_local_ContentChange_Class(const Context&);
void NotifyLocalContentChange(dx::Interpreter&, const Context&, dx::VmObjectRef uri, dx::VmObjectRef sender,
                              bool wants_self);
[[nodiscard]] Decl Declare_android_database_CursorWindow_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_database_sqlite_SQLiteConnection_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_database_sqlite_SQLiteConnection_Operation_Overrides(const Context& context);
[[nodiscard]] Decl Declare_android_database_sqlite_SQLiteDebug_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_database_sqlite_SQLiteGlobal_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_content_res_AssetManager_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Bitmap_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_BitmapFactory_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_NinePatch_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_BitmapFactory_Options_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Canvas_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_DrawFilter_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_PaintFlagsDrawFilter_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Matrix_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Paint_Natives(const Context& context);
[[nodiscard]] std::vector<Decl> Declare_android_graphics_StateObjects_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Path_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Color_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_view_GLES20Canvas_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_PathMeasure_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Region_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_RegionIterator_Natives(const Context& context);
void RegisterSkiaStateTables(dx::Interpreter& vm, const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Typeface_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_drawable_Drawable_Shell(const Context& context);
[[nodiscard]] std::vector<Decl> Declare_android_graphics_Effects_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Camera_Natives(const Context& context);
[[nodiscard]] Decl Declare_local_LocationService_Class(const Context& context);
[[nodiscard]] Decl Declare_android_media_AudioSystem_Natives(const Context& context);
[[nodiscard]] Decl Declare_local_AudioService_Class(const Context& context);
[[nodiscard]] Decl Declare_android_media_AudioTrack_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_media_MediaPlayer_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_media_SoundPool_SoundPoolImpl_Natives(const Context& context);
[[nodiscard]] Decl Declare_local_WifiService_Class(const Context& context);
[[nodiscard]] Decl Declare_local_ConnectivityService_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLES10_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLES10Ext_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLES11_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLES11Ext_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLES20_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLES30_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLUtils_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLU_Class(const Context& context);
[[nodiscard]] Decl Declare_android_os_SystemProperties_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_os_Binder_Natives(const Context& context);
[[nodiscard]] Decl Declare_com_android_internal_os_BinderInternal_Natives(const Context& context);
[[nodiscard]] Decl Declare_local_ServiceManager_Class(const Context& context);
[[nodiscard]] Decl Declare_local_MountService_Class(const Context& context);
[[nodiscard]] Decl Declare_android_os_MessageQueue_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_os_SystemClock_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_os_Parcel_Natives(const Context& context);
[[nodiscard]] Decl Declare_local_PowerService_Class(const Context& context);
[[nodiscard]] Decl Declare_local_VibratorService_Class(const Context& context);
[[nodiscard]] Decl Declare_android_os_Process_Natives(const Context& context);
[[nodiscard]] Decl Declare_local_SettingsProvider_Class(const Context& context);
[[nodiscard]] Decl Declare_local_SmsService_Class(const Context& context);
[[nodiscard]] Decl Declare_local_PhoneSubscriberService_Class(const Context& context);
[[nodiscard]] Decl Declare_local_TelephonyService_Class(const Context& context);
[[nodiscard]] Decl Declare_local_TelephonyRegistry_Class(const Context& context);
[[nodiscard]] Decl Declare_android_text_AndroidBidi_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_text_format_Time_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_util_Log_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_util_EventLog_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_util_AttributeSet_Shell(const Context& context);
[[nodiscard]] Decl Declare_android_util_FloatMath_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_view_ContextThemeWrapper_Shell(const Context& context);
[[nodiscard]] Decl Declare_android_view_KeyEvent_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_view_KeyCharacterMap_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_view_MotionEvent_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_view_ViewTreeObserver_OnGlobalLayoutListener_Shell(const Context& context);
[[nodiscard]] Decl Declare_android_view_ViewTreeObserver_Shell(const Context& context);
[[nodiscard]] Decl Declare_android_widget_Button_Shell(const Context& context);
[[nodiscard]] Decl Declare_android_widget_FrameLayout_Shell(const Context& context);
[[nodiscard]] Decl Declare_android_widget_ImageButton_Shell(const Context& context);
[[nodiscard]] Decl Declare_android_widget_ImageView_ScaleType_Shell(const Context& context);
[[nodiscard]] Decl Declare_android_widget_LinearLayout_Shell(const Context& context);
[[nodiscard]] Decl Declare_android_widget_ProgressBar_Shell(const Context& context);
[[nodiscard]] Decl Declare_android_widget_RelativeLayout_Shell(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_EGL14_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_EGLObjectHandle_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_EGLConfig_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_EGLContext_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_EGLDisplay_Class(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_EGLSurface_Class(const Context& context);
[[nodiscard]] Decl Declare_com_google_android_gles_jni_GLImpl_Natives(const Context& context);
[[nodiscard]] Decl Declare_android_hardware_SystemSensorManager_Natives(const Context& context);
[[nodiscard]] Decl Declare_local_UsbService_Class(const Context& context);
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_SurfaceTexture_Natives(const Context&);
}

namespace ogplay::runtime::android_intrinsics {}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
[[nodiscard]] Decl Declare_android_graphics_drawable_StateListDrawable_Shell(const Context&);
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {}

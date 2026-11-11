#pragma once

#include "shared.h"
#include <vector>

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_animation_PropertyValuesHolder(const Context&);
Decl Declare_android_view_DisplayEventReceiver(const Context&);
Decl Declare_android_view_HardwareRenderer();
Decl Declare_local_DisplayService(const Context&);
Decl Declare_dalvik_system_DexFile(const Context&);
void AddLocalActivityBindingMethods(dx::IntrinsicClassBuilder&, const Context&);
void AddLocalIntentSenderMethods(dx::IntrinsicClassBuilder&, const Context&);
void AddLocalProviderMethods(dx::IntrinsicClassBuilder&, const Context&);
dx::VmObjectRef ApplicationProviders(dx::IntrinsicContext&, const Context&, dx::VmObjectRef application_info);
Decl Declare_test_ListenerInfo(const Context& context);
Decl Declare_test_AccessibilityManager(const Context& context);
Decl Declare_android_view_LayoutInflater_BlinkLayout(const Context& context);
Decl Declare_android_os_Trace(const Context& context);
Decl Declare_android_content_res_XmlBlock(const Context& context);
Decl Declare_android_content_res_StringBlock(const Context& context);
using DebugMemoryInfoFields = std::vector<dx::IntrinsicFieldHandle>;
[[nodiscard]] Decl Declare_android_os_Debug_MemoryInfo(const Context&, DebugMemoryInfoFields&);
[[nodiscard]] Decl Declare_android_os_Debug(const Context&, const DebugMemoryInfoFields&);

[[nodiscard]] Decl Declare_android_app_NativeActivity(const Context& context);
[[nodiscard]] Decl Declare_android_view_LayoutInflater(const Context& context);
[[nodiscard]] Decl Declare_android_view_Surface(const Context& context);
Decl Declare_android_view_InputQueue(const Context& context);
[[nodiscard]] Decl Declare_android_app_Activity(const Context& context);
[[nodiscard]] Decl Declare_local_ActivityService(const Context& context);
[[nodiscard]] Decl Declare_local_PackageService(const Context& context);
[[nodiscard]] Decl Declare_ddm_DdmServer();
[[nodiscard]] Decl Declare_local_BackupService(const Context& context);
[[nodiscard]] dx::VmObjectRef ActivityManagerForContext(dx::IntrinsicContext& call, const Context& context);
[[nodiscard]] Decl Declare_local_NotificationService(const Context& context);
[[nodiscard]] dx::VmObjectRef NotificationManagerForContext(dx::IntrinsicContext& call, const Context& context);
[[nodiscard]] Decl Declare_local_WindowService(const Context& context);
[[nodiscard]] Decl Declare_android_app_AlertDialog_Builder(const Context& context);
[[nodiscard]] Decl Declare_android_app_AlertDialog(const Context& context);
[[nodiscard]] Decl Declare_android_app_Dialog(const Context& context);
[[nodiscard]] Decl Declare_local_IntentSender(const Context& context);
[[nodiscard]] Decl Declare_local_AlarmService(const Context& context);
[[nodiscard]] Decl Declare_android_app_ProgressDialog(const Context& context);
Decl Declare_local_ContentService(const Context&);
Decl Declare_local_ClipboardService(const Context&);
Decl Declare_local_UiModeService(const Context&);
Decl Declare_local_ContentChange(const Context&);
void NotifyLocalContentChange(dx::Interpreter&, const Context&, dx::VmObjectRef uri, dx::VmObjectRef sender,
                              bool wants_self);
[[nodiscard]] Decl Declare_android_database_CursorWindow(const Context& context);
[[nodiscard]] Decl Declare_android_database_sqlite_SQLiteConnection(const Context& context);
[[nodiscard]] Decl Declare_android_database_sqlite_SQLiteConnection_Operation(const Context& context);
[[nodiscard]] Decl Declare_android_database_sqlite_SQLiteDebug(const Context& context);
[[nodiscard]] Decl Declare_android_database_sqlite_SQLiteGlobal(const Context& context);
[[nodiscard]] Decl Declare_android_content_res_AssetManager(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Bitmap(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_BitmapFactory(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_NinePatch(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_BitmapFactory_Options(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Canvas(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_DrawFilter(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_PaintFlagsDrawFilter(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Matrix(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Paint(const Context& context);
[[nodiscard]] std::vector<Decl> Declare_android_graphics_StateObjects(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Path(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Color(const Context& context);
[[nodiscard]] Decl Declare_android_view_GLES20Canvas(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_PathMeasure(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Region(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_RegionIterator(const Context& context);
void RegisterSkiaStateTables(dx::Interpreter& vm, const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Typeface(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_drawable_Drawable(const Context& context);
[[nodiscard]] std::vector<Decl> Declare_android_graphics_Effects(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_Camera(const Context& context);
[[nodiscard]] Decl Declare_android_graphics_drawable_BitmapDrawable(const Context& context);
[[nodiscard]] Decl Declare_local_LocationService(const Context& context);
[[nodiscard]] dx::VmObjectRef LocationManagerForContext(dx::IntrinsicContext& call, const Context& context);
[[nodiscard]] Decl Declare_android_media_AudioSystem(const Context& context);
[[nodiscard]] Decl Declare_local_AudioService(const Context& context);
[[nodiscard]] Decl Declare_android_media_AudioTrack(const Context& context);
[[nodiscard]] Decl Declare_android_media_MediaPlayer(const Context& context);
[[nodiscard]] Decl Declare_android_media_SoundPoolImpl(const Context& context);
[[nodiscard]] Decl Declare_local_WifiService(const Context& context);
[[nodiscard]] Decl Declare_local_ConnectivityService(const Context& context);
[[nodiscard]] dx::VmObjectRef ConnectivityManagerForContext(dx::IntrinsicContext& call, const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLSurfaceView_EGLConfigChooser(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLSurfaceView_EGLContextFactory(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLSurfaceView_Renderer(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLSurfaceView(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLSurfaceView_GLThread(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLES10(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLES10Ext(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLES11(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLES11Ext(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLES20(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLES30(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLUtils(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_GLU(const Context& context);
[[nodiscard]] Decl Declare_android_os_SystemProperties(const Context& context);
[[nodiscard]] Decl Declare_android_os_Binder(const Context& context);
[[nodiscard]] Decl Declare_com_android_internal_os_BinderInternal(const Context& context);
[[nodiscard]] Decl Declare_local_ServiceManager(const Context& context);
[[nodiscard]] Decl Declare_local_MountService(const Context& context);
[[nodiscard]] Decl Declare_android_os_MessageQueue(const Context& context);
[[nodiscard]] Decl Declare_android_os_SystemClock(const Context& context);
[[nodiscard]] Decl Declare_android_os_Parcel(const Context& context);
[[nodiscard]] Decl Declare_local_PowerService(const Context& context);
[[nodiscard]] Decl Declare_local_VibratorService(const Context& context);
[[nodiscard]] Decl Declare_android_os_Process(const Context& context);
[[nodiscard]] Decl Declare_local_SettingsProvider(const Context& context);
[[nodiscard]] Decl Declare_local_SmsService(const Context& context);
[[nodiscard]] Decl Declare_local_PhoneSubscriberService(const Context& context);
[[nodiscard]] Decl Declare_local_TelephonyService(const Context& context);
[[nodiscard]] Decl Declare_local_TelephonyRegistry(const Context& context);
[[nodiscard]] dx::VmObjectRef TelephonyManagerForContext(dx::IntrinsicContext& call, const Context& context);
[[nodiscard]] Decl Declare_android_text_Editable(const Context& context);
[[nodiscard]] Decl Declare_android_text_EditableImpl(const Context& context);
[[nodiscard]] Decl Declare_android_text_AndroidBidi(const Context& context);
Decl Declare_android_text_TextPaint(const Context& context);
[[nodiscard]] Decl Declare_android_text_TextWatcher(const Context& context);
[[nodiscard]] Decl Declare_android_text_format_Time(const Context& context);
[[nodiscard]] Decl Declare_android_util_Log(const Context& context);
[[nodiscard]] Decl Declare_android_util_EventLog(const Context& context);
[[nodiscard]] Decl Declare_android_util_AttributeSet(const Context& context);
[[nodiscard]] Decl Declare_android_util_FloatMath(const Context& context);
[[nodiscard]] Decl Declare_android_hardware_display_DisplayManager(const Context& context);
[[nodiscard]] Decl Declare_android_view_ContextThemeWrapper(const Context& context);
[[nodiscard]] Decl Declare_android_view_KeyEvent(const Context& context);
[[nodiscard]] Decl Declare_android_view_KeyCharacterMap(const Context& context);
[[nodiscard]] Decl Declare_android_view_InputDevice(const Context& context);
[[nodiscard]] Decl Declare_android_view_MotionEvent(const Context& context);
[[nodiscard]] Decl Declare_android_view_SurfaceHolder_Callback(const Context& context);
[[nodiscard]] Decl Declare_android_view_SurfaceHolder_Impl(const Context& context);
[[nodiscard]] Decl Declare_android_view_SurfaceHolder(const Context& context);
[[nodiscard]] Decl Declare_android_view_SurfaceView(const Context& context);
[[nodiscard]] Decl Declare_android_view_View_OnClickListener(const Context& context);
[[nodiscard]] Decl Declare_android_view_View_OnFocusChangeListener(const Context& context);
[[nodiscard]] Decl Declare_android_view_View_OnSystemUiVisibilityChangeListener(const Context& context);
[[nodiscard]] Decl Declare_android_view_View_OnTouchListener(const Context& context);
[[nodiscard]] Decl Declare_android_view_ViewParent(const Context& context);
[[nodiscard]] Decl Declare_android_view_View(const Context& context);
[[nodiscard]] Decl Declare_android_view_ViewGroup(const Context& context);
[[nodiscard]] Decl Declare_android_view_ViewTreeObserver_OnGlobalLayoutListener(const Context& context);
[[nodiscard]] Decl Declare_android_view_ViewTreeObserver(const Context& context);
[[nodiscard]] Decl Declare_android_view_Window(const Context& context);
[[nodiscard]] Decl Declare_android_view_WindowManager_BadTokenException(const Context& context);
[[nodiscard]] Decl Declare_android_view_WindowManager_LayoutParams(const Context& context);
[[nodiscard]] Decl Declare_android_view_WindowManager(const Context& context);
[[nodiscard]] Decl Declare_android_view_WindowManagerImpl(const Context& context);
[[nodiscard]] Decl Declare_android_view_inputmethod_InputMethodManager(const Context& context);
[[nodiscard]] Decl Declare_android_webkit_WebChromeClient(const Context& context);
[[nodiscard]] Decl Declare_android_webkit_WebChromeClient_CustomViewCallback(const Context& context);
[[nodiscard]] Decl Declare_android_webkit_ConsoleMessage(const Context& context);
[[nodiscard]] Decl Declare_android_webkit_HttpAuthHandler(const Context& context);
[[nodiscard]] Decl Declare_android_webkit_WebSettings(const Context& context);
[[nodiscard]] Decl Declare_android_webkit_WebSettings_RenderPriority(const Context& context);
[[nodiscard]] Decl Declare_android_webkit_WebView(const Context& context);
[[nodiscard]] Decl Declare_android_webkit_WebView_DisabledLoadCallback(const Context& context);
[[nodiscard]] Decl Declare_android_webkit_WebViewClient(const Context& context);
[[nodiscard]] Decl Declare_android_widget_Button(const Context& context);
[[nodiscard]] Decl Declare_android_widget_EditText(const Context& context);
[[nodiscard]] Decl Declare_android_widget_FrameLayout(const Context& context);
[[nodiscard]] Decl Declare_android_widget_ImageButton(const Context& context);
[[nodiscard]] Decl Declare_android_widget_ImageView_ScaleType(const Context& context);
[[nodiscard]] Decl Declare_android_widget_ImageView(const Context& context);
[[nodiscard]] Decl Declare_android_widget_LinearLayout(const Context& context);
[[nodiscard]] Decl Declare_android_widget_ProgressBar(const Context& context);
[[nodiscard]] Decl Declare_android_widget_RelativeLayout(const Context& context);
[[nodiscard]] Decl Declare_android_widget_EdgeEffect(const Context& context);
[[nodiscard]] Decl Declare_android_widget_HorizontalScrollView(const Context& context);
[[nodiscard]] Decl Declare_android_widget_ScrollView(const Context& context);
[[nodiscard]] Decl Declare_android_widget_TableLayout(const Context& context);
[[nodiscard]] Decl Declare_android_widget_TableRow(const Context& context);
[[nodiscard]] Decl Declare_android_widget_TextView(const Context& context);
[[nodiscard]] Decl Declare_android_widget_Toast(const Context& context);
[[nodiscard]] Decl Declare_android_widget_VideoView(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_EGL14(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_EGLObjectHandle(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_EGLConfig(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_EGLContext(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_EGLDisplay(const Context& context);
[[nodiscard]] Decl Declare_android_opengl_EGLSurface(const Context& context);
[[nodiscard]] Decl Declare_javax_microedition_khronos_egl_EGLConfig(const Context& context);
[[nodiscard]] Decl Declare_javax_microedition_khronos_egl_EGL(const Context& context);
[[nodiscard]] Decl Declare_javax_microedition_khronos_egl_EGL10(const Context& context);
[[nodiscard]] Decl Declare_javax_microedition_khronos_egl_EGL10_Impl(const Context& context);
[[nodiscard]] Decl Declare_javax_microedition_khronos_egl_EGLContext(const Context& context);
[[nodiscard]] Decl Declare_javax_microedition_khronos_egl_EGLDisplay(const Context& context);
[[nodiscard]] Decl Declare_javax_microedition_khronos_egl_EGLSurface(const Context& context);
[[nodiscard]] Decl Declare_com_google_android_gles_jni_GLImpl(const Context& context);
[[nodiscard]] Decl Declare_android_hardware_SystemSensorManager(const Context& context);
[[nodiscard]] dx::VmObjectRef SensorManagerForContext(dx::IntrinsicContext& call, const Context& context);
[[nodiscard]] Decl Declare_local_UsbService(const Context& context);
[[nodiscard]] dx::VmObjectRef UsbManagerForContext(dx::IntrinsicContext& call, const Context& context);
[[nodiscard]] dx::VmObjectRef NfcManagerForContext(dx::IntrinsicContext& call, const Context& context);

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_SurfaceTexture(const Context&);
}

namespace ogplay::runtime::android_intrinsics {
bool ProjectBitmapDrawable(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef drawable);
void ProjectAndroidImage(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef view);
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
[[nodiscard]] Decl Declare_android_graphics_drawable_StateListDrawable(const Context&);
[[nodiscard]] Decl Declare_android_graphics_drawable_DrawableContainer(const Context&);
[[nodiscard]] Decl Declare_android_graphics_drawable_NinePatchDrawable(const Context&);
[[nodiscard]] Decl Declare_managed_WindowToken(const Context&);
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
[[nodiscard]] Decl Declare_android_widget_Spinner(const Context& context);
[[nodiscard]] Decl Declare_android_widget_Spinner_DropdownPopup(const Context& context);
[[nodiscard]] Decl Declare_android_widget_Spinner_ForwardingListener(const Context& context);
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
[[nodiscard]] Decl Declare_android_graphics_drawable_AnimationDrawable(const Context& context);
}

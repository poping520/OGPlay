#include "catalog.h"

namespace ogplay::runtime {

dexvm::CoreIntrinsicServices AndroidCoreIntrinsicServices(
    const std::shared_ptr<DexVmAndroidContext>& context) {
    dexvm::CoreIntrinsicServices services;
    if (context == nullptr) return services;
    services.iso3_language = context->iso3_language;
    services.iso3_country = context->iso3_country;
    services.singleton =
        [context](dexvm::Interpreter& vm, const std::string_view key,
                  const std::string_view descriptor) {
            const auto found = context->singletons.find(std::string(key));
            if (found != context->singletons.end()) return found->second;
            const auto instance =
                vm.NewIntrinsicInstance(std::string(descriptor));
            context->singletons.emplace(std::string(key), instance);
            return instance;
        };
    services.schedule_timer_task =
        [context](dexvm::Interpreter& vm, const dexvm::VmObjectRef timer,
                  const dexvm::VmObjectRef task, const std::int64_t delay,
                  const std::int64_t period, const bool fixed_rate) {
            android_intrinsics::ScheduleTimerTask(
                vm, context, timer, task, delay, period, fixed_rate);
        };
    services.cancel_timer = [context](const dexvm::VmObjectRef timer) {
        android_intrinsics::CancelTimer(context, timer);
    };
    services.cancel_timer_task = [context](const dexvm::VmObjectRef task) {
        return android_intrinsics::CancelTimerTask(context, task);
    };
    services.timer_task_scheduled_execution_time =
        [context](const dexvm::VmObjectRef task) {
            return android_intrinsics::TimerTaskScheduledExecutionTime(
                context, task);
    };
    services.set_sax_content_handler =
        [context](const dexvm::VmObjectRef reader,
                  const dexvm::VmObjectRef handler) {
            context->sax_content_handlers[reader.Value()] = handler;
        };
    services.current_time_millis = [context] {
        return 1'400'000'000'000LL +
               context->uptime_millis.load(std::memory_order_relaxed);
    };
    return services;
}

std::vector<dexvm::IntrinsicClassDecl> AndroidIntrinsicCatalog(
    const std::shared_ptr<DexVmAndroidContext>& context) {
    using namespace android_intrinsics;
    return {
        Declare_android_app_Activity(context),
        Declare_android_app_AlertDialog_Builder(context),
        Declare_android_app_AlertDialog(context),
        Declare_android_app_Dialog(context),
        Declare_android_app_IntentService(context),
        Declare_android_app_Service(context),
        Declare_android_app_PendingIntent(context),
        Declare_android_app_ProgressDialog(context),
        Declare_android_content_BroadcastReceiver(context),
        Declare_android_content_ContentResolver(context),
        Declare_android_content_ContentValues(context),
        Declare_android_content_Context(context),
        Declare_android_content_ContextWrapper(context),
        Declare_android_content_DialogInterface_OnCancelListener(context),
        Declare_android_content_DialogInterface_OnClickListener(context),
        Declare_android_content_DialogInterface_OnDismissListener(context),
        Declare_android_content_Intent(context),
        Declare_android_content_IntentFilter(context),
        Declare_android_content_pm_PackageItemInfo(context),
        Declare_android_content_pm_ApplicationInfo(context),
        Declare_android_content_pm_PackageInfo(context),
        Declare_android_content_pm_PackageManager_NameNotFoundException(context),
        Declare_android_content_pm_PackageManager(context),
        Declare_android_content_SharedPreferences_Editor(context),
        Declare_android_content_SharedPreferences(context),
        Declare_android_content_SharedPreferencesEditorImpl(context),
        Declare_android_content_SharedPreferencesImpl(context),
        Declare_android_content_res_AssetFileDescriptor(context),
        Declare_android_content_res_AssetManager(context),
        Declare_android_content_res_Configuration(context),
        Declare_android_content_res_Resources(context),
        Declare_android_database_Cursor(context),
        Declare_android_database_CursorImpl(context),
        Declare_android_database_sqlite_SQLiteDatabase(context),
        Declare_android_database_sqlite_SQLiteDatabase_CursorFactory(context),
        Declare_android_database_sqlite_SQLiteOpenHelper(context),
        Declare_android_database_SQLiteException(context),
        Declare_android_graphics_Bitmap_Config(context),
        Declare_android_graphics_Bitmap(context),
        Declare_android_graphics_BitmapFactory(context),
        Declare_android_graphics_Canvas(context),
        Declare_android_graphics_Matrix(context),
        Declare_android_graphics_Paint(context),
        Declare_android_graphics_Rect(context),
        Declare_android_graphics_RectF(context),
        Declare_android_graphics_Point(context),
        Declare_android_graphics_PointF(context),
        Declare_android_graphics_Path_Direction(context),
        Declare_android_graphics_Path(context),
        Declare_android_graphics_Color(context),
        Declare_android_graphics_PorterDuff(context),
        Declare_android_graphics_PorterDuff_Mode(context),
        Declare_android_graphics_Region_Op(context),
        Declare_android_graphics_Typeface(context),
        Declare_android_graphics_drawable_Drawable(context),
        Declare_android_graphics_drawable_PaintDrawable(context),
        Declare_android_graphics_drawable_BitmapDrawable(context),
        Declare_android_graphics_drawable_ColorDrawable(context),
        Declare_android_hardware_Sensor(context),
        Declare_android_hardware_SensorEvent(context),
        Declare_android_hardware_SensorEventListener(context),
        Declare_android_hardware_SensorManager(context),
        Declare_android_media_AudioManager(context),
        Declare_android_media_AudioFormat(context),
        Declare_android_media_AudioTrack(context),
        Declare_android_media_AudioTrack_OnPlaybackPositionUpdateListener(context),
        Declare_android_media_MediaPlayer_OnCompletionListener(context),
        Declare_android_media_MediaPlayer_OnErrorListener(context),
        Declare_android_media_MediaPlayer_OnPreparedListener(context),
        Declare_android_media_MediaPlayer(context),
        Declare_android_media_SoundPool(context),
        Declare_android_net_ConnectivityManager(context),
        Declare_android_net_NetworkInfo_State(context),
        Declare_android_net_NetworkInfo(context),
        Declare_android_net_Uri(context),
        Declare_android_net_wifi_WifiInfo(context),
        Declare_android_net_wifi_WifiManager_WifiLock(context),
        Declare_android_net_wifi_WifiManager(context),
        Declare_android_opengl_GLSurfaceView_EGLConfigChooser(context),
        Declare_android_opengl_GLSurfaceView_EGLContextFactory(context),
        Declare_android_opengl_GLSurfaceView_Renderer(context),
        Declare_android_opengl_GLSurfaceView(context),
        Declare_android_opengl_GLES10(context),
        Declare_android_opengl_GLES10Ext(context),
        Declare_android_opengl_GLES11(context),
        Declare_android_opengl_GLES11Ext(context),
        Declare_android_opengl_GLES20(context),
        Declare_android_opengl_GLUtils(context),
        Declare_android_opengl_GLU(context),
        Declare_android_os_AsyncTask(context),
        Declare_android_os_AsyncTask_Status(context),
        Declare_android_os_AsyncTask_Worker(context),
        Declare_android_os_Build_VERSION(context),
        Declare_android_os_Build(context),
        Declare_android_os_Bundle(context),
        Declare_android_os_Bundle_1(context),
        Declare_android_os_CountDownTimer(context),
        Declare_android_os_Environment(context),
        Declare_android_os_Handler(context),
        Declare_android_os_Handler_Callback(context),
        Declare_android_os_HandlerThread(context),
        Declare_android_os_IBinder(context),
        Declare_android_os_Looper(context),
        Declare_android_os_Message(context),
        Declare_android_os_SystemClock(context),
        Declare_android_os_StatFs(context),
        Declare_android_os_Parcelable(context),
        Declare_android_os_Parcelable_Creator(context),
        Declare_android_os_Parcel(context),
        Declare_android_os_PowerManager(context),
        Declare_android_os_PowerManager_WakeLock(context),
        Declare_android_os_Vibrator(context),
        Declare_android_os_Process(context),
        Declare_android_provider_Settings_System(context),
        Declare_android_telephony_PhoneStateListener(context),
        Declare_android_telephony_SmsManager(context),
        Declare_android_telephony_SmsMessage(context),
        Declare_android_telephony_TelephonyManager(context),
        Declare_android_text_Editable(context),
        Declare_android_text_EditableImpl(context),
        Declare_android_text_TextPaint(context),
        Declare_android_text_TextWatcher(context),
        Declare_android_text_TextUtils(context),
        Declare_android_util_Log(context),
        Declare_android_util_Pair(context),
        Declare_android_util_Base64(context),
        Declare_android_util_SparseArray(context),
        Declare_android_util_SparseBooleanArray(context),
        Declare_android_util_SparseIntArray(context),
        Declare_android_util_TypedValue(context),
        Declare_android_util_DisplayMetrics(context),
        Declare_android_view_Display(context),
        Declare_android_view_ContextThemeWrapper(context),
        Declare_android_view_KeyEvent(context),
        Declare_android_view_MotionEvent(context),
        Declare_android_view_SurfaceHolder_Callback(context),
        Declare_android_view_SurfaceHolder_Impl(context),
        Declare_android_view_SurfaceHolder(context),
        Declare_android_view_SurfaceView(context),
        Declare_android_view_View_OnClickListener(context),
        Declare_android_view_View_OnTouchListener(context),
        Declare_android_view_View(context),
        Declare_android_view_ViewGroup_LayoutParams(context),
        Declare_android_view_ViewGroup(context),
        Declare_android_view_ViewTreeObserver_OnGlobalLayoutListener(context),
        Declare_android_view_ViewTreeObserver(context),
        Declare_android_view_Window(context),
        Declare_android_view_WindowManager_LayoutParams(context),
        Declare_android_view_WindowManager(context),
        Declare_android_view_WindowManagerImpl(context),
        Declare_android_view_inputmethod_InputMethodManager(context),
        Declare_android_webkit_WebChromeClient(context),
        Declare_android_webkit_WebSettings(context),
        Declare_android_webkit_WebView(context),
        Declare_android_webkit_WebViewClient(context),
        Declare_android_widget_AbsoluteLayout_LayoutParams(context),
        Declare_android_widget_AbsoluteLayout(context),
        Declare_android_widget_Button(context),
        Declare_android_widget_EditText(context),
        Declare_android_widget_FrameLayout_LayoutParams(context),
        Declare_android_widget_FrameLayout(context),
        Declare_android_widget_ImageButton(context),
        Declare_android_widget_ImageView_ScaleType(context),
        Declare_android_widget_ImageView(context),
        Declare_android_widget_LinearLayout_LayoutParams(context),
        Declare_android_widget_LinearLayout(context),
        Declare_android_widget_ProgressBar(context),
        Declare_android_widget_RelativeLayout_LayoutParams(context),
        Declare_android_widget_RelativeLayout(context),
        Declare_android_widget_ScrollView(context),
        Declare_android_widget_TableLayout(context),
        Declare_android_widget_TableRow(context),
        Declare_android_widget_TextView(context),
        Declare_android_widget_Toast(context),
        Declare_android_widget_VideoView(context),
        Declare_javax_microedition_khronos_egl_EGLConfig(context),
        Declare_javax_microedition_khronos_opengles_GL10(context),
        // Historical compatibility tail. DVM-61 decouples Java identity hash
        // from linker ids and object handles, so future catalog insertion no
        // longer changes Object.hashCode/default toString identity.
        Declare_javax_microedition_khronos_egl_EGL(context),
        Declare_javax_microedition_khronos_egl_EGL10(context),
        Declare_javax_microedition_khronos_egl_EGL10_Impl(context),
        Declare_javax_microedition_khronos_egl_EGLContext(context),
        Declare_javax_microedition_khronos_egl_EGLDisplay(context),
        Declare_javax_microedition_khronos_egl_EGLSurface(context),
        Declare_javax_microedition_khronos_opengles_GL(context),
        Declare_javax_microedition_khronos_opengles_GL10_Impl(context),
        Declare_android_app_Application(context),
    };
}

}  // namespace ogplay::runtime

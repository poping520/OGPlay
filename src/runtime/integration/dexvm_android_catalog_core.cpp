// Catalog batch: Context/Activity/Window/View/GLSurfaceView/Resources —
// the classes the dex_activity lifecycle itself drives.

#include "dexvm_android_internal.h"

namespace ogplay::runtime::android_intrinsics {

void AppendCoreClasses(std::vector<Decl>& catalog) {
    {
        Decl context;
        context.descriptor = "Landroid/content/Context;";
        context.superclass = "Ljava/lang/Object;";
        context.methods = {
            {"<init>", "()V", false, false, "android.context.init"},
            {"getAssets", "()Landroid/content/res/AssetManager;", false,
             false, "android.context.get_assets"},
            {"getPackageName", "()Ljava/lang/String;", false, false,
             "android.context.get_package_name"},
            {"getResources", "()Landroid/content/res/Resources;", false,
             false, "android.context.get_resources"},
            {"getSystemService",
             "(Ljava/lang/String;)Ljava/lang/Object;", false, false,
             "android.context.get_system_service"},
            {"registerReceiver",
             "(Landroid/content/BroadcastReceiver;"
             "Landroid/content/IntentFilter;)Landroid/content/Intent;",
             false, false, "android.context.register_receiver"},
            {"startActivity", "(Landroid/content/Intent;)V", false, false,
             "android.context.start_activity"},
            {"getSharedPreferences",
             "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
             false, false, "android.context.get_shared_preferences"},
            {"getContentResolver",
             "()Landroid/content/ContentResolver;", false, false,
             "android.context.get_content_resolver"},
            // No receivers beyond the session's own exist; broadcasts have
            // no audience and truthfully go nowhere.
            {"sendBroadcast", "(Landroid/content/Intent;)V", false, false,
             "android.context.send_broadcast"},
            {"getExternalFilesDir",
             "(Ljava/lang/String;)Ljava/io/File;", false, false,
             "android.context.get_external_files_dir"},
            // No service infrastructure exists on this platform; the
            // documented "service not found" answer is null.
            {"startService",
             "(Landroid/content/Intent;)Landroid/content/ComponentName;",
             false, false, "android.context.start_service_none"},
        };
        catalog.push_back(std::move(context));
        Decl resolver;
        resolver.descriptor = "Landroid/content/ContentResolver;";
        resolver.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(resolver));
        // Real value pair: the fields are read directly by title code.
        Decl pair;
        pair.descriptor = "Landroid/util/Pair;";
        pair.superclass = "Ljava/lang/Object;";
        pair.fields = {
            {"first", "Ljava/lang/Object;", false, false, 0, ""},
            {"second", "Ljava/lang/Object;", false, false, 0, ""},
        };
        pair.methods = {
            {"<init>", "(Ljava/lang/Object;Ljava/lang/Object;)V", false,
             false, "android.pair.init"},
        };
        catalog.push_back(std::move(pair));
    }
    {
        // SharedPreferences with real session-lifetime typed storage
        // (memory_files v1 semantics). Interface + Impl pairing mirrors
        // WindowManager so invoke-interface dispatch works.
        Decl prefs_interface;
        prefs_interface.descriptor = "Landroid/content/SharedPreferences;";
        prefs_interface.is_interface = true;
        prefs_interface.methods = {
            {"edit", "()Landroid/content/SharedPreferences$Editor;", false,
             false, "android.prefs.edit"},
            {"getBoolean", "(Ljava/lang/String;Z)Z", false, false,
             "android.prefs.get_boolean"},
            {"getInt", "(Ljava/lang/String;I)I", false, false,
             "android.prefs.get_int"},
            {"getLong", "(Ljava/lang/String;J)J", false, false,
             "android.prefs.get_long"},
            {"getString",
             "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
             false, false, "android.prefs.get_string"},
        };
        Decl prefs_impl;
        prefs_impl.descriptor = "Landroid/content/SharedPreferencesImpl;";
        prefs_impl.superclass = "Ljava/lang/Object;";
        prefs_impl.interfaces = {"Landroid/content/SharedPreferences;"};
        prefs_impl.methods = prefs_interface.methods;
        catalog.push_back(std::move(prefs_interface));
        catalog.push_back(std::move(prefs_impl));
        Decl editor_interface;
        editor_interface.descriptor =
            "Landroid/content/SharedPreferences$Editor;";
        editor_interface.is_interface = true;
        editor_interface.methods = {
            {"putBoolean",
             "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;",
             false, false, "android.prefs_editor.put_boolean"},
            {"putInt",
             "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;",
             false, false, "android.prefs_editor.put_int"},
            {"putLong",
             "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;",
             false, false, "android.prefs_editor.put_long"},
            {"putString",
             "(Ljava/lang/String;Ljava/lang/String;)"
             "Landroid/content/SharedPreferences$Editor;",
             false, false, "android.prefs_editor.put_string"},
            {"commit", "()Z", false, false, "android.prefs_editor.commit"},
        };
        Decl editor_impl;
        editor_impl.descriptor =
            "Landroid/content/SharedPreferencesEditorImpl;";
        editor_impl.superclass = "Ljava/lang/Object;";
        editor_impl.interfaces = {
            "Landroid/content/SharedPreferences$Editor;"};
        editor_impl.methods = editor_interface.methods;
        catalog.push_back(std::move(editor_interface));
        catalog.push_back(std::move(editor_impl));
    }
    {
        Decl activity;
        activity.descriptor = "Landroid/app/Activity;";
        activity.superclass = "Landroid/content/Context;";
        activity.methods = {
            {"<init>", "()V", false, false, "android.activity.init"},
            {"onCreate", "(Landroid/os/Bundle;)V", false, true,
             "android.activity.lifecycle_noop"},
            {"onStart", "()V", false, true,
             "android.activity.lifecycle_noop"},
            {"onRestart", "()V", false, true,
             "android.activity.lifecycle_noop"},
            {"onResume", "()V", false, true,
             "android.activity.lifecycle_noop"},
            {"onPause", "()V", false, true,
             "android.activity.lifecycle_noop"},
            {"onStop", "()V", false, true,
             "android.activity.lifecycle_noop"},
            {"onDestroy", "()V", false, true,
             "android.activity.lifecycle_noop"},
            {"onConfigurationChanged",
             "(Landroid/content/res/Configuration;)V", false, true,
             "android.activity.lifecycle_noop"},
            {"getWindow", "()Landroid/view/Window;", false, false,
             "android.activity.get_window"},
            {"requestWindowFeature", "(I)Z", false, false,
             "android.activity.request_window_feature"},
            {"setContentView", "(Landroid/view/View;)V", false, false,
             "android.activity.set_content_view"},
            // XML layout inflation is not provided: the id form records
            // the request and findViewById answers null (documented value
            // for an absent view), so callers see a consistent world.
            {"setContentView", "(I)V", false, false,
             "android.activity.set_content_view_id"},
            {"findViewById", "(I)Landroid/view/View;", false, false,
             "android.activity.find_view_by_id"},
            {"getIntent", "()Landroid/content/Intent;", false, false,
             "android.activity.get_intent"},
            {"runOnUiThread", "(Ljava/lang/Runnable;)V", false, false,
             "android.activity.run_on_ui_thread"},
            {"setVolumeControlStream", "(I)V", false, false,
             "android.activity.set_volume_control_stream"},
            {"onKeyDown", "(ILandroid/view/KeyEvent;)Z", false, true,
             "android.activity.on_key_false"},
            {"onKeyUp", "(ILandroid/view/KeyEvent;)Z", false, true,
             "android.activity.on_key_false"},
            {"onTouchEvent", "(Landroid/view/MotionEvent;)Z", false, true,
             "android.activity.on_touch_false"},
            // Overridable: Activity.finish is a plain virtual on Android
            // and title subclasses commonly wrap it (super.finish() still
            // reaches the intrinsic).
            {"finish", "()V", false, true, "android.activity.finish"},
            {"getWindowManager", "()Landroid/view/WindowManager;", false,
             false, "android.activity.get_window_manager"},
        };
        catalog.push_back(std::move(activity));
    }
    {
        // Display geometry is a real platform fact (the profile surface).
        Decl window_manager;
        window_manager.descriptor = "Landroid/view/WindowManager;";
        window_manager.is_interface = true;
        window_manager.methods = {
            {"getDefaultDisplay", "()Landroid/view/Display;", false, false,
             "android.windowmanager.get_default_display"},
        };
        catalog.push_back(std::move(window_manager));
        Decl window_manager_impl;
        window_manager_impl.descriptor = "Landroid/view/WindowManagerImpl;";
        window_manager_impl.superclass = "Ljava/lang/Object;";
        window_manager_impl.interfaces = {"Landroid/view/WindowManager;"};
        window_manager_impl.methods = {
            {"getDefaultDisplay", "()Landroid/view/Display;", false, false,
             "android.windowmanager.get_default_display"},
        };
        catalog.push_back(std::move(window_manager_impl));
        Decl display;
        display.descriptor = "Landroid/view/Display;";
        display.superclass = "Ljava/lang/Object;";
        display.methods = {
            {"getWidth", "()I", false, false, "android.display.get_width"},
            {"getHeight", "()I", false, false,
             "android.display.get_height"},
        };
        catalog.push_back(std::move(display));
    }
    {
        Decl window;
        window.descriptor = "Landroid/view/Window;";
        window.superclass = "Ljava/lang/Object;";
        window.methods = {
            {"setFlags", "(II)V", false, false, "android.window.noop"},
            {"addFlags", "(I)V", false, false, "android.window.noop_add"},
            {"clearFlags", "(I)V", false, false,
             "android.window.noop_clear"},
            {"getAttributes",
             "()Landroid/view/WindowManager$LayoutParams;", false, false,
             "android.window.get_attributes"},
        };
        catalog.push_back(std::move(window));
        // Attribute holder: the title only passes it around (no fields are
        // referenced per the gap report); the instance is a singleton.
        Decl layout_params;
        layout_params.descriptor =
            "Landroid/view/WindowManager$LayoutParams;";
        layout_params.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(layout_params));
        Decl absolute_params;
        absolute_params.descriptor =
            "Landroid/widget/AbsoluteLayout$LayoutParams;";
        absolute_params.superclass = "Ljava/lang/Object;";
        absolute_params.methods = {
            {"<init>", "(IIII)V", false, false, "android.graphics.noop"},
        };
        catalog.push_back(std::move(absolute_params));
    }
    {
        Decl view;
        view.descriptor = "Landroid/view/View;";
        view.superclass = "Ljava/lang/Object;";
        view.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
            {"onSizeChanged", "(IIII)V", false, true,
             "android.view.noop_size"},
            {"onWindowFocusChanged", "(Z)V", false, true,
             "android.view.noop_focus"},
            // Focus flags: the dex_activity lifecycle drives a single
            // fullscreen view, focus state is implicit; flag setters are
            // truthful no-ops and requestFocus reports success.
            {"setFocusable", "(Z)V", false, false,
             "android.view.noop_flag"},
            {"setFocusableInTouchMode", "(Z)V", false, false,
             "android.view.noop_flag"},
            {"requestFocus", "()Z", false, false,
             "android.view.request_focus"},
            // Canvas invalidation has no consumer yet (installer views
            // draw nothing); ids default to NO_ID (-1).
            {"invalidate", "()V", false, false, "android.view.noop_flag"},
            {"postInvalidate", "()V", false, false,
             "android.view.noop_flag"},
            {"getId", "()I", false, false, "android.view.get_id"},
            // Visibility and click listeners are real state consumed by
            // the widget click dispatch; background drawing stays a no-op
            // (the GL surface / video frame is the visual output).
            {"setVisibility", "(I)V", false, false,
             "android.view.set_visibility"},
            {"getVisibility", "()I", false, false,
             "android.view.get_visibility"},
            {"setBackgroundColor", "(I)V", false, false,
             "android.widget.noop"},
            {"setBackgroundResource", "(I)V", false, false,
             "android.widget.noop"},
            {"setBackgroundDrawable",
             "(Landroid/graphics/drawable/Drawable;)V", false, false,
             "android.widget.noop"},
            {"setOnClickListener",
             "(Landroid/view/View$OnClickListener;)V", false, false,
             "android.view.set_on_click_listener"},
            {"setOnTouchListener",
             "(Landroid/view/View$OnTouchListener;)V", false, false,
             "android.widget.noop"},
            {"clearFocus", "()V", false, false, "android.widget.noop"},
            {"getWindowToken", "()Landroid/os/IBinder;", false, false,
             "android.widget.null"},
        };
        catalog.push_back(std::move(view));
    }
    {
        Decl renderer_interface;
        renderer_interface.descriptor =
            "Landroid/opengl/GLSurfaceView$Renderer;";
        renderer_interface.is_interface = true;
        catalog.push_back(std::move(renderer_interface));
        Decl gl10;
        gl10.descriptor = "Ljavax/microedition/khronos/opengles/GL10;";
        gl10.is_interface = true;
        catalog.push_back(std::move(gl10));
        Decl egl_config;
        egl_config.descriptor =
            "Ljavax/microedition/khronos/egl/EGLConfig;";
        egl_config.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(egl_config));
    }
    {
        Decl surface;
        surface.descriptor = "Landroid/opengl/GLSurfaceView;";
        surface.superclass = "Landroid/view/View;";
        surface.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.glsurfaceview.init"},
            {"setRenderer",
             "(Landroid/opengl/GLSurfaceView$Renderer;)V", false, false,
             "android.glsurfaceview.set_renderer"},
            {"requestRender", "()V", false, false,
             "android.glsurfaceview.request_render"},
            // Render pause/resume is owned by the lifecycle driver.
            {"onPause", "()V", false, false,
             "android.glsurfaceview.lifecycle_noop"},
            {"onResume", "()V", false, false,
             "android.glsurfaceview.lifecycle_noop"},
        };
        catalog.push_back(std::move(surface));
    }
    {
        Decl resources;
        resources.descriptor = "Landroid/content/res/Resources;";
        resources.superclass = "Ljava/lang/Object;";
        resources.methods = {
            {"getConfiguration",
             "()Landroid/content/res/Configuration;", false, false,
             "android.resources.get_configuration"},
            {"getIdentifier",
             "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
             false, false, "android.resources.get_identifier"},
            {"openRawResource", "(I)Ljava/io/InputStream;", false, false,
             "android.resources.open_raw_resource"},
            {"getString", "(I)Ljava/lang/String;", false, false,
             "android.resources.get_string"},
        };
        catalog.push_back(std::move(resources));
    }
    {
        Decl configuration;
        configuration.descriptor = "Landroid/content/res/Configuration;";
        configuration.superclass = "Ljava/lang/Object;";
        configuration.fields = {{"keyboard", "I", false, false, 0, ""}};
        catalog.push_back(std::move(configuration));
    }
    {
        Decl assets;
        assets.descriptor = "Landroid/content/res/AssetManager;";
        assets.superclass = "Ljava/lang/Object;";
        assets.methods = {
            {"open", "(Ljava/lang/String;)Ljava/io/InputStream;", false,
             false, "android.assets.open"},
            {"open", "(Ljava/lang/String;I)Ljava/io/InputStream;", false,
             false, "android.assets.open_mode"},
        };
        catalog.push_back(std::move(assets));
    }
}

}  // namespace ogplay::runtime::android_intrinsics

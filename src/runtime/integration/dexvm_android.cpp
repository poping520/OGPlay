// android.* intrinsic catalog for the dex_activity lifecycle. Calibrated by
// the pilot title's static reference survey (WU-M9-002): every class/method
// here is actually referenced by the interpreted glue; anything outside the
// list stays an explicit, accounted failure.

#include "ogplay/runtime/integration/dexvm_android.h"

namespace ogplay::runtime {

std::vector<dexvm::IntrinsicClassDecl> AndroidIntrinsicCatalog() {
    using Decl = dexvm::IntrinsicClassDecl;
    std::vector<Decl> catalog;

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
        };
        catalog.push_back(std::move(context));
        Decl resolver;
        resolver.descriptor = "Landroid/content/ContentResolver;";
        resolver.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(resolver));
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
            {"finish", "()V", false, false, "android.activity.finish"},
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
            // Presentation state consumed by nothing (the GL surface is
            // the only visual output): truthful no-ops / neutral answers.
            {"setVisibility", "(I)V", false, false, "android.widget.noop"},
            {"getVisibility", "()I", false, false, "android.widget.zero"},
            {"setBackgroundColor", "(I)V", false, false,
             "android.widget.noop"},
            {"setBackgroundResource", "(I)V", false, false,
             "android.widget.noop"},
            {"setBackgroundDrawable",
             "(Landroid/graphics/drawable/Drawable;)V", false, false,
             "android.widget.noop"},
            {"setOnClickListener",
             "(Landroid/view/View$OnClickListener;)V", false, false,
             "android.widget.noop"},
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
    {
        Decl stream;
        stream.descriptor = "Ljava/io/InputStream;";
        stream.superclass = "Ljava/lang/Object;";
        stream.methods = {
            {"read", "([BII)I", false, false, "android.stream.read_range"},
            {"read", "([B)I", false, false, "android.stream.read_full"},
            {"read", "()I", false, false, "android.stream.read_one"},
            {"available", "()I", false, false, "android.stream.available"},
            {"close", "()V", false, false, "android.stream.close"},
            {"skip", "(J)J", false, false, "android.stream.skip"},
        };
        catalog.push_back(std::move(stream));
    }
    {
        Decl file;
        file.descriptor = "Ljava/io/File;";
        file.superclass = "Ljava/lang/Object;";
        file.fields = {{"path", "Ljava/lang/String;", false, false, 0, ""}};
        file.methods = {
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.file.init"},
            {"<init>", "(Ljava/lang/String;Ljava/lang/String;)V", false,
             false, "android.file.init_parent_child"},
            {"exists", "()Z", false, false, "android.file.exists"},
            {"length", "()J", false, false, "android.file.length"},
            {"getPath", "()Ljava/lang/String;", false, false,
             "android.file.get_path"},
            {"getAbsolutePath", "()Ljava/lang/String;", false, false,
             "android.file.get_path"},
            // Directories are implicit in the guest VFS, so creating them
            // trivially succeeds; delete only reaches the session overlay
            // (read-only mounts truthfully report failure).
            {"mkdir", "()Z", false, false, "android.file.mkdirs"},
            {"mkdirs", "()Z", false, false, "android.file.mkdirs"},
            {"createNewFile", "()Z", false, false,
             "android.file.create_new"},
            {"delete", "()Z", false, false, "android.file.delete"},
        };
        catalog.push_back(std::move(file));
        Decl file_input;
        file_input.descriptor = "Ljava/io/FileInputStream;";
        file_input.superclass = "Ljava/io/InputStream;";
        file_input.methods = {
            {"<init>", "(Ljava/io/File;)V", false, false,
             "android.file_stream.init_file"},
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.file_stream.init_path"},
        };
        catalog.push_back(std::move(file_input));
        Decl output;
        output.descriptor = "Ljava/io/OutputStream;";
        output.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(output));
        Decl file_output;
        file_output.descriptor = "Ljava/io/FileOutputStream;";
        file_output.superclass = "Ljava/io/OutputStream;";
        file_output.methods = {
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.file_output.init_path"},
            {"<init>", "(Ljava/io/File;)V", false, false,
             "android.file_output.init_file"},
            {"write", "([B)V", false, false,
             "android.file_output.write_bytes"},
            {"flush", "()V", false, false, "android.file_output.flush"},
            {"close", "()V", false, false, "android.file_output.close"},
        };
        catalog.push_back(std::move(file_output));
        // Reader family: byte streams with line decoding on top. Wrapper
        // constructors adopt the wrapped stream's record (the wrapped
        // object becomes unusable, matching single-owner usage).
        Decl reader;
        reader.descriptor = "Ljava/io/Reader;";
        reader.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(reader));
        Decl file_reader;
        file_reader.descriptor = "Ljava/io/FileReader;";
        file_reader.superclass = "Ljava/io/Reader;";
        file_reader.methods = {
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.file_stream.init_path"},
            {"<init>", "(Ljava/io/File;)V", false, false,
             "android.file_stream.init_file"},
        };
        catalog.push_back(std::move(file_reader));
        Decl stream_reader;
        stream_reader.descriptor = "Ljava/io/InputStreamReader;";
        stream_reader.superclass = "Ljava/io/Reader;";
        stream_reader.methods = {
            {"<init>", "(Ljava/io/InputStream;)V", false, false,
             "android.reader.adopt_stream"},
            {"<init>",
             "(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V", false,
             false, "android.reader.adopt_stream"},
        };
        catalog.push_back(std::move(stream_reader));
        Decl buffered_reader;
        buffered_reader.descriptor = "Ljava/io/BufferedReader;";
        buffered_reader.superclass = "Ljava/io/Reader;";
        buffered_reader.methods = {
            {"<init>", "(Ljava/io/Reader;)V", false, false,
             "android.reader.adopt_stream"},
            {"readLine", "()Ljava/lang/String;", false, false,
             "android.reader.read_line"},
            {"ready", "()Z", false, false, "android.reader.ready"},
            {"close", "()V", false, false, "android.stream.close"},
        };
        catalog.push_back(std::move(buffered_reader));
        Decl charset;
        charset.descriptor = "Ljava/nio/charset/Charset;";
        charset.superclass = "Ljava/lang/Object;";
        charset.methods = {
            {"forName",
             "(Ljava/lang/String;)Ljava/nio/charset/Charset;", true, false,
             "android.charset.for_name"},
        };
        catalog.push_back(std::move(charset));
        Decl byte_input;
        byte_input.descriptor = "Ljava/io/ByteArrayInputStream;";
        byte_input.superclass = "Ljava/io/InputStream;";
        byte_input.methods = {
            {"<init>", "([B)V", false, false,
             "android.byte_stream.init_input"},
        };
        catalog.push_back(std::move(byte_input));
        Decl data_input;
        data_input.descriptor = "Ljava/io/DataInputStream;";
        data_input.superclass = "Ljava/io/InputStream;";
        data_input.methods = {
            {"<init>", "(Ljava/io/InputStream;)V", false, false,
             "android.reader.adopt_stream"},
            {"readFully", "([B)V", false, false,
             "android.data_input.read_fully"},
            {"skipBytes", "(I)I", false, false,
             "android.data_input.skip_bytes"},
            {"close", "()V", false, false, "android.stream.close"},
        };
        catalog.push_back(std::move(data_input));
        Decl byte_output;
        byte_output.descriptor = "Ljava/io/ByteArrayOutputStream;";
        byte_output.superclass = "Ljava/io/OutputStream;";
        byte_output.methods = {
            {"<init>", "()V", false, false,
             "android.byte_output.init"},
            {"write", "([BII)V", false, false,
             "android.byte_output.write_range"},
            {"write", "([B)V", false, false,
             "android.file_output.write_bytes"},
            {"toByteArray", "()[B", false, false,
             "android.byte_output.to_byte_array"},
            {"size", "()I", false, false, "android.byte_output.size"},
            {"toString", "()Ljava/lang/String;", false, false,
             "android.byte_output.to_string"},
            {"close", "()V", false, false, "android.graphics.noop"},
        };
        catalog.push_back(std::move(byte_output));
        Decl writer;
        writer.descriptor = "Ljava/io/Writer;";
        writer.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(writer));
        Decl file_writer;
        file_writer.descriptor = "Ljava/io/FileWriter;";
        file_writer.superclass = "Ljava/io/Writer;";
        file_writer.methods = {
            {"<init>", "(Ljava/io/File;Z)V", false, false,
             "android.file_writer.init_file_append"},
            {"append", "(C)Ljava/io/Writer;", false, false,
             "android.file_writer.append_char"},
            {"append", "(Ljava/lang/CharSequence;)Ljava/io/Writer;", false,
             false, "android.file_writer.append_sequence"},
            {"flush", "()V", false, false, "android.file_output.flush"},
            {"close", "()V", false, false, "android.file_output.close"},
        };
        catalog.push_back(std::move(file_writer));
        Decl data_output;
        data_output.descriptor = "Ljava/io/DataOutputStream;";
        data_output.superclass = "Ljava/io/OutputStream;";
        data_output.methods = {
            {"<init>", "(Ljava/io/OutputStream;)V", false, false,
             "android.data_output.init"},
            {"writeUTF", "(Ljava/lang/String;)V", false, false,
             "android.data_output.write_utf"},
            {"close", "()V", false, false, "android.data_output.close"},
        };
        catalog.push_back(std::move(data_output));
    }
    {
        Decl log;
        log.descriptor = "Landroid/util/Log;";
        log.superclass = "Ljava/lang/Object;";
        log.methods = {
            {"d", "(Ljava/lang/String;Ljava/lang/String;)I", true, false,
             "android.log.d"},
            {"e", "(Ljava/lang/String;Ljava/lang/String;)I", true, false,
             "android.log.e"},
            {"i", "(Ljava/lang/String;Ljava/lang/String;)I", true, false,
             "android.log.i"},
            {"w", "(Ljava/lang/String;Ljava/lang/String;)I", true, false,
             "android.log.w"},
            {"v", "(Ljava/lang/String;Ljava/lang/String;)I", true, false,
             "android.log.d"},
            {"e",
             "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I",
             true, false, "android.log.e"},
        };
        catalog.push_back(std::move(log));
    }
    {
        Decl audio;
        audio.descriptor = "Landroid/media/AudioManager;";
        audio.superclass = "Ljava/lang/Object;";
        audio.methods = {
            {"getRingerMode", "()I", false, false,
             "android.audio_manager.get_ringer_mode"},
            {"getStreamMaxVolume", "(I)I", false, false,
             "android.audio_manager.get_stream_max_volume"},
            {"setStreamVolume", "(III)V", false, false,
             "android.audio_manager.set_stream_volume"},
            // The mixer has no master volume; streams run at full scale, so
            // the queried volume is truthfully the maximum.
            {"getStreamVolume", "(I)I", false, false,
             "android.audio_manager.get_stream_max_volume"},
            {"setStreamMute", "(IZ)V", false, false,
             "android.audio_manager.set_stream_volume"},
        };
        catalog.push_back(std::move(audio));
    }
    {
        Decl wifi;
        wifi.descriptor = "Landroid/net/wifi/WifiManager;";
        wifi.superclass = "Ljava/lang/Object;";
        wifi.methods = {
            {"isWifiEnabled", "()Z", false, false,
             "android.wifi.is_enabled"},
            {"getWifiState", "()I", false, false,
             "android.wifi.get_state"},
            {"setWifiEnabled", "(Z)Z", false, false,
             "android.wifi.set_enabled"},
            {"getConnectionInfo", "()Landroid/net/wifi/WifiInfo;", false,
             false, "android.wifi.get_connection_info"},
            {"createWifiLock",
             "(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;",
             false, false, "android.wifi.create_lock"},
        };
        catalog.push_back(std::move(wifi));
        // Wake locks are real no-ops: the host never sleeps mid-session.
        Decl wifi_lock;
        wifi_lock.descriptor = "Landroid/net/wifi/WifiManager$WifiLock;";
        wifi_lock.superclass = "Ljava/lang/Object;";
        wifi_lock.methods = {
            {"acquire", "()V", false, false, "android.graphics.noop"},
            {"release", "()V", false, false, "android.graphics.noop"},
            {"isHeld", "()Z", false, false, "android.telephony.false"},
        };
        catalog.push_back(std::move(wifi_lock));
        Decl wifi_info;
        wifi_info.descriptor = "Landroid/net/wifi/WifiInfo;";
        wifi_info.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(wifi_info));
    }
    {
        // Canvas-drawn installer views create Paint state objects in their
        // constructors. Paint holds pure drawing state; state setters are
        // no-ops until a canvas surface actually consumes them.
        Decl paint;
        paint.descriptor = "Landroid/graphics/Paint;";
        paint.superclass = "Ljava/lang/Object;";
        paint.methods = {
            {"<init>", "()V", false, false, "android.graphics.noop"},
            {"<init>", "(I)V", false, false, "android.graphics.noop"},
            {"setColor", "(I)V", false, false, "android.graphics.noop"},
            {"setAntiAlias", "(Z)V", false, false, "android.graphics.noop"},
            {"setTextSize", "(F)V", false, false, "android.graphics.noop"},
            {"setTypeface",
             "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;",
             false, false, "android.paint.set_typeface"},
        };
        catalog.push_back(std::move(paint));
        Decl typeface;
        typeface.descriptor = "Landroid/graphics/Typeface;";
        typeface.superclass = "Ljava/lang/Object;";
        typeface.methods = {
            {"defaultFromStyle", "(I)Landroid/graphics/Typeface;", true,
             false, "android.typeface.default_from_style"},
        };
        typeface.fields = {
            {"SERIF", "Landroid/graphics/Typeface;", true, false, 0, ""},
        };
        typeface.clinit_handler = "android.typeface.clinit";
        catalog.push_back(std::move(typeface));
        Decl matrix;
        matrix.descriptor = "Landroid/graphics/Matrix;";
        matrix.superclass = "Ljava/lang/Object;";
        matrix.methods = {
            {"<init>", "()V", false, false, "android.graphics.noop"},
        };
        catalog.push_back(std::move(matrix));
        Decl rect;
        rect.descriptor = "Landroid/graphics/Rect;";
        rect.superclass = "Ljava/lang/Object;";
        rect.fields = {
            {"left", "I", false, false, 0, ""},
            {"top", "I", false, false, 0, ""},
            {"right", "I", false, false, 0, ""},
            {"bottom", "I", false, false, 0, ""},
        };
        rect.methods = {
            {"<init>", "()V", false, false, "android.graphics.noop"},
            {"width", "()I", false, false, "android.rect.width"},
            {"height", "()I", false, false, "android.rect.height"},
        };
        catalog.push_back(std::move(rect));
        Decl paint_drawable;
        paint_drawable.descriptor =
            "Landroid/graphics/drawable/PaintDrawable;";
        paint_drawable.superclass = "Landroid/graphics/drawable/Drawable;";
        paint_drawable.methods = {
            {"<init>", "(I)V", false, false, "android.graphics.noop"},
            {"setCornerRadius", "(F)V", false, false,
             "android.graphics.noop"},
        };
        catalog.push_back(std::move(paint_drawable));
        // Bitmaps carry real host-side ARGB8888 pixel stores; decode uses
        // the vendored stb_image (BitmapFactory returns the documented null
        // on undecodable data). Config selects storage precision on device;
        // this platform always keeps full 8888 precision, which the
        // getPixels contract (packed ARGB ints) makes observationally
        // equivalent.
        Decl bitmap_config;
        bitmap_config.descriptor = "Landroid/graphics/Bitmap$Config;";
        bitmap_config.superclass = "Ljava/lang/Object;";
        bitmap_config.fields = {
            {"ARGB_4444", "Landroid/graphics/Bitmap$Config;", true, false, 0,
             ""},
            {"ARGB_8888", "Landroid/graphics/Bitmap$Config;", true, false, 0,
             ""},
        };
        bitmap_config.clinit_handler = "android.bitmap_config.clinit";
        catalog.push_back(std::move(bitmap_config));
        Decl bitmap;
        bitmap.descriptor = "Landroid/graphics/Bitmap;";
        bitmap.superclass = "Ljava/lang/Object;";
        bitmap.methods = {
            {"createBitmap",
             "([IIILandroid/graphics/Bitmap$Config;)"
             "Landroid/graphics/Bitmap;",
             true, false, "android.bitmap.create"},
            {"createBitmap",
             "([IIIIILandroid/graphics/Bitmap$Config;)"
             "Landroid/graphics/Bitmap;",
             true, false, "android.bitmap.create_offset"},
            {"getWidth", "()I", false, false, "android.bitmap.get_width"},
            {"getHeight", "()I", false, false, "android.bitmap.get_height"},
            {"getPixels", "([IIIIIII)V", false, false,
             "android.bitmap.get_pixels"},
            {"prepareToDraw", "()V", false, false, "android.graphics.noop"},
            {"recycle", "()V", false, false, "android.bitmap.recycle"},
        };
        catalog.push_back(std::move(bitmap));
        Decl bitmap_factory;
        bitmap_factory.descriptor = "Landroid/graphics/BitmapFactory;";
        bitmap_factory.superclass = "Ljava/lang/Object;";
        bitmap_factory.methods = {
            {"decodeByteArray", "([BII)Landroid/graphics/Bitmap;", true,
             false, "android.bitmap_factory.decode_byte_array"},
        };
        catalog.push_back(std::move(bitmap_factory));
        // Canvas instances only come from the framework; the dex_activity
        // lifecycle never dispatches View.onDraw, so draw calls are pure
        // presentation with no consumer. State queries answer with the real
        // surface geometry.
        Decl region_op;
        region_op.descriptor = "Landroid/graphics/Region$Op;";
        region_op.superclass = "Ljava/lang/Object;";
        region_op.fields = {
            {"REPLACE", "Landroid/graphics/Region$Op;", true, false, 0, ""},
        };
        region_op.clinit_handler = "android.region_op.clinit";
        catalog.push_back(std::move(region_op));
        Decl canvas;
        canvas.descriptor = "Landroid/graphics/Canvas;";
        canvas.superclass = "Ljava/lang/Object;";
        canvas.methods = {
            {"save", "(I)I", false, false, "android.canvas.save"},
            {"restore", "()V", false, false, "android.graphics.noop"},
            {"clipRect", "(FFFFLandroid/graphics/Region$Op;)Z", false, false,
             "android.canvas.clip_rect"},
            {"getClipBounds", "()Landroid/graphics/Rect;", false, false,
             "android.canvas.get_clip_bounds"},
            {"drawColor", "(I)V", false, false, "android.graphics.noop"},
            {"drawBitmap",
             "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V", false,
             false, "android.graphics.noop"},
            {"drawBitmap", "([IIIIIIIZLandroid/graphics/Paint;)V", false,
             false, "android.graphics.noop"},
        };
        catalog.push_back(std::move(canvas));
    }
    {
        // Cooperative message passing: everything runs on the single VM
        // host thread, so sendMessage dispatches handleMessage
        // synchronously; Looper.prepare is a real no-op in this model.
        Decl looper;
        looper.descriptor = "Landroid/os/Looper;";
        looper.superclass = "Ljava/lang/Object;";
        looper.methods = {
            {"prepare", "()V", true, false, "android.looper.noop"},
            {"loop", "()V", true, false, "android.looper.noop"},
        };
        catalog.push_back(std::move(looper));
        Decl message;
        message.descriptor = "Landroid/os/Message;";
        message.superclass = "Ljava/lang/Object;";
        message.fields = {
            {"what", "I", false, false, 0, ""},
            {"arg1", "I", false, false, 0, ""},
            {"arg2", "I", false, false, 0, ""},
        };
        catalog.push_back(std::move(message));
        Decl handler;
        handler.descriptor = "Landroid/os/Handler;";
        handler.superclass = "Ljava/lang/Object;";
        handler.methods = {
            {"<init>", "()V", false, false, "android.handler.init"},
            {"obtainMessage", "()Landroid/os/Message;", false, false,
             "android.handler.obtain_message"},
            {"sendMessage", "(Landroid/os/Message;)Z", false, false,
             "android.handler.send_message"},
            {"handleMessage", "(Landroid/os/Message;)V", false, true,
             "android.handler.handle_message_noop"},
        };
        catalog.push_back(std::move(handler));
    }
    {
        // Offline runtime: the manager exists and truthfully reports no
        // active network (null NetworkInfo is the documented "no network"
        // value); network transfer itself stays a non-goal.
        Decl connectivity;
        connectivity.descriptor = "Landroid/net/ConnectivityManager;";
        connectivity.superclass = "Ljava/lang/Object;";
        connectivity.methods = {
            {"getActiveNetworkInfo", "()Landroid/net/NetworkInfo;", false,
             false, "android.connectivity.get_active_network_info"},
            {"getNetworkInfo", "(I)Landroid/net/NetworkInfo;", false,
             false, "android.connectivity.get_network_info"},
        };
        catalog.push_back(std::move(connectivity));
        Decl network_info;
        network_info.descriptor = "Landroid/net/NetworkInfo;";
        network_info.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(network_info));
    }
    {
        Decl listener;
        listener.descriptor = "Landroid/hardware/SensorEventListener;";
        listener.is_interface = true;
        catalog.push_back(std::move(listener));
        Decl sensor;
        sensor.descriptor = "Landroid/hardware/Sensor;";
        sensor.superclass = "Ljava/lang/Object;";
        sensor.methods = {
            {"getType", "()I", false, false, "android.sensor.get_type"},
        };
        catalog.push_back(std::move(sensor));
        Decl event;
        event.descriptor = "Landroid/hardware/SensorEvent;";
        event.superclass = "Ljava/lang/Object;";
        event.fields = {{"values", "[F", false, false, 0, ""}};
        catalog.push_back(std::move(event));
        Decl manager;
        manager.descriptor = "Landroid/hardware/SensorManager;";
        manager.superclass = "Ljava/lang/Object;";
        manager.methods = {
            {"getDefaultSensor", "(I)Landroid/hardware/Sensor;", false,
             false, "android.sensor_manager.get_default"},
            {"registerListener",
             "(Landroid/hardware/SensorEventListener;"
             "Landroid/hardware/Sensor;I)Z",
             false, false, "android.sensor_manager.register"},
            {"unregisterListener",
             "(Landroid/hardware/SensorEventListener;)V", false, false,
             "android.sensor_manager.unregister"},
        };
        catalog.push_back(std::move(manager));
    }
    {
        Decl telephony;
        telephony.descriptor = "Landroid/telephony/TelephonyManager;";
        telephony.superclass = "Ljava/lang/Object;";
        telephony.methods = {
            {"getDeviceId", "()Ljava/lang/String;", false, false,
             "android.telephony.get_device_id"},
            {"getDeviceSoftwareVersion", "()Ljava/lang/String;", false,
             false, "android.telephony.get_software_version"},
            {"getLine1Number", "()Ljava/lang/String;", false, false,
             "android.telephony.get_line1_number"},
            {"getNetworkOperator", "()Ljava/lang/String;", false, false,
             "android.telephony.get_network_operator"},
            // No SIM and no carrier network exist on this platform: the
            // documented empty/false answers for an absent SIM.
            {"getNetworkOperatorName", "()Ljava/lang/String;", false, false,
             "android.telephony.empty_string"},
            {"getNetworkCountryIso", "()Ljava/lang/String;", false, false,
             "android.telephony.empty_string"},
            {"getSimCountryIso", "()Ljava/lang/String;", false, false,
             "android.telephony.empty_string"},
            {"getSimOperator", "()Ljava/lang/String;", false, false,
             "android.telephony.empty_string"},
            {"getSimOperatorName", "()Ljava/lang/String;", false, false,
             "android.telephony.empty_string"},
            {"isNetworkRoaming", "()Z", false, false,
             "android.telephony.false"},
        };
        catalog.push_back(std::move(telephony));
    }
    {
        Decl pool;
        pool.descriptor = "Landroid/media/SoundPool;";
        pool.superclass = "Ljava/lang/Object;";
        pool.methods = {
            {"<init>", "(III)V", false, false, "android.sound_pool.init"},
            {"load", "(Landroid/content/Context;II)I", false, false,
             "android.sound_pool.load"},
            {"play", "(IFFIIF)I", false, false, "android.sound_pool.play"},
            {"pause", "(I)V", false, false, "android.sound_pool.pause"},
            {"resume", "(I)V", false, false, "android.sound_pool.resume"},
            {"stop", "(I)V", false, false, "android.sound_pool.stop"},
            {"unload", "(I)Z", false, false, "android.sound_pool.unload"},
            {"release", "()V", false, false, "android.sound_pool.release"},
            {"setVolume", "(IFF)V", false, false,
             "android.sound_pool.set_volume"},
            {"setRate", "(IF)V", false, false,
             "android.sound_pool.set_rate"},
        };
        catalog.push_back(std::move(pool));
    }
    {
        Decl completion;
        completion.descriptor =
            "Landroid/media/MediaPlayer$OnCompletionListener;";
        completion.is_interface = true;
        catalog.push_back(std::move(completion));
        Decl player;
        player.descriptor = "Landroid/media/MediaPlayer;";
        player.superclass = "Ljava/lang/Object;";
        player.methods = {
            {"<init>", "()V", false, false, "android.media_player.init"},
            {"setDataSource", "(Ljava/lang/String;)V", false, false,
             "android.media_player.set_data_source"},
            {"isLooping", "()Z", false, false,
             "android.media_player.is_looping"},
            {"create",
             "(Landroid/content/Context;I)Landroid/media/MediaPlayer;",
             true, false, "android.media_player.create"},
            {"isPlaying", "()Z", false, false,
             "android.media_player.is_playing"},
            {"start", "()V", false, false, "android.media_player.start"},
            {"pause", "()V", false, false, "android.media_player.pause"},
            {"stop", "()V", false, false, "android.media_player.stop"},
            {"release", "()V", false, false,
             "android.media_player.release"},
            {"prepare", "()V", false, false,
             "android.media_player.prepare"},
            {"seekTo", "(I)V", false, false,
             "android.media_player.seek_to"},
            {"setLooping", "(Z)V", false, false,
             "android.media_player.set_looping"},
            {"setVolume", "(FF)V", false, false,
             "android.media_player.set_volume"},
            {"setOnCompletionListener",
             "(Landroid/media/MediaPlayer$OnCompletionListener;)V", false,
             false, "android.media_player.set_completion_listener"},
        };
        catalog.push_back(std::move(player));
    }
    {
        Decl bundle;
        bundle.descriptor = "Landroid/os/Bundle;";
        bundle.superclass = "Ljava/lang/Object;";
        bundle.methods = {
            {"<init>", "()V", false, false, "android.bundle.init"},
            {"get", "(Ljava/lang/String;)Ljava/lang/Object;", false, false,
             "android.bundle.get"},
            {"getInt", "(Ljava/lang/String;)I", false, false,
             "android.bundle.get_int"},
            {"getString", "(Ljava/lang/String;)Ljava/lang/String;", false,
             false, "android.bundle.get_string"},
        };
        catalog.push_back(std::move(bundle));
    }
    {
        // External storage facts come from the session's external mount
        // (root path + real free space of the backing host volume).
        Decl environment;
        environment.descriptor = "Landroid/os/Environment;";
        environment.superclass = "Ljava/lang/Object;";
        environment.methods = {
            {"getExternalStorageDirectory", "()Ljava/io/File;", true, false,
             "android.environment.get_external_storage_dir"},
            {"getExternalStorageState", "()Ljava/lang/String;", true, false,
             "android.environment.get_external_storage_state"},
        };
        catalog.push_back(std::move(environment));
        Decl statfs;
        statfs.descriptor = "Landroid/os/StatFs;";
        statfs.superclass = "Ljava/lang/Object;";
        statfs.methods = {
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.statfs.init"},
            {"getBlockSize", "()I", false, false,
             "android.statfs.get_block_size"},
            {"getAvailableBlocks", "()I", false, false,
             "android.statfs.get_available_blocks"},
        };
        catalog.push_back(std::move(statfs));
        // Neutral identity: AOSP's default for unset build properties is
        // "unknown"; the ABI is the real guest ABI. Vendor/device names
        // never appear in src/ (AGENTS scope rule).
        Decl build;
        build.descriptor = "Landroid/os/Build;";
        build.superclass = "Ljava/lang/Object;";
        build.fields = {
            {"CPU_ABI", "Ljava/lang/String;", true, true, 0, "armeabi"},
            {"DEVICE", "Ljava/lang/String;", true, true, 0, "unknown"},
            {"MANUFACTURER", "Ljava/lang/String;", true, true, 0, "unknown"},
            {"MODEL", "Ljava/lang/String;", true, true, 0, "unknown"},
            {"PRODUCT", "Ljava/lang/String;", true, true, 0, "unknown"},
            {"TAGS", "Ljava/lang/String;", true, true, 0, "release-keys"},
        };
        catalog.push_back(std::move(build));
    }
    {
        Decl version;
        version.descriptor = "Landroid/os/Build$VERSION;";
        version.superclass = "Ljava/lang/Object;";
        version.fields = {
            {"SDK_INT", "I", true, true, 19, ""},
            {"SDK", "Ljava/lang/String;", true, true, 0, "19"},
            {"RELEASE", "Ljava/lang/String;", true, true, 0, "4.4.4"},
        };
        catalog.push_back(std::move(version));
    }
    {
        Decl receiver;
        receiver.descriptor = "Landroid/content/BroadcastReceiver;";
        receiver.superclass = "Ljava/lang/Object;";
        receiver.methods = {
            {"<init>", "()V", false, false, "android.receiver.init"},
            {"onReceive",
             "(Landroid/content/Context;Landroid/content/Intent;)V", false,
             true, "android.receiver.on_receive_noop"},
        };
        catalog.push_back(std::move(receiver));
        Decl filter;
        filter.descriptor = "Landroid/content/IntentFilter;";
        filter.superclass = "Ljava/lang/Object;";
        filter.methods = {
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.intent_filter.init"},
            {"<init>", "()V", false, false,
             "android.intent_filter.init_empty"},
            {"addAction", "(Ljava/lang/String;)V", false, false,
             "android.intent_filter.add_action"},
        };
        catalog.push_back(std::move(filter));
        Decl intent;
        intent.descriptor = "Landroid/content/Intent;";
        intent.superclass = "Ljava/lang/Object;";
        intent.methods = {
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.intent.init"},
            {"<init>", "()V", false, false, "android.intent.init"},
            {"<init>", "(Ljava/lang/String;Landroid/net/Uri;)V", false,
             false, "android.intent.init"},
            {"<init>", "(Ljava/lang/String;Landroid/net/Uri;)V", false,
             false, "android.intent.init"},
            {"<init>",
             "(Landroid/content/Context;Ljava/lang/Class;)V", false, false,
             "android.intent.init_component"},
            {"setClassName",
             "(Ljava/lang/String;Ljava/lang/String;)"
             "Landroid/content/Intent;",
             false, false, "android.intent.set_class_name"},
            {"addFlags", "(I)Landroid/content/Intent;", false, false,
             "android.intent.set_flags"},
            {"putExtra",
             "(Ljava/lang/String;I)Landroid/content/Intent;", false, false,
             "android.intent.put_extra_int"},
            {"putExtra",
             "(Ljava/lang/String;Ljava/lang/String;)"
             "Landroid/content/Intent;",
             false, false, "android.intent.put_extra_string"},
            {"getStringExtra",
             "(Ljava/lang/String;)Ljava/lang/String;", false, false,
             "android.intent.get_string_extra"},
            {"getIntExtra", "(Ljava/lang/String;I)I", false, false,
             "android.intent.get_int_extra"},
            {"addCategory",
             "(Ljava/lang/String;)Landroid/content/Intent;", false, false,
             "android.intent.set_flags"},
            {"getAction", "()Ljava/lang/String;", false, false,
             "android.intent.get_action"},
            {"getExtras", "()Landroid/os/Bundle;", false, false,
             "android.intent.get_extras"},
            {"setFlags", "(I)Landroid/content/Intent;", false, false,
             "android.intent.set_flags"},
            {"setDataAndType",
             "(Landroid/net/Uri;Ljava/lang/String;)"
             "Landroid/content/Intent;",
             false, false, "android.intent.set_data_and_type"},
        };
        catalog.push_back(std::move(intent));
        Decl pending;
        pending.descriptor = "Landroid/app/PendingIntent;";
        pending.superclass = "Ljava/lang/Object;";
        pending.methods = {
            {"getBroadcast",
             "(Landroid/content/Context;ILandroid/content/Intent;I)"
             "Landroid/app/PendingIntent;",
             true, false, "android.pending_intent.get_broadcast"},
        };
        catalog.push_back(std::move(pending));
        Decl uri;
        uri.descriptor = "Landroid/net/Uri;";
        uri.superclass = "Ljava/lang/Object;";
        uri.methods = {
            {"parse", "(Ljava/lang/String;)Landroid/net/Uri;", true, false,
             "android.uri.parse"},
        };
        catalog.push_back(std::move(uri));
    }
    {
        Decl toast;
        toast.descriptor = "Landroid/widget/Toast;";
        toast.superclass = "Ljava/lang/Object;";
        toast.methods = {
            {"makeText",
             "(Landroid/content/Context;Ljava/lang/CharSequence;I)"
             "Landroid/widget/Toast;",
             true, false, "android.toast.make_text"},
            {"show", "()V", false, false, "android.toast.show"},
        };
        catalog.push_back(std::move(toast));
    }
    {
        Decl motion;
        motion.descriptor = "Landroid/view/MotionEvent;";
        motion.superclass = "Ljava/lang/Object;";
        motion.fields = {
            {"action", "I", false, false, 0, ""},
            {"x", "F", false, false, 0, ""},
            {"y", "F", false, false, 0, ""},
            {"pointer", "I", false, false, 0, ""},
        };
        motion.methods = {
            {"getAction", "()I", false, false,
             "android.motion_event.get_action"},
            {"getX", "()F", false, false, "android.motion_event.get_x"},
            {"getY", "()F", false, false, "android.motion_event.get_y"},
            {"getX", "(I)F", false, false,
             "android.motion_event.get_x_indexed"},
            {"getY", "(I)F", false, false,
             "android.motion_event.get_y_indexed"},
            {"getPointerCount", "()I", false, false,
             "android.motion_event.get_pointer_count"},
            {"getPointerId", "(I)I", false, false,
             "android.motion_event.get_pointer_id"},
        };
        catalog.push_back(std::move(motion));
        Decl key;
        key.descriptor = "Landroid/view/KeyEvent;";
        key.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(key));
    }
    {
        Decl locale;
        locale.descriptor = "Ljava/util/Locale;";
        locale.superclass = "Ljava/lang/Object;";
        locale.methods = {
            {"getDefault", "()Ljava/util/Locale;", true, false,
             "android.locale.get_default"},
            {"getISO3Language", "()Ljava/lang/String;", false, false,
             "android.locale.get_iso3_language"},
            {"getISO3Country", "()Ljava/lang/String;", false, false,
             "android.locale.get_iso3_country"},
        };
        catalog.push_back(std::move(locale));
    }
    {
        Decl thread;
        thread.descriptor = "Ljava/lang/Thread;";
        thread.superclass = "Ljava/lang/Object;";
        thread.interfaces = {"Ljava/lang/Runnable;"};
        thread.methods = {
            {"sleep", "(J)V", true, false, "android.thread.sleep"},
            {"<init>", "(Ljava/lang/Runnable;)V", false, false,
             "android.thread.init_runnable"},
            {"start", "()V", false, false, "android.thread.start"},
            {"join", "()V", false, false, "android.thread.join"},
            {"isAlive", "()Z", false, false, "android.thread.is_alive"},
        };
        catalog.push_back(std::move(thread));
    }
    {
        // Cooperative timer (04 §3 interim): schedule() queues the task on
        // the cooperative thread queue and the delay collapses to the next
        // lifecycle frame boundary (deterministic clock, no host timers).
        Decl timer;
        timer.descriptor = "Ljava/util/Timer;";
        timer.superclass = "Ljava/lang/Object;";
        timer.methods = {
            {"<init>", "()V", false, false, "android.timer.init"},
            {"schedule", "(Ljava/util/TimerTask;J)V", false, false,
             "android.timer.schedule"},
            {"schedule", "(Ljava/util/TimerTask;JJ)V", false, false,
             "android.timer.schedule_repeating"},
            {"cancel", "()V", false, false, "android.timer.cancel"},
        };
        catalog.push_back(std::move(timer));
    }
    {
        Decl sms;
        sms.descriptor = "Landroid/telephony/SmsManager;";
        sms.superclass = "Ljava/lang/Object;";
        sms.methods = {
            {"getDefault", "()Landroid/telephony/SmsManager;", true, false,
             "android.sms.get_default"},
            {"sendTextMessage",
             "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
             "Landroid/app/PendingIntent;Landroid/app/PendingIntent;)V",
             false, false, "android.sms.send_text"},
        };
        catalog.push_back(std::move(sms));
        Decl message;
        message.descriptor = "Landroid/telephony/SmsMessage;";
        message.superclass = "Ljava/lang/Object;";
        message.methods = {
            {"createFromPdu", "([B)Landroid/telephony/SmsMessage;", true,
             false, "android.sms.create_from_pdu"},
            {"getMessageBody", "()Ljava/lang/String;", false, false,
             "android.sms.get_message_body"},
            {"getOriginatingAddress", "()Ljava/lang/String;", false, false,
             "android.sms.get_originating_address"},
        };
        catalog.push_back(std::move(message));
    }
    {
        // Network surface is a non-goal: classes resolve so linking works,
        // every method hit is an accounted explicit failure.
        Decl url;
        url.descriptor = "Ljava/net/URL;";
        url.superclass = "Ljava/lang/Object;";
        url.methods = {
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.net.unsupported"},
            {"openConnection", "()Ljava/net/URLConnection;", false, false,
             "android.net.unsupported"},
        };
        catalog.push_back(std::move(url));
        Decl connection;
        connection.descriptor = "Ljava/net/URLConnection;";
        connection.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(connection));
        Decl http;
        http.descriptor = "Ljava/net/HttpURLConnection;";
        http.superclass = "Ljava/net/URLConnection;";
        http.methods = {
            {"connect", "()V", false, false, "android.net.unsupported"},
            {"disconnect", "()V", false, false, "android.net.unsupported"},
            {"getInputStream", "()Ljava/io/InputStream;", false, false,
             "android.net.unsupported"},
            {"setConnectTimeout", "(I)V", false, false,
             "android.net.unsupported"},
        };
        catalog.push_back(std::move(http));
        // TLS setup is local state and succeeds as truthful no-ops (trust
        // configuration has nothing to protect when no socket ever
        // opens); any actual connection stays an accounted failure.
        Decl ssl_context;
        ssl_context.descriptor = "Ljavax/net/ssl/SSLContext;";
        ssl_context.superclass = "Ljava/lang/Object;";
        ssl_context.methods = {
            {"getInstance",
             "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;", true, false,
             "android.ssl.context_instance"},
            {"init",
             "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;"
             "Ljava/security/SecureRandom;)V",
             false, false, "android.graphics.noop"},
            {"getSocketFactory", "()Ljavax/net/ssl/SSLSocketFactory;",
             false, false, "android.ssl.socket_factory"},
        };
        catalog.push_back(std::move(ssl_context));
        Decl ssl_factory;
        ssl_factory.descriptor = "Ljavax/net/ssl/SSLSocketFactory;";
        ssl_factory.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(ssl_factory));
        Decl key_manager;
        key_manager.descriptor = "Ljavax/net/ssl/KeyManager;";
        key_manager.is_interface = true;
        catalog.push_back(std::move(key_manager));
        Decl trust_manager;
        trust_manager.descriptor = "Ljavax/net/ssl/TrustManager;";
        trust_manager.is_interface = true;
        catalog.push_back(std::move(trust_manager));
        Decl https;
        https.descriptor = "Ljavax/net/ssl/HttpsURLConnection;";
        https.superclass = "Ljava/net/HttpURLConnection;";
        https.methods = {
            {"setDefaultHostnameVerifier",
             "(Ljavax/net/ssl/HostnameVerifier;)V", true, false,
             "android.graphics.noop"},
            {"setDefaultSSLSocketFactory",
             "(Ljavax/net/ssl/SSLSocketFactory;)V", true, false,
             "android.graphics.noop"},
            {"setRequestMethod", "(Ljava/lang/String;)V", false, false,
             "android.net.unsupported"},
            {"setRequestProperty",
             "(Ljava/lang/String;Ljava/lang/String;)V", false, false,
             "android.net.unsupported"},
            {"getResponseCode", "()I", false, false,
             "android.net.unsupported"},
            {"getInputStream", "()Ljava/io/InputStream;", false, false,
             "android.net.unsupported"},
        };
        catalog.push_back(std::move(https));
    }
    {
        // Installer widget layer. The dex_activity lifecycle never draws
        // the view hierarchy (the GL surface is the only visual output),
        // so widgets are state-holding views: text is real (backed by the
        // interpreter's builder buffers so game logic round-trips), all
        // presentation setters are truthful no-ops, geometry queries
        // answer the real surface.
        Decl view_group;
        view_group.descriptor = "Landroid/view/ViewGroup;";
        view_group.superclass = "Landroid/view/View;";
        view_group.methods = {
            {"addView", "(Landroid/view/View;)V", false, false,
             "android.widget.noop"},
            {"addView",
             "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V",
             false, false, "android.widget.noop"},
            {"addView",
             "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             false, false, "android.widget.noop"},
            {"removeView", "(Landroid/view/View;)V", false, false,
             "android.widget.noop"},
            {"removeViews", "(II)V", false, false, "android.widget.noop"},
            {"updateViewLayout",
             "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             false, false, "android.widget.noop"},
        };
        catalog.push_back(std::move(view_group));
        Decl layout_params_base;
        layout_params_base.descriptor =
            "Landroid/view/ViewGroup$LayoutParams;";
        layout_params_base.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(layout_params_base));
        Decl absolute_layout;
        absolute_layout.descriptor = "Landroid/widget/AbsoluteLayout;";
        absolute_layout.superclass = "Landroid/view/ViewGroup;";
        absolute_layout.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
        };
        catalog.push_back(std::move(absolute_layout));
        // Container family used by the titles' inflated layouts.
        for (const char* descriptor :
             {"Landroid/widget/LinearLayout;",
              "Landroid/widget/FrameLayout;",
              "Landroid/widget/RelativeLayout;",
              "Landroid/widget/TableLayout;", "Landroid/widget/TableRow;",
              "Landroid/widget/ScrollView;"}) {
            Decl container;
            container.descriptor = descriptor;
            container.superclass = "Landroid/view/ViewGroup;";
            container.methods = {
                {"<init>", "(Landroid/content/Context;)V", false, false,
                 "android.view.init"},
            };
            catalog.push_back(std::move(container));
        }
        Decl text_view;
        text_view.descriptor = "Landroid/widget/TextView;";
        text_view.superclass = "Landroid/view/View;";
        text_view.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
            {"setText", "(Ljava/lang/CharSequence;)V", false, false,
             "android.textview.set_text"},
            {"getText", "()Ljava/lang/CharSequence;", false, false,
             "android.textview.get_text"},
            {"setTextColor", "(I)V", false, false, "android.widget.noop"},
            {"setTextSize", "(F)V", false, false, "android.widget.noop"},
            {"setTextSize", "(IF)V", false, false, "android.widget.noop"},
            {"setLines", "(I)V", false, false, "android.widget.noop"},
            {"setMaxLines", "(I)V", false, false, "android.widget.noop"},
            {"setMaxWidth", "(I)V", false, false, "android.widget.noop"},
            {"setGravity", "(I)V", false, false, "android.widget.noop"},
            {"setId", "(I)V", false, false, "android.widget.noop"},
            {"setTypeface", "(Landroid/graphics/Typeface;)V", false, false,
             "android.widget.noop"},
            {"getPaint", "()Landroid/text/TextPaint;", false, false,
             "android.textview.get_paint"},
            {"addTextChangedListener", "(Landroid/text/TextWatcher;)V",
             false, false, "android.widget.noop"},
        };
        catalog.push_back(std::move(text_view));
        Decl button;
        button.descriptor = "Landroid/widget/Button;";
        button.superclass = "Landroid/widget/TextView;";
        button.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
        };
        catalog.push_back(std::move(button));
        Decl edit_text;
        edit_text.descriptor = "Landroid/widget/EditText;";
        edit_text.superclass = "Landroid/widget/TextView;";
        edit_text.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
            {"getText", "()Landroid/text/Editable;", false, false,
             "android.edittext.get_editable"},
        };
        catalog.push_back(std::move(edit_text));
        Decl editable_interface;
        editable_interface.descriptor = "Landroid/text/Editable;";
        editable_interface.is_interface = true;
        editable_interface.methods = {
            {"clear", "()V", false, false, "android.editable.clear"},
            {"length", "()I", false, false, "android.editable.length"},
            {"replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;",
             false, false, "android.editable.replace"},
        };
        Decl editable_impl;
        editable_impl.descriptor = "Landroid/text/EditableImpl;";
        editable_impl.superclass = "Ljava/lang/Object;";
        editable_impl.interfaces = {"Landroid/text/Editable;"};
        editable_impl.methods = editable_interface.methods;
        catalog.push_back(std::move(editable_interface));
        catalog.push_back(std::move(editable_impl));
        Decl image_view;
        image_view.descriptor = "Landroid/widget/ImageView;";
        image_view.superclass = "Landroid/view/View;";
        image_view.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
            {"setImageResource", "(I)V", false, false,
             "android.widget.noop"},
            {"setScaleType", "(Landroid/widget/ImageView$ScaleType;)V",
             false, false, "android.widget.noop"},
        };
        catalog.push_back(std::move(image_view));
        Decl image_button;
        image_button.descriptor = "Landroid/widget/ImageButton;";
        image_button.superclass = "Landroid/widget/ImageView;";
        image_button.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
        };
        catalog.push_back(std::move(image_button));
        Decl scale_type;
        scale_type.descriptor = "Landroid/widget/ImageView$ScaleType;";
        scale_type.superclass = "Ljava/lang/Object;";
        scale_type.fields = {
            {"CENTER", "Landroid/widget/ImageView$ScaleType;", true, false,
             0, ""},
        };
        scale_type.clinit_handler = "android.scale_type.clinit";
        catalog.push_back(std::move(scale_type));
        Decl progress_bar;
        progress_bar.descriptor = "Landroid/widget/ProgressBar;";
        progress_bar.superclass = "Landroid/view/View;";
        progress_bar.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
        };
        catalog.push_back(std::move(progress_bar));
        // Video playback is not provided (recorded gap): controls log
        // loudly, position/duration answer zero so skip paths trigger.
        Decl video_view;
        video_view.descriptor = "Landroid/widget/VideoView;";
        video_view.superclass = "Landroid/view/View;";
        video_view.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
            {"setVideoPath", "(Ljava/lang/String;)V", false, false,
             "android.videoview.unsupported"},
            {"start", "()V", false, false, "android.videoview.start"},
            {"pause", "()V", false, false, "android.widget.noop"},
            {"seekTo", "(I)V", false, false, "android.widget.noop"},
            {"stopPlayback", "()V", false, false, "android.widget.noop"},
            {"getDuration", "()I", false, false, "android.widget.zero"},
            {"getCurrentPosition", "()I", false, false,
             "android.widget.zero"},
            {"setOnCompletionListener",
             "(Landroid/media/MediaPlayer$OnCompletionListener;)V", false,
             false, "android.videoview.set_completion"},
        };
        catalog.push_back(std::move(video_view));
        Decl text_paint;
        text_paint.descriptor = "Landroid/text/TextPaint;";
        text_paint.superclass = "Landroid/graphics/Paint;";
        text_paint.methods = {
            {"getTextBounds",
             "(Ljava/lang/String;IILandroid/graphics/Rect;)V", false, false,
             "android.paint.get_text_bounds"},
        };
        catalog.push_back(std::move(text_paint));
        Decl dialog_builder;
        dialog_builder.descriptor = "Landroid/app/AlertDialog$Builder;";
        dialog_builder.superclass = "Ljava/lang/Object;";
        dialog_builder.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.widget.noop"},
            {"setTitle",
             "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;",
             false, false, "android.widget.self"},
            {"setItems",
             "([Ljava/lang/CharSequence;"
             "Landroid/content/DialogInterface$OnClickListener;)"
             "Landroid/app/AlertDialog$Builder;",
             false, false, "android.widget.self"},
            {"create", "()Landroid/app/AlertDialog;", false, false,
             "android.dialog.create"},
        };
        catalog.push_back(std::move(dialog_builder));
        Decl alert_dialog;
        alert_dialog.descriptor = "Landroid/app/AlertDialog;";
        alert_dialog.superclass = "Ljava/lang/Object;";
        alert_dialog.methods = {
            {"show", "()V", false, false, "android.widget.noop"},
        };
        catalog.push_back(std::move(alert_dialog));
        Decl progress_dialog;
        progress_dialog.descriptor = "Landroid/app/ProgressDialog;";
        progress_dialog.superclass = "Ljava/lang/Object;";
        progress_dialog.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.widget.noop"},
            {"setMessage", "(Ljava/lang/CharSequence;)V", false, false,
             "android.widget.noop"},
            {"setProgressStyle", "(I)V", false, false,
             "android.widget.noop"},
            {"show", "()V", false, false, "android.widget.noop"},
            {"dismiss", "()V", false, false, "android.widget.noop"},
        };
        catalog.push_back(std::move(progress_dialog));
        // Web content is a non-goal: the widget exists, navigation is a
        // loud accounted no-op and settings answer neutral values.
        Decl web_settings;
        web_settings.descriptor = "Landroid/webkit/WebSettings;";
        web_settings.superclass = "Ljava/lang/Object;";
        web_settings.methods = {
            {"setJavaScriptEnabled", "(Z)V", false, false,
             "android.widget.noop"},
            {"getUserAgentString", "()Ljava/lang/String;", false, false,
             "android.telephony.empty_string"},
        };
        catalog.push_back(std::move(web_settings));
        Decl web_view;
        web_view.descriptor = "Landroid/webkit/WebView;";
        web_view.superclass = "Landroid/view/ViewGroup;";
        web_view.methods = {
            {"<init>", "(Landroid/content/Context;)V", false, false,
             "android.view.init"},
            {"loadUrl", "(Ljava/lang/String;)V", false, false,
             "android.webview.load_url"},
            {"getSettings", "()Landroid/webkit/WebSettings;", false, false,
             "android.webview.get_settings"},
            {"setWebViewClient", "(Landroid/webkit/WebViewClient;)V",
             false, false, "android.widget.noop"},
            {"addJavascriptInterface",
             "(Ljava/lang/Object;Ljava/lang/String;)V", false, false,
             "android.widget.noop"},
            {"clearHistory", "()V", false, false, "android.widget.noop"},
            {"goBack", "()V", false, false, "android.widget.noop"},
        };
        catalog.push_back(std::move(web_view));
        Decl settings_system;
        settings_system.descriptor = "Landroid/provider/Settings$System;";
        settings_system.superclass = "Ljava/lang/Object;";
        settings_system.methods = {
            {"getInt",
             "(Landroid/content/ContentResolver;Ljava/lang/String;I)I",
             true, false, "android.settings.get_int"},
            {"putInt",
             "(Landroid/content/ContentResolver;Ljava/lang/String;I)Z",
             true, false, "android.settings.put_int"},
        };
        catalog.push_back(std::move(settings_system));
        Decl ime;
        ime.descriptor = "Landroid/view/inputmethod/InputMethodManager;";
        ime.superclass = "Ljava/lang/Object;";
        ime.methods = {
            // No soft keyboard exists, so nothing was hidden.
            {"hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z", false,
             false, "android.telephony.false"},
        };
        catalog.push_back(std::move(ime));
        Decl network_state;
        network_state.descriptor = "Landroid/net/NetworkInfo$State;";
        network_state.superclass = "Ljava/lang/Object;";
        network_state.fields = {
            {"CONNECTED", "Landroid/net/NetworkInfo$State;", true, false, 0,
             ""},
        };
        network_state.clinit_handler = "android.network_state.clinit";
        catalog.push_back(std::move(network_state));
        Decl drawable;
        drawable.descriptor = "Landroid/graphics/drawable/Drawable;";
        drawable.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(drawable));
        Decl binder;
        binder.descriptor = "Landroid/os/IBinder;";
        binder.is_interface = true;
        catalog.push_back(std::move(binder));
    }
    // Hierarchy placeholders (03 §6 layering): platform classes referenced
    // as superclass/interface by title glue code. Declaring the type is all
    // linking needs; every undeclared method hit stays an accounted,
    // explicit UnsatisfiedLinkError through the existing intrinsic route.
    // Entries are machine-evidenced by the linker's full-gap report, never
    // guessed. Real semantics (Handler dispatch etc.) arrive only per hit
    // batch; javax.net.ssl / webkit are non-goals and stay placeholders.
    struct HierarchyPlaceholder final {
        const char* descriptor;
        bool is_interface;
        const char* superclass;    // nullptr => Object for classes
        const char* interface_of;  // optional single implemented interface
        bool with_init;            // subclasses call super.<init>()
    };
    static constexpr HierarchyPlaceholder kHierarchyPlaceholders[] = {
        // Dungeon Hunter (P1000) link gap, linker report 2026-08-12.
        {"Landroid/content/DialogInterface$OnClickListener;", true, nullptr,
         nullptr, false},
        {"Landroid/text/TextWatcher;", true, nullptr, nullptr, false},
        {"Landroid/view/View$OnClickListener;", true, nullptr, nullptr,
         false},
        {"Landroid/view/View$OnTouchListener;", true, nullptr, nullptr,
         false},
        {"Landroid/webkit/WebViewClient;", false, nullptr, nullptr, true},
        {"Ljava/util/TimerTask;", false, nullptr, "Ljava/lang/Runnable;",
         true},
        {"Ljavax/net/ssl/HostnameVerifier;", true, nullptr, nullptr, false},
        {"Ljavax/net/ssl/X509TrustManager;", true, nullptr, nullptr, false},
        {"Lorg/xml/sax/helpers/DefaultHandler;", false, nullptr, nullptr,
         true},
    };
    for (const auto& placeholder : kHierarchyPlaceholders) {
        Decl declaration;
        declaration.descriptor = placeholder.descriptor;
        declaration.is_interface = placeholder.is_interface;
        if (!placeholder.is_interface) {
            declaration.superclass = placeholder.superclass != nullptr
                                         ? placeholder.superclass
                                         : "Ljava/lang/Object;";
        }
        if (placeholder.interface_of != nullptr) {
            declaration.interfaces = {placeholder.interface_of};
        }
        if (placeholder.with_init) {
            declaration.methods = {
                {"<init>", "()V", false, false, "core.object.init"},
            };
        }
        catalog.push_back(std::move(declaration));
    }
    return catalog;
}

}  // namespace ogplay::runtime
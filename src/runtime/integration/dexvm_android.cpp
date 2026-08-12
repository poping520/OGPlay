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
        };
        catalog.push_back(std::move(context));
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
            {"getWindow", "()Landroid/view/Window;", false, false,
             "android.activity.get_window"},
            {"requestWindowFeature", "(I)Z", false, false,
             "android.activity.request_window_feature"},
            {"setContentView", "(Landroid/view/View;)V", false, false,
             "android.activity.set_content_view"},
            {"setVolumeControlStream", "(I)V", false, false,
             "android.activity.set_volume_control_stream"},
            {"onKeyDown", "(ILandroid/view/KeyEvent;)Z", false, true,
             "android.activity.on_key_false"},
            {"onKeyUp", "(ILandroid/view/KeyEvent;)Z", false, true,
             "android.activity.on_key_false"},
            {"onTouchEvent", "(Landroid/view/MotionEvent;)Z", false, true,
             "android.activity.on_touch_false"},
            {"finish", "()V", false, false, "android.activity.finish"},
        };
        catalog.push_back(std::move(activity));
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
        };
        catalog.push_back(std::move(window));
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
            {"exists", "()Z", false, false, "android.file.exists"},
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
            {"close", "()V", false, false, "android.file_output.close"},
        };
        catalog.push_back(std::move(file_output));
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
        };
        catalog.push_back(std::move(wifi));
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
        };
        catalog.push_back(std::move(bundle));
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
        };
        catalog.push_back(std::move(thread));
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
    }
    return catalog;
}

}  // namespace ogplay::runtime
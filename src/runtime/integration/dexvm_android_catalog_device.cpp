// Catalog batch: audio/Wi-Fi/sensor/telephony services, Looper/Handler,
// Bundle/Intent/Environment, threads, timers and the non-goal network
// and SMS surfaces.

#include "dexvm_android_internal.h"

namespace ogplay::runtime::android_intrinsics {

void AppendDeviceClasses(std::vector<Decl>& catalog) {
    {
        Decl audio;
        audio.descriptor = "Landroid/media/AudioManager;";
        audio.superclass = "Ljava/lang/Object;";
        audio.methods = {
            {"getRingerMode", "()I", false, false,
             "android.audio_manager.get_ringer_mode"},
            {"isMusicActive", "()Z", false, false,
             "android.audio_manager.is_music_active"},
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
        {"isWifiEnabled", "()Z", false, false, "android.wifi.is_enabled"},
        {"getWifiState", "()I", false, false, "android.wifi.get_state"},
        {"setWifiEnabled", "(Z)Z", false, false, "android.wifi.set_enabled"},
        {"getConnectionInfo", "()Landroid/net/wifi/WifiInfo;", false, false,
         "android.wifi.get_connection_info"},
            {"createWifiLock",
         "(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;", false,
         false, "android.wifi.create_lock"},
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
        // Cooperative message passing: everything runs on the single VM
        // host thread, so sendMessage dispatches handleMessage
        // synchronously; Looper.prepare is a real no-op in this model.
        Decl looper;
        looper.descriptor = "Landroid/os/Looper;";
        looper.superclass = "Ljava/lang/Object;";
        looper.methods = {
            {"prepare", "()V", true, false, "android.looper.noop"},
            {"loop", "()V", true, false, "android.looper.noop"},
            // One host thread drives the VM, so the main looper is the
            // only looper; it answers a per-session singleton.
            {"getMainLooper", "()Landroid/os/Looper;", true, false,
             "android.looper.get_main_looper"},
        };
        catalog.push_back(std::move(looper));
        Decl message;
        message.descriptor = "Landroid/os/Message;";
        message.superclass = "Ljava/lang/Object;";
        message.fields = {
            {"what", "I", false, false, 0, ""},
            {"arg1", "I", false, false, 0, ""},
            {"arg2", "I", false, false, 0, ""},
            {"obj", "Ljava/lang/Object;", false, false, 0, ""},
            // Delivery target recorded by obtainMessage/obtain.
            {"target", "Landroid/os/Handler;", false, false, 0, ""},
        };
        message.methods = {
            {"obtain",
         "(Landroid/os/Handler;ILjava/lang/Object;)Landroid/os/Message;", true,
         false, "android.message.obtain_static"},
        {"sendToTarget", "()V", false, false, "android.message.send_to_target"},
        };
        catalog.push_back(std::move(message));
        Decl handler;
        handler.descriptor = "Landroid/os/Handler;";
        handler.superclass = "Ljava/lang/Object;";
        handler.methods = {
            {"<init>", "()V", false, false, "android.handler.init"},
            // The looper argument can only name the main looper on this
            // single-threaded platform.
            {"<init>", "(Landroid/os/Looper;)V", false, false,
             "android.handler.init"},
            {"obtainMessage", "()Landroid/os/Message;", false, false,
             "android.handler.obtain_message"},
            {"obtainMessage", "(I)Landroid/os/Message;", false, false,
             "android.handler.obtain_message_what"},
        {"obtainMessage", "(ILjava/lang/Object;)Landroid/os/Message;", false,
         false, "android.handler.obtain_message_what_obj"},
            {"sendMessage", "(Landroid/os/Message;)Z", false, false,
             "android.handler.send_message"},
            {"dispatchMessage", "(Landroid/os/Message;)V", false, false,
             "android.handler.dispatch_message"},
            // Synchronous delivery on the single VM thread (the same
            // policy as runOnUiThread).
            {"post", "(Ljava/lang/Runnable;)Z", false, false,
             "android.handler.post"},
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
        {"getActiveNetworkInfo", "()Landroid/net/NetworkInfo;", false, false,
         "android.connectivity.get_active_network_info"},
        {"getNetworkInfo", "(I)Landroid/net/NetworkInfo;", false, false,
         "android.connectivity.get_network_info"},
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
        {"getDefaultSensor", "(I)Landroid/hardware/Sensor;", false, false,
         "android.sensor_manager.get_default"},
            {"registerListener",
             "(Landroid/hardware/SensorEventListener;"
             "Landroid/hardware/Sensor;I)Z",
             false, false, "android.sensor_manager.register"},
        {"unregisterListener", "(Landroid/hardware/SensorEventListener;)V",
         false, false, "android.sensor_manager.unregister"},
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
        {"getDeviceSoftwareVersion", "()Ljava/lang/String;", false, false,
         "android.telephony.get_software_version"},
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
        {"isNetworkRoaming", "()Z", false, false, "android.telephony.false"},
        {"getSimState", "()I", false, false, "android.telephony.get_sim_state"},
            {"getPhoneType", "()I", false, false,
             "android.telephony.get_phone_type"},
        {"listen", "(Landroid/telephony/PhoneStateListener;I)V", false, false,
         "android.telephony.listen"},
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
        {"setVolume", "(IFF)V", false, false, "android.sound_pool.set_volume"},
        {"setRate", "(IF)V", false, false, "android.sound_pool.set_rate"},
        };
        catalog.push_back(std::move(pool));
    }
    {
        Decl completion;
    completion.descriptor = "Landroid/media/MediaPlayer$OnCompletionListener;";
        completion.is_interface = true;
        catalog.push_back(std::move(completion));
        Decl player;
        player.descriptor = "Landroid/media/MediaPlayer;";
        player.superclass = "Ljava/lang/Object;";
        player.methods = {
            {"<init>", "()V", false, false, "android.media_player.init"},
            {"setDataSource", "(Ljava/lang/String;)V", false, false,
             "android.media_player.set_data_source"},
        {"isLooping", "()Z", false, false, "android.media_player.is_looping"},
        {"create", "(Landroid/content/Context;I)Landroid/media/MediaPlayer;",
             true, false, "android.media_player.create"},
        {"isPlaying", "()Z", false, false, "android.media_player.is_playing"},
            {"start", "()V", false, false, "android.media_player.start"},
            {"pause", "()V", false, false, "android.media_player.pause"},
            {"stop", "()V", false, false, "android.media_player.stop"},
        {"release", "()V", false, false, "android.media_player.release"},
        {"prepare", "()V", false, false, "android.media_player.prepare"},
        {"seekTo", "(I)V", false, false, "android.media_player.seek_to"},
            {"setLooping", "(Z)V", false, false,
             "android.media_player.set_looping"},
        {"setVolume", "(FF)V", false, false, "android.media_player.set_volume"},
            {"setOnCompletionListener",
         "(Landroid/media/MediaPlayer$OnCompletionListener;)V", false, false,
         "android.media_player.set_completion_listener"},
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
        {"getString", "(Ljava/lang/String;)Ljava/lang/String;", false, false,
         "android.bundle.get_string"},
        {"putString", "(Ljava/lang/String;Ljava/lang/String;)V", false, false,
         "android.bundle.put_string"},
        {"putInt", "(Ljava/lang/String;I)V", false, false,
         "android.bundle.put_int"},
        {"getLong", "(Ljava/lang/String;)J", false, false,
         "android.bundle.get_long"},
        {"putLong", "(Ljava/lang/String;J)V", false, false,
         "android.bundle.put_long"},
        {"getByteArray", "(Ljava/lang/String;)[B", false, false,
         "android.bundle.get_byte_array"},
        {"putByteArray", "(Ljava/lang/String;[B)V", false, false,
         "android.bundle.put_byte_array"},
        {"containsKey", "(Ljava/lang/String;)Z", false, false,
         "android.bundle.contains"},
        {"clear", "()V", false, false, "android.bundle.clear"},
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
        {"getBlockSize", "()I", false, false, "android.statfs.get_block_size"},
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
        {"onReceive", "(Landroid/content/Context;Landroid/content/Intent;)V",
         false, true, "android.receiver.on_receive_noop"},
        };
        catalog.push_back(std::move(receiver));
        Decl filter;
        filter.descriptor = "Landroid/content/IntentFilter;";
        filter.superclass = "Ljava/lang/Object;";
        filter.methods = {
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.intent_filter.init"},
        {"<init>", "()V", false, false, "android.intent_filter.init_empty"},
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
        {"<init>", "(Ljava/lang/String;Landroid/net/Uri;)V", false, false,
         "android.intent.init"},
        {"<init>", "(Landroid/content/Context;Ljava/lang/Class;)V", false,
         false, "android.intent.init_component"},
            {"setClassName",
             "(Ljava/lang/String;Ljava/lang/String;)"
             "Landroid/content/Intent;",
             false, false, "android.intent.set_class_name"},
            {"addFlags", "(I)Landroid/content/Intent;", false, false,
             "android.intent.set_flags"},
        {"putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;", false,
         false, "android.intent.put_extra_int"},
            {"putExtra",
             "(Ljava/lang/String;Ljava/lang/String;)"
             "Landroid/content/Intent;",
             false, false, "android.intent.put_extra_string"},
        {"getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;", false,
         false, "android.intent.get_string_extra"},
            {"getIntExtra", "(Ljava/lang/String;I)I", false, false,
             "android.intent.get_int_extra"},
        {"addCategory", "(Ljava/lang/String;)Landroid/content/Intent;", false,
         false, "android.intent.set_flags"},
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
        {"getAction", "()I", false, false, "android.motion_event.get_action"},
            {"getX", "()F", false, false, "android.motion_event.get_x"},
            {"getY", "()F", false, false, "android.motion_event.get_y"},
        {"getX", "(I)F", false, false, "android.motion_event.get_x_indexed"},
        {"getY", "(I)F", false, false, "android.motion_event.get_y_indexed"},
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
        {"<init>", "()V", false, false, "android.thread.init"},
            {"<init>", "(Ljava/lang/Runnable;)V", false, false,
             "android.thread.init_runnable"},
            {"start", "()V", false, false, "android.thread.start"},
            {"join", "()V", false, false, "android.thread.join"},
            {"isAlive", "()Z", false, false, "android.thread.is_alive"},
            {"currentThread", "()Ljava/lang/Thread;", true, false,
             "android.thread.current"},
            {"interrupt", "()V", false, false, "android.thread.interrupt"},
            {"isInterrupted", "()Z", false, false,
             "android.thread.is_interrupted"},
            {"interrupted", "()Z", true, false,
             "android.thread.clear_interrupted"},
            {"yield", "()V", true, false, "android.thread.yield"},
        {"getId", "()J", false, false, "android.thread.get_id"},
        {"getName", "()Ljava/lang/String;", false, false,
         "android.thread.get_name"},
        {"setName", "(Ljava/lang/String;)V", false, false,
         "android.thread.set_name"},
        {"setPriority", "(I)V", false, false, "android.thread.set_priority"},
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
        {"createFromPdu", "([B)Landroid/telephony/SmsMessage;", true, false,
         "android.sms.create_from_pdu"},
            {"getMessageBody", "()Ljava/lang/String;", false, false,
             "android.sms.get_message_body"},
            {"getOriginatingAddress", "()Ljava/lang/String;", false, false,
             "android.sms.get_originating_address"},
        };
        catalog.push_back(std::move(message));
    }
    {
        Decl encoder;
        encoder.descriptor = "Ljava/net/URLEncoder;";
        encoder.superclass = "Ljava/lang/Object;";
        encoder.methods = {
        {"encode", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
             true, false, "android.url_encoder.encode"},
        };
        catalog.push_back(std::move(encoder));
    }
  {
    Decl factory;
    factory.descriptor = "Ljavax/xml/parsers/SAXParserFactory;";
    factory.superclass = "Ljava/lang/Object;";
    factory.methods = {
        {"newInstance", "()Ljavax/xml/parsers/SAXParserFactory;", true, false,
         "android.sax.factory_instance"},
        {"newSAXParser", "()Ljavax/xml/parsers/SAXParser;", false, false,
         "android.sax.new_parser"},
    };
    catalog.push_back(std::move(factory));
    Decl parser;
    parser.descriptor = "Ljavax/xml/parsers/SAXParser;";
    parser.superclass = "Ljava/lang/Object;";
    parser.methods = {
        {"getXMLReader", "()Lorg/xml/sax/XMLReader;", false, false,
         "android.sax.get_reader"},
    };
    catalog.push_back(std::move(parser));
    Decl content_handler;
    content_handler.descriptor = "Lorg/xml/sax/ContentHandler;";
    content_handler.is_interface = true;
    catalog.push_back(std::move(content_handler));
    Decl reader;
    reader.descriptor = "Lorg/xml/sax/XMLReader;";
    reader.is_interface = true;
    reader.methods = {
        {"setContentHandler", "(Lorg/xml/sax/ContentHandler;)V", false, false,
         "android.sax.set_content_handler"},
        {"parse", "(Lorg/xml/sax/InputSource;)V", false, false,
         "android.sax.parse_unsupported"},
    };
    catalog.push_back(std::move(reader));
    Decl reader_impl;
    reader_impl.descriptor = "Lorg/xml/sax/XMLReader$Impl;";
    reader_impl.superclass = "Ljava/lang/Object;";
    reader_impl.interfaces = {"Lorg/xml/sax/XMLReader;"};
    reader_impl.methods = {
        {"setContentHandler", "(Lorg/xml/sax/ContentHandler;)V", false, false,
         "android.sax.set_content_handler"},
        {"parse", "(Lorg/xml/sax/InputSource;)V", false, false,
         "android.sax.parse_unsupported"},
    };
    catalog.push_back(std::move(reader_impl));
    Decl input_source;
    input_source.descriptor = "Lorg/xml/sax/InputSource;";
    input_source.superclass = "Ljava/lang/Object;";
    input_source.methods = {
        {"<init>", "(Ljava/io/InputStream;)V", false, false,
         "android.graphics.noop"},
    };
    catalog.push_back(std::move(input_source));
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
        {"setConnectTimeout", "(I)V", false, false, "android.net.unsupported"},
        };
        catalog.push_back(std::move(http));
        // TLS setup is local state and succeeds as truthful no-ops (trust
        // configuration has nothing to protect when no socket ever
        // opens); any actual connection stays an accounted failure.
        Decl ssl_context;
        ssl_context.descriptor = "Ljavax/net/ssl/SSLContext;";
        ssl_context.superclass = "Ljava/lang/Object;";
        ssl_context.methods = {
        {"getInstance", "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;", true,
         false, "android.ssl.context_instance"},
            {"init",
             "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;"
             "Ljava/security/SecureRandom;)V",
             false, false, "android.graphics.noop"},
        {"getSocketFactory", "()Ljavax/net/ssl/SSLSocketFactory;", false, false,
         "android.ssl.socket_factory"},
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
        {"setDefaultHostnameVerifier", "(Ljavax/net/ssl/HostnameVerifier;)V",
         true, false, "android.graphics.noop"},
        {"setDefaultSSLSocketFactory", "(Ljavax/net/ssl/SSLSocketFactory;)V",
         true, false, "android.graphics.noop"},
            {"setRequestMethod", "(Ljava/lang/String;)V", false, false,
             "android.net.unsupported"},
        {"setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V", false,
         false, "android.net.unsupported"},
        {"getResponseCode", "()I", false, false, "android.net.unsupported"},
            {"getInputStream", "()Ljava/io/InputStream;", false, false,
             "android.net.unsupported"},
        };
        catalog.push_back(std::move(https));
    }
}

}  // namespace ogplay::runtime::android_intrinsics

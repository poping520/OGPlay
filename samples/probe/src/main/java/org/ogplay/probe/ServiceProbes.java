package org.ogplay.probe;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.app.ActivityManager;
import android.app.AlarmManager;
import android.app.KeyguardManager;
import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.UiModeManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.res.Configuration;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.location.Location;
import android.location.LocationListener;
import android.media.MediaRouter;
import android.location.LocationManager;
import android.media.AudioManager;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.os.IBinder;
import android.os.PowerManager;
import android.os.Process;
import android.os.SystemClock;
import android.os.Vibrator;
import android.os.storage.StorageManager;
import android.view.inputmethod.InputMethodManager;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.Probe.obj;

final class ServiceProbes {
    private static final String GET_SERVICE = "Landroid/content/Context;.getSystemService(Ljava/lang/String;)Ljava/lang/Object;";
    private static final List<String> SCOPE_REVIEW = Arrays.asList(
            "account", "appops", "bluetooth", "camera", "consumer_ir", "country_detector", "device_policy",
            "download", "netpolicy", "print", "serial", "servicediscovery", "statusbar", "usb", "user", "wallpaper", "wifip2p");

    static void register(Map<String, Probe> probes) {
        for (final String[] service : ServiceRegistry.SERVICES) {
            final boolean infoOnly = SCOPE_REVIEW.contains(service[0]);
            add(probes, new Probe("services.registry." + service[0], infoOnly ? "info" : "in_scope", true, GET_SERVICE,
                    "Ljava/lang/Class;.isInstance(Ljava/lang/Object;)Z") {
                @Override Outcome run(ProbeSession session) throws Exception {
                    Object manager = session.activity.getSystemService(service[0]);
                    boolean matches = manager != null && Class.forName(service[1]).isInstance(manager);
                    JSONObject actual = obj("service", service[0], "class", manager == null ? null : manager.getClass().getName(),
                            "matches_api19_type", matches);
                    JSONObject expected = obj("api19_type", service[1], "present", true);
                    if (manager instanceof MediaRouter && "media_router".equals(service[0])) {
                        MediaRouter router = (MediaRouter) manager;
                        MediaRouter.RouteInfo route = router.getDefaultRoute();
                        actual.put("default_route", route != null
                                && route == router.getSelectedRoute(MediaRouter.ROUTE_TYPE_LIVE_AUDIO));
                        expected.put("default_route", true);
                        return check(matches && actual.getBoolean("default_route"), actual, expected,
                                "MediaRouter returns the default route");
                    }
                    String message = infoOnly ? "API 19 registry presence and type; compatibility scope needs review"
                            : "Context returns a manager assignable to the API 19 service type";
                    return infoOnly ? new Outcome("info", actual, expected, message, null) : check(matches, actual, expected, message);
                }
            });
        }
        coreServices(probes);
        deviceServices(probes);
        interactionServices(probes);
        ServiceSystemProbes.register(probes);
        ServiceDeviceProbes.register(probes);
        ServiceOtherProbes.register(probes);
    }

    private static void coreServices(Map<String, Probe> probes) {
        add(probes, new Probe("services.activity.process_tasks", "in_scope", false,
                "Landroid/app/ActivityManager;.getRunningAppProcesses()Ljava/util/List;",
                "Landroid/app/ActivityManager;.getRunningTasks(I)Ljava/util/List;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                ActivityManager am = (ActivityManager) session.activity.getSystemService(Context.ACTIVITY_SERVICE);
                List<ActivityManager.RunningAppProcessInfo> processes = am.getRunningAppProcesses();
                boolean own = false;
                JSONArray pids = new JSONArray();
                if (processes != null) for (ActivityManager.RunningAppProcessInfo process : processes) {
                    pids.put(process.pid);
                    own |= process.pid == Process.myPid() && session.activity.getPackageName().equals(process.processName);
                }
                List<ActivityManager.RunningTaskInfo> tasks = am.getRunningTasks(1);
                ComponentName top = tasks == null || tasks.isEmpty() ? null : tasks.get(0).topActivity;
                return check(own && session.activity.getComponentName().equals(top),
                        obj("pids", pids, "own_pid", Process.myPid(), "top", top == null ? null : top.flattenToShortString()),
                        obj("own_process_listed", true, "top", session.activity.getComponentName().flattenToShortString()),
                        "The foreground Activity and its process are visible to ActivityManager");
            }
        });
        add(probes, new Probe("services.audio.volume", "in_scope", true,
                "Landroid/media/AudioManager;.getStreamVolume(I)I", "Landroid/media/AudioManager;.getStreamMaxVolume(I)I",
                "Landroid/media/AudioManager;.setStreamVolume(III)V",
                "Landroid/media/AudioManager;.requestAudioFocus(Landroid/media/AudioManager$OnAudioFocusChangeListener;II)I",
                "Landroid/media/AudioManager;.abandonAudioFocus(Landroid/media/AudioManager$OnAudioFocusChangeListener;)I") {
            @Override Outcome run(ProbeSession session) throws Exception {
                AudioManager audio = (AudioManager) session.activity.getSystemService(Context.AUDIO_SERVICE);
                int stream = AudioManager.STREAM_MUSIC;
                int before = audio.getStreamVolume(stream);
                int max = audio.getStreamMaxVolume(stream);
                int target = before == max && max > 0 ? max - 1 : max;
                int readback;
                int request;
                int abandon;
                AudioManager.OnAudioFocusChangeListener listener = new AudioManager.OnAudioFocusChangeListener() {
                    @Override public void onAudioFocusChange(int change) {}
                };
                try {
                    audio.setStreamVolume(stream, target, 0);
                    readback = audio.getStreamVolume(stream);
                    request = audio.requestAudioFocus(listener, stream, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT);
                    abandon = audio.abandonAudioFocus(listener);
                } finally {
                    audio.setStreamVolume(stream, before, 0);
                }
                return check(before >= 0 && before <= max && max > 0 && readback == target
                                && request == AudioManager.AUDIOFOCUS_REQUEST_GRANTED,
                        obj("initial_volume", before, "max_volume", max, "readback", readback,
                                "focus_result", request, "abandon_result", abandon),
                        obj("initial_within_range", true, "readback", target, "focus_result", AudioManager.AUDIOFOCUS_REQUEST_GRANTED),
                        "Music stream volume round-trips and transient focus can be acquired and released");
            }
        });
        add(probes, new Probe("services.power.wakelock", "in_scope", false,
                "Landroid/os/PowerManager;.isScreenOn()Z",
                "Landroid/os/PowerManager;.newWakeLock(ILjava/lang/String;)Landroid/os/PowerManager$WakeLock;",
                "Landroid/os/PowerManager$WakeLock;.acquire()V", "Landroid/os/PowerManager$WakeLock;.release()V",
                "Landroid/os/PowerManager$WakeLock;.isHeld()Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                PowerManager power = (PowerManager) session.activity.getSystemService(Context.POWER_SERVICE);
                boolean screen = power.isScreenOn();
                PowerManager.WakeLock wake = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "OGProbe");
                boolean initial = wake.isHeld();
                wake.acquire();
                boolean held;
                try { held = wake.isHeld(); } finally { wake.release(); }
                boolean released = !wake.isHeld();
                return check(screen && !initial && held && released,
                        obj("screen_on", screen, "initially_held", initial, "after_acquire", held, "after_release", !released),
                        obj("screen_on", true, "initially_held", false, "after_acquire", true, "after_release", false),
                        "A foreground screen is on and the local wake lock follows acquire/release");
            }
        });
        add(probes, new Probe("services.binder.cross_process", "expected_reject", true,
                "Landroid/content/Context;.bindService(Landroid/content/Intent;Landroid/content/ServiceConnection;I)Z",
                "Landroid/content/Context;.unbindService(Landroid/content/ServiceConnection;)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                final boolean[] connected = { false };
                ServiceConnection connection = new ServiceConnection() {
                    @Override public void onServiceConnected(ComponentName name, IBinder service) { connected[0] = true; }
                    @Override public void onServiceDisconnected(ComponentName name) {}
                };
                Intent intent = new Intent(session.activity, RemoteProbeService.class);
                boolean bound;
                try {
                    bound = session.activity.bindService(intent, connection, Context.BIND_AUTO_CREATE);
                } catch (Throwable failure) {
                    return new Outcome("fail", obj("exception_class", failure.getClass().getName()), obj("bound", false),
                            "Binding the separate-process component should return false", failure);
                }
                session.activity.unbindService(connection);
                return check(!bound && !connected[0], obj("bound", bound, "connected", connected[0]),
                        obj("bound", false, "connected", false), "An explicitly separate service process is rejected");
            }
        });
    }

    private static void deviceServices(Map<String, Probe> probes) {
        add(probes, new Probe("services.vibrator.present", "info", false, "Landroid/os/Vibrator;.hasVibrator()Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Vibrator vibrator = (Vibrator) session.activity.getSystemService(Context.VIBRATOR_SERVICE);
                return info(obj("present", vibrator.hasVibrator()), "Vibrator hardware presence is an observation");
            }
        });
        add(probes, new Probe("services.vibrator.request", "in_scope", false,
                "Landroid/os/Vibrator;.vibrate(J)V", "Landroid/os/Vibrator;.cancel()V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Vibrator vibrator = (Vibrator) session.activity.getSystemService(Context.VIBRATOR_SERVICE);
                try { vibrator.vibrate(10); } finally { vibrator.cancel(); }
                return check(true, obj("request_and_cancel_returned", true), obj("request_and_cancel_returned", true),
                        "A short vibration request and cancellation return normally");
            }
        });
        add(probes, new Probe("services.connectivity.state", "info", false,
                "Landroid/net/ConnectivityManager;.getActiveNetworkInfo()Landroid/net/NetworkInfo;",
                "Landroid/net/ConnectivityManager;.getAllNetworkInfo()[Landroid/net/NetworkInfo;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                ConnectivityManager cm = (ConnectivityManager) session.activity.getSystemService(Context.CONNECTIVITY_SERVICE);
                NetworkInfo active = cm.getActiveNetworkInfo();
                JSONArray networks = new JSONArray();
                NetworkInfo[] items = cm.getAllNetworkInfo();
                if (items != null) for (NetworkInfo item : items) networks.put(network(item));
                return info(obj("active", network(active), "networks", networks), "Network state is recorded without attempting transport");
            }
        });
        add(probes, new Probe("services.connectivity.consistency", "in_scope", false,
                "Landroid/net/ConnectivityManager;.getActiveNetworkInfo()Landroid/net/NetworkInfo;",
                "Landroid/net/ConnectivityManager;.getAllNetworkInfo()[Landroid/net/NetworkInfo;",
                "Landroid/net/NetworkInfo;.isConnected()Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                ConnectivityManager cm = (ConnectivityManager) session.activity.getSystemService(Context.CONNECTIVITY_SERVICE);
                NetworkInfo active = cm.getActiveNetworkInfo();
                boolean listed = false;
                boolean anyConnected = false;
                NetworkInfo[] items = cm.getAllNetworkInfo();
                if (items != null) for (NetworkInfo item : items) {
                    anyConnected |= item.isConnected();
                    listed |= active != null && item.getType() == active.getType() && item.isConnected() == active.isConnected();
                }
                return check(active == null ? !anyConnected : listed,
                        obj("active", network(active), "any_connected", anyConnected, "active_is_listed", listed),
                        obj("active_matches_inventory", true), "The active network agrees with the network inventory");
            }
        });
        add(probes, new Probe("services.wifi.connection", "info", false,
                "Landroid/net/wifi/WifiManager;.getConnectionInfo()Landroid/net/wifi/WifiInfo;",
                "Landroid/net/wifi/WifiManager;.isWifiEnabled()Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                WifiManager wifi = (WifiManager) session.activity.getSystemService(Context.WIFI_SERVICE);
                return info(obj("enabled", wifi.isWifiEnabled(), "connection", wifi(wifi.getConnectionInfo())),
                        "Wi-Fi association facts are recorded without changing Wi-Fi state");
            }
        });
        add(probes, new Probe("services.wifi.consistency", "in_scope", false,
                "Landroid/net/wifi/WifiManager;.getConnectionInfo()Landroid/net/wifi/WifiInfo;",
                "Landroid/net/ConnectivityManager;.getNetworkInfo(I)Landroid/net/NetworkInfo;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                WifiManager wifi = (WifiManager) session.activity.getSystemService(Context.WIFI_SERVICE);
                ConnectivityManager cm = (ConnectivityManager) session.activity.getSystemService(Context.CONNECTIVITY_SERVICE);
                boolean enabled = wifi.isWifiEnabled();
                WifiInfo info = wifi.getConnectionInfo();
                NetworkInfo network = cm.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
                boolean connected = network != null && network.isConnected();
                return check(!connected || enabled && info != null && info.getNetworkId() >= 0,
                        obj("enabled", enabled, "wifi", wifi(info), "network", network(network)),
                        obj("connected_requires_enabled_association", true),
                        "A connected Wi-Fi network requires Wi-Fi to be enabled with a valid association");
            }
        });
        add(probes, new Probe("services.location.providers", "info", false,
                "Landroid/location/LocationManager;.getProviders(Z)Ljava/util/List;",
                "Landroid/location/LocationManager;.isProviderEnabled(Ljava/lang/String;)Z",
                "Landroid/location/LocationManager;.getLastKnownLocation(Ljava/lang/String;)Landroid/location/Location;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                LocationManager locations = (LocationManager) session.activity.getSystemService(Context.LOCATION_SERVICE);
                JSONArray rows = new JSONArray();
                for (String name : locations.getProviders(false)) {
                    rows.put(obj("provider", name, "enabled", locations.isProviderEnabled(name),
                            "has_last_known_location", locations.getLastKnownLocation(name) != null));
                }
                return info(obj("providers", rows), "Provider availability, enabled state and cached location presence");
            }
        });
        add(probes, new Probe("services.location.subscription", "in_scope", true,
                "Landroid/location/LocationManager;.requestLocationUpdates(Ljava/lang/String;JFLandroid/location/LocationListener;)V",
                "Landroid/location/LocationManager;.removeUpdates(Landroid/location/LocationListener;)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                LocationManager locations = (LocationManager) session.activity.getSystemService(Context.LOCATION_SERVICE);
                List<String> names = locations.getProviders(false);
                if (names == null || names.isEmpty()) return skip("The device exposes no location provider");
                String provider = names.contains(LocationManager.PASSIVE_PROVIDER) ? LocationManager.PASSIVE_PROVIDER : names.get(0);
                LocationListener listener = new LocationListener() {
                    @Override public void onLocationChanged(Location location) {}
                    @Override public void onStatusChanged(String provider, int status, Bundle extras) {}
                    @Override public void onProviderEnabled(String provider) {}
                    @Override public void onProviderDisabled(String provider) {}
                };
                locations.requestLocationUpdates(provider, 1000, 0, listener);
                locations.removeUpdates(listener);
                return check(true, obj("provider", provider, "registered_and_removed", true),
                        obj("registered_and_removed", true), "A listener can be registered and removed for an advertised provider");
            }
        });
        add(probes, new Probe("services.sensor.accelerometer", "in_scope", false,
                "Landroid/hardware/SensorManager;.getDefaultSensor(I)Landroid/hardware/Sensor;",
                "Landroid/hardware/SensorManager;.registerListener(Landroid/hardware/SensorEventListener;Landroid/hardware/Sensor;I)Z",
                "Landroid/hardware/SensorManager;.unregisterListener(Landroid/hardware/SensorEventListener;)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                SensorManager sensors = (SensorManager) session.activity.getSystemService(Context.SENSOR_SERVICE);
                Sensor accelerometer = sensors.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
                if (accelerometer == null) return skip("The sensor inventory has no accelerometer");
                final float[] reading = new float[3];
                final CountDownLatch latch = new CountDownLatch(1);
                SensorEventListener callback = new SensorEventListener() {
                    @Override public void onAccuracyChanged(Sensor sensor, int accuracy) {}
                    @Override public void onSensorChanged(SensorEvent event) {
                        if (event.values.length >= 3 && latch.getCount() != 0) {
                            System.arraycopy(event.values, 0, reading, 0, 3);
                            latch.countDown();
                        }
                    }
                };
                boolean registered = sensors.registerListener(callback, accelerometer, SensorManager.SENSOR_DELAY_GAME);
                boolean received;
                try { received = registered && latch.await(1, TimeUnit.SECONDS); }
                finally { sensors.unregisterListener(callback); }
                JSONArray sample = new JSONArray();
                boolean finite = received;
                if (received) for (float value : reading) {
                    boolean valid = !Float.isInfinite(value) && !Float.isNaN(value);
                    finite &= valid;
                    sample.put(valid ? value : JSONObject.NULL);
                }
                return check(registered && finite, obj("registered", registered, "received", received, "xyz", sample),
                        obj("registered", true, "received_within_ms", 1000, "finite_xyz", true),
                        "An advertised accelerometer emits finite samples after registration");
            }
        });
    }

    private static void interactionServices(Map<String, Probe> probes) {
        add(probes, new Probe("services.clipboard.round_trip", "in_scope", true,
                "Landroid/content/ClipboardManager;.setPrimaryClip(Landroid/content/ClipData;)V",
                "Landroid/content/ClipboardManager;.getPrimaryClip()Landroid/content/ClipData;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                ClipboardManager clipboard = (ClipboardManager) session.activity.getSystemService(Context.CLIPBOARD_SERVICE);
                ClipData previous = clipboard.getPrimaryClip();
                String expected = "OGPlay Probe ¦ 19";
                String observed;
                try {
                    clipboard.setPrimaryClip(ClipData.newPlainText("Probe", expected));
                    ClipData next = clipboard.getPrimaryClip();
                    observed = next == null || next.getItemCount() == 0 ? null : String.valueOf(next.getItemAt(0).getText());
                } finally {
                    if (previous != null) clipboard.setPrimaryClip(previous);
                }
                return check(expected.equals(observed), obj("text", observed), obj("text", expected),
                        "Plain-text clipboard content survives a write and read");
            }
        });
        add(probes, new Probe("services.notification.post_cancel", "in_scope", true,
                "Landroid/app/NotificationManager;.notify(ILandroid/app/Notification;)V",
                "Landroid/app/NotificationManager;.cancel(I)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                NotificationManager manager = (NotificationManager) session.activity.getSystemService(Context.NOTIFICATION_SERVICE);
                Notification notification = new Notification.Builder(session.activity)
                        .setContentTitle(session.activity.getText(R.string.app_name))
                        .setSmallIcon(android.R.drawable.ic_dialog_info).build();
                manager.notify(19, notification);
                manager.cancel(19);
                return check(true, obj("posted_and_cancelled", true), obj("posted_and_cancelled", true),
                        "Notification posting and cancellation return normally");
            }
        });
        add(probes, new Probe("services.alarm.set_cancel", "in_scope", true,
                "Landroid/app/AlarmManager;.set(IJLandroid/app/PendingIntent;)V",
                "Landroid/app/AlarmManager;.cancel(Landroid/app/PendingIntent;)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                AlarmManager alarm = (AlarmManager) session.activity.getSystemService(Context.ALARM_SERVICE);
                PendingIntent intent = PendingIntent.getActivity(session.activity, 19, new Intent(session.activity, ProbeActivity.class), 0);
                try {
                    alarm.set(AlarmManager.ELAPSED_REALTIME, SystemClock.elapsedRealtime() + 600000, intent);
                    alarm.cancel(intent);
                } finally {
                    intent.cancel();
                }
                return check(true, obj("set_and_cancelled", true), obj("set_and_cancelled", true),
                        "Scheduling a future PendingIntent and cancelling it return normally");
            }
        });
        add(probes, new Probe("services.keyguard.state", "info", false,
                "Landroid/app/KeyguardManager;.inKeyguardRestrictedInputMode()Z",
                "Landroid/app/KeyguardManager;.isKeyguardLocked()Z", "Landroid/app/KeyguardManager;.isKeyguardSecure()Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                KeyguardManager manager = (KeyguardManager) session.activity.getSystemService(Context.KEYGUARD_SERVICE);
                return info(obj("restricted_input", manager.inKeyguardRestrictedInputMode(), "locked", manager.isKeyguardLocked(),
                        "secure", manager.isKeyguardSecure()), "Keyguard state in the current environment");
            }
        });
        add(probes, new Probe("services.uimode.configuration", "in_scope", false,
                "Landroid/app/UiModeManager;.getCurrentModeType()I", "Landroid/app/UiModeManager;.getNightMode()I") {
            @Override Outcome run(ProbeSession session) throws Exception {
                UiModeManager manager = (UiModeManager) session.activity.getSystemService(Context.UI_MODE_SERVICE);
                int type = manager.getCurrentModeType();
                int uiMode = session.activity.getResources().getConfiguration().uiMode;
                int config = uiMode & Configuration.UI_MODE_TYPE_MASK;
                return new Checks().require("manager_matches_configuration", type == config)
                        .require("normal_phone_mode",
                                uiMode == (Configuration.UI_MODE_TYPE_NORMAL | Configuration.UI_MODE_NIGHT_NO))
                        .outcome(obj("manager_type", type, "configuration_type", config, "ui_mode", uiMode,
                                        "night_mode", manager.getNightMode()),
                                obj("manager_matches_configuration", true, "ui_mode", 0x11),
                                "UiModeManager and Resources describe a phone in day mode");
            }
        });
        add(probes, new Probe("services.input_method.state", "info", true,
                "Landroid/view/inputmethod/InputMethodManager;.getEnabledInputMethodList()Ljava/util/List;",
                "Landroid/view/inputmethod/InputMethodManager;.isActive()Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                InputMethodManager manager = (InputMethodManager) session.activity.getSystemService(Context.INPUT_METHOD_SERVICE);
                List<?> enabled = manager.getEnabledInputMethodList();
                return info(obj("active", manager.isActive(), "enabled_count", enabled == null ? null : enabled.size()),
                        "Input method state for the foreground application");
            }
        });
        add(probes, new Probe("services.storage.obb_state", "info", false,
                "Landroid/os/storage/StorageManager;.isObbMounted(Ljava/lang/String;)Z",
                "Landroid/os/storage/StorageManager;.getMountedObbPath(Ljava/lang/String;)Ljava/lang/String;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                StorageManager manager = (StorageManager) session.activity.getSystemService(Context.STORAGE_SERVICE);
                String path = new java.io.File(session.activity.getObbDir(), "unmounted.obb").getAbsolutePath();
                return info(obj("mounted", manager.isObbMounted(path), "mounted_path", manager.getMountedObbPath(path)),
                        "StorageManager observes an unused OBB path");
            }
        });
        add(probes, new Probe("services.account.inventory", "info", false,
                "Landroid/accounts/AccountManager;.getAccounts()[Landroid/accounts/Account;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                AccountManager manager = (AccountManager) session.activity.getSystemService(Context.ACCOUNT_SERVICE);
                Account[] accounts = manager.getAccounts();
                return info(obj("account_count", accounts == null ? null : accounts.length),
                        "Account availability is observed without recording account names");
            }
        });
    }

    private static Object network(NetworkInfo value) throws JSONException {
        return value == null ? null : obj("type", value.getType(), "connected", value.isConnected(),
                "state", String.valueOf(value.getState()), "available", value.isAvailable());
    }

    private static Object wifi(WifiInfo value) throws JSONException {
        return value == null ? null : obj("network_id", value.getNetworkId(),
                "supplicant_state", String.valueOf(value.getSupplicantState()), "ssid", value.getSSID());
    }

    static void environment(String id, Object value, Map<String, JSONObject> groups) {
        if (value instanceof JSONObject) {
            if ("services.wifi.connection".equals(id)) groups.put("wifi", (JSONObject) value);
            if ("device.identity.telephony".equals(id)) groups.put("telephony", (JSONObject) value);
        }
    }
}

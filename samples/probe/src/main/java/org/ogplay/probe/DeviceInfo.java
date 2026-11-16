package org.ogplay.probe;

import android.app.ActivityManager;
import android.content.Context;
import android.content.res.Configuration;
import android.hardware.Sensor;
import android.hardware.SensorManager;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Environment;
import android.os.SystemClock;
import android.telephony.TelephonyManager;
import android.text.format.DateFormat;
import android.util.DisplayMetrics;
import android.view.Display;
import java.util.ArrayList;
import java.util.Locale;
import java.util.TimeZone;
import org.json.JSONArray;
import org.json.JSONObject;

/** Read-only device snapshot, independent of probe execution and report environment. */
final class DeviceInfo {
    static final int TEXT = 0, BYTES = 1, BOOLEAN = 2, MHZ = 3, DURATION = 4, GLES = 5;
    static final class Row {
        final int label, format;
        final Object value;
        Row(int label, Object value, int format) {
            this.label = label;
            this.value = value;
            this.format = format;
        }
    }
    static final class Section {
        final String key;
        final int title;
        final ArrayList<Row> rows = new ArrayList<Row>();
        final JSONObject raw = new JSONObject();
        Throwable failure;
        String[] extensions;
        Section(String key, int title) { this.key = key; this.title = title; }
        void add(String key, int label, Object value) throws Exception { add(key, label, value, TEXT); }
        void add(String key, int label, Object value, int format) throws Exception {
            raw.put(key, value == null ? JSONObject.NULL : value);
            rows.add(new Row(label, value, format));
        }
    }
    final ArrayList<Section> sections = new ArrayList<Section>();
    final JSONObject raw = new JSONObject();

    static DeviceInfo collect(ProbeActivity activity) {
        DeviceInfo snapshot = new DeviceInfo();
        String[] keys = {"device", "cpu", "memory", "display", "storage", "graphics", "sensors", "locale_time", "network_phone"};
        int[] titles = {R.string.device, R.string.cpu_info, R.string.memory_info, R.string.display_info,
                R.string.storage, R.string.gl_info, R.string.sensor_info, R.string.locale_time, R.string.network_phone};
        for (int i = 0; i < keys.length; i++) {
            Section section = new Section(keys[i], titles[i]);
            snapshot.sections.add(section);
            try {
                read(activity, section, i);
            } catch (Throwable failure) {
                section.failure = failure;
                android.util.Log.w("OGProbeDeviceInfo", section.key + ": " + failure.toString());
                try {
                    section.raw.put("error_class", failure.getClass().getName());
                    section.raw.put("error_message", failure.getMessage());
                } catch (Exception ignored) { /* A section failure must not discard the other sections. */ }
            }
            try { snapshot.raw.put(section.key, section.raw); } catch (Exception ignored) { }
        }
        return snapshot;
    }

    private static void read(ProbeActivity a, Section s, int index) throws Exception {
        switch (index) {
            case 0:
                s.add("model", R.string.model, Build.MODEL);
                s.add("manufacturer", R.string.manufacturer, Build.MANUFACTURER);
                s.add("brand", R.string.brand, Build.BRAND);
                s.add("device", R.string.device_code, Build.DEVICE);
                s.add("android", R.string.android_version, Build.VERSION.RELEASE + " / API " + Build.VERSION.SDK_INT);
                s.add("fingerprint", R.string.fingerprint, Build.FINGERPRINT);
                s.add("serial", R.string.serial, Build.SERIAL);
                s.add("android_id", R.string.android_id, DeviceProbes.androidId(a));
                break;
            case 1:
                String proc = PhaseOne.text("/proc/cpuinfo");
                s.add("processor", R.string.processor, DeviceProbes.procField(proc, "Processor"));
                s.add("abi", R.string.abi, Build.CPU_ABI);
                s.add("abi2", R.string.abi2, Build.CPU_ABI2);
                s.add("architecture", R.string.architecture, System.getProperty("os.arch"));
                s.add("java_cores", R.string.java_cores, Runtime.getRuntime().availableProcessors());
                NativeProbe.load();
                s.add("sysconf_cores", R.string.sysconf_cores, NativeProbe.cpuCount(true));
                String base = "/sys/devices/system/cpu/cpu0/cpufreq/";
                s.add("maximum_khz", R.string.maximum_frequency, Long.parseLong(PhaseOne.text(base + "cpuinfo_max_freq").trim()), MHZ);
                s.add("current_khz", R.string.current_frequency, Long.parseLong(PhaseOne.text(base + "scaling_cur_freq").trim()), MHZ);
                String features = DeviceProbes.procField(proc, "Features");
                s.add("features", R.string.features, features == null ? null : features.trim().replaceAll("\\s+", "\n"));
                break;
            case 2:
                ActivityManager manager = (ActivityManager) a.getSystemService(Context.ACTIVITY_SERVICE);
                ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
                manager.getMemoryInfo(memory);
                s.add("total_bytes", R.string.total_memory, memory.totalMem, BYTES);
                s.add("available_bytes", R.string.available_memory, memory.availMem, BYTES);
                s.add("threshold_bytes", R.string.memory_threshold, memory.threshold, BYTES);
                s.add("low_memory", R.string.low_memory, memory.lowMemory, BOOLEAN);
                s.add("heap_bytes", R.string.heap_limit, manager.getMemoryClass() * 1048576L, BYTES);
                s.add("large_heap_bytes", R.string.large_heap_limit, manager.getLargeMemoryClass() * 1048576L, BYTES);
                break;
            case 3:
                Display display = a.getWindowManager().getDefaultDisplay();
                DisplayMetrics metrics = new DisplayMetrics();
                display.getRealMetrics(metrics);
                s.add("resolution", R.string.resolution, metrics.widthPixels + " × " + metrics.heightPixels + " px");
                s.add("density_dpi", R.string.density_dpi, metrics.densityDpi + " dpi (" + densityBucket(metrics.densityDpi) + ")");
                s.add("scaled_density", R.string.scaled_density, metrics.scaledDensity);
                double inches = metrics.xdpi > 0 && metrics.ydpi > 0 ? Math.sqrt(
                        Math.pow(metrics.widthPixels / metrics.xdpi, 2) + Math.pow(metrics.heightPixels / metrics.ydpi, 2)) : 0;
                s.add("physical_inches", R.string.physical_size, inches > 0 ? String.format(Locale.US, "%.1f\u2033", inches) : null);
                s.add("refresh_hz", R.string.refresh_rate, String.format(Locale.US, "%.1f Hz", display.getRefreshRate()));
                int orientation = a.getResources().getConfiguration().orientation;
                s.add("orientation", R.string.orientation, a.getString(orientation == Configuration.ORIENTATION_LANDSCAPE
                        ? R.string.landscape : orientation == Configuration.ORIENTATION_PORTRAIT ? R.string.portrait : R.string.unknown));
                break;
            case 4:
                JSONObject internal = DeviceProbes.stat(Environment.getDataDirectory());
                s.add("internal_total_bytes", R.string.internal_total, internal.getLong("total_bytes"), BYTES);
                s.add("internal_available_bytes", R.string.internal_available, internal.getLong("available_bytes"), BYTES);
                s.add("external_state", R.string.external_state, Environment.getExternalStorageState());
                JSONObject external = DeviceProbes.stat(Environment.getExternalStorageDirectory());
                s.add("external_total_bytes", R.string.external_total, external.getLong("total_bytes"), BYTES);
                s.add("external_available_bytes", R.string.external_available, external.getLong("available_bytes"), BYTES);
                break;
            case 5:
                int declared = ((ActivityManager) a.getSystemService(Context.ACTIVITY_SERVICE)).getDeviceConfigurationInfo().reqGlEsVersion;
                // inspect owns the pbuffer/context and destroys both in its finally block.
                Probe.Outcome outcome = GlInfo.inspect(declared);
                JSONObject gl = (JSONObject) outcome.observed;
                if (gl.has("call")) throw new IllegalStateException(gl.optString("call") + ": EGL " + gl.optInt("egl_error"));
                if (!gl.has("vendor")) throw new IllegalStateException(outcome.message);
                s.add("vendor", R.string.gl_vendor, gl.opt("vendor"));
                s.add("renderer", R.string.gl_renderer, gl.opt("renderer"));
                s.add("version", R.string.gl_version, gl.opt("version"));
                s.add("max_texture_size", R.string.max_texture_size, gl.optInt("max_texture_size") + " px");
                s.add("req_gles_version", R.string.req_gles_version, declared, GLES);
                String extensions = gl.optString("extensions", "").trim();
                s.extensions = extensions.length() == 0 ? new String[0] : extensions.split("\\s+");
                s.raw.put("extensions", new JSONArray(java.util.Arrays.asList(s.extensions)));
                break;
            case 6:
                SensorManager sensors = (SensorManager) a.getSystemService(Context.SENSOR_SERVICE);
                JSONArray inventory = new JSONArray();
                for (Sensor sensor : sensors.getSensorList(Sensor.TYPE_ALL)) {
                    inventory.put(Probe.obj("name", sensor.getName(), "type", sensor.getType(), "vendor", sensor.getVendor()));
                    s.rows.add(new Row(R.string.sensor, a.getString(R.string.sensor_description,
                            present(sensor.getName()), sensor.getType(), present(sensor.getVendor())), TEXT));
                }
                s.raw.put("inventory", inventory);
                if (inventory.length() == 0) s.rows.add(new Row(R.string.sensor, a.getString(R.string.none), TEXT));
                break;
            case 7:
                s.add("locale", R.string.locale_value, Locale.getDefault().toString());
                TimeZone zone = TimeZone.getDefault();
                s.add("timezone_id", R.string.timezone_id, zone.getID());
                int minutes = zone.getOffset(System.currentTimeMillis()) / 60000;
                s.add("timezone_offset", R.string.timezone_offset, String.format(Locale.US, "UTC%s%02d:%02d",
                        minutes < 0 ? "-" : "+", Math.abs(minutes / 60), Math.abs(minutes % 60)));
                s.add("24_hour", R.string.hour_24, DateFormat.is24HourFormat(a), BOOLEAN);
                s.add("uptime_ms", R.string.uptime, SystemClock.uptimeMillis(), DURATION);
                s.add("elapsed_realtime_ms", R.string.elapsed_realtime, SystemClock.elapsedRealtime(), DURATION);
                break;
            case 8:
                ConnectivityManager connectivity = (ConnectivityManager) a.getSystemService(Context.CONNECTIVITY_SERVICE);
                NetworkInfo active = connectivity.getActiveNetworkInfo();
                s.add("connected", R.string.network_connected, active != null && active.isConnected(), BOOLEAN);
                s.add("network_type", R.string.network_type, active == null ? null : active.getTypeName());
                WifiManager wifi = (WifiManager) a.getSystemService(Context.WIFI_SERVICE);
                s.add("wifi_enabled", R.string.wifi_enabled, wifi.isWifiEnabled(), BOOLEAN);
                TelephonyManager phone = (TelephonyManager) a.getSystemService(Context.TELEPHONY_SERVICE);
                s.add("phone_type", R.string.phone_type, phone.getPhoneType());
                s.add("sim_state", R.string.sim_state, phone.getSimState());
                s.add("network_operator", R.string.network_operator, phone.getNetworkOperatorName());
                s.add("roaming", R.string.roaming, phone.isNetworkRoaming(), BOOLEAN);
                break;
            default: throw new IllegalArgumentException("Unknown device section");
        }
    }

    static String present(Object value) {
        return value == null || value == JSONObject.NULL || String.valueOf(value).trim().length() == 0 ? "—" : String.valueOf(value);
    }

    private static String densityBucket(int dpi) {
        if (dpi <= 120) return "ldpi";
        if (dpi <= 160) return "mdpi";
        if (dpi <= 213) return "tvdpi";
        if (dpi <= 240) return "hdpi";
        if (dpi <= 320) return "xhdpi";
        if (dpi <= 480) return "xxhdpi";
        return "xxxhdpi";
    }
}

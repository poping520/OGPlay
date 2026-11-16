package org.ogplay.probe;

import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.FeatureInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.content.res.Configuration;
import android.hardware.Sensor;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.Environment;
import android.os.StatFs;
import android.os.SystemClock;
import android.provider.Settings;
import android.telephony.TelephonyManager;
import android.view.Choreographer;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.Locale;
import java.util.Map;
import java.util.TimeZone;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.zip.ZipFile;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.PhaseOne.text;
import static org.ogplay.probe.Probe.obj;

final class DeviceProbes {
    static final String IDENTITY = "device.identity";
    private static final String PROP = "Landroid/os/SystemProperties;.get(Ljava/lang/String;)Ljava/lang/String;";

    static void register(Map<String, Probe> probes) {
        buildAndCpu(probes);
        memoryAndTime(probes);
        installation(probes);
        features(probes);
        add(probes, new Probe("device.gl.info", "in_scope", false,
                "Ljavax/microedition/khronos/egl/EGL10;.eglCreatePbufferSurface(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;[I)Ljavax/microedition/khronos/egl/EGLSurface;",
                "Landroid/opengl/GLES20;.glGetString(I)Ljava/lang/String;",
                "Landroid/opengl/GLES20;.glGetIntegerv(I[II)V",
                "Landroid/app/ActivityManager;.getDeviceConfigurationInfo()Landroid/content/pm/ConfigurationInfo;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                return GlInfo.inspect(manager(session).getDeviceConfigurationInfo().reqGlEsVersion);
            }
        });
    }

    private static void buildAndCpu(Map<String, Probe> probes) {
        add(probes, new Probe("device.build.sysprop_consistency", "in_scope", false, PROP) {
            @Override Outcome run(ProbeSession session) throws Exception {
                String[] keys = {"ro.product.model", "ro.product.manufacturer", "ro.product.brand", "ro.product.device",
                        "ro.build.version.sdk", "ro.build.version.release", "ro.serialno"};
                String[] fields = {Build.MODEL, Build.MANUFACTURER, Build.BRAND, Build.DEVICE,
                        String.valueOf(Build.VERSION.SDK_INT), Build.VERSION.RELEASE, Build.SERIAL};
                JSONObject build = new JSONObject();
                JSONObject properties = new JSONObject();
                boolean ok = true;
                for (int i = 0; i < keys.length; i++) {
                    String actual = prop(keys[i]);
                    build.put(keys[i], fields[i]);
                    properties.put(keys[i], actual);
                    // Build.getString uses UNKNOWN for an empty ro.* value.
                    String mapped = actual.length() == 0 && i != 4 ? Build.UNKNOWN : actual;
                    ok &= mapped.equals(fields[i]);
                }
                return check(ok, obj("fields", build, "properties", properties),
                        obj("same_after_build_unknown_fallback", true), "Build fields are derived from their API 19 ro.* properties");
            }
        });
        add(probes, new Probe("device.cpu.abi_consistency", "in_scope", false, PROP,
                "Ljava/lang/System;.getProperty(Ljava/lang/String;)Ljava/lang/String;", "uname") {
            @Override Outcome run(ProbeSession session) throws Exception {
                NativeProbe.load();
                String machine = NativeProbe.machine();
                String arch = System.getProperty("os.arch");
                String abi = prop("ro.product.cpu.abi");
                String abi2 = prop("ro.product.cpu.abi2");
                String proc = text("/proc/cpuinfo");
                String processor = procField(proc, "Processor");
                String features = procField(proc, "Features");
                boolean arm = "armeabi-v7a".equals(abi) || "armeabi".equals(abi);
                boolean armMachine = machine != null && machine.startsWith("arm");
                boolean armArch = arch != null && arch.startsWith("arm");
                boolean armProc = processor != null && processor.toLowerCase(Locale.US).contains("arm");
                boolean featureLine = features != null && features.trim().length() > 0;
                boolean v7 = "armeabi-v7a".equals(abi);
                return new Checks()
                        .require("primary_property_is_arm_abi", arm)
                        .require("uname_reports_arm", armMachine)
                        .require("os_arch_reports_arm", armArch)
                        .require("proc_processor_reports_arm", armProc)
                        .require("proc_features_present", featureLine)
                        .require("uname_reports_armv7_when_required", !v7 || machine != null && machine.startsWith("armv7"))
                        .require("proc_processor_reports_armv7_when_required",
                                !v7 || processor != null && processor.toLowerCase(Locale.US).contains("v7"))
                        .require("build_primary_abi_matches_property", abi.equals(Build.CPU_ABI))
                        .require("build_secondary_abi_matches_property",
                                (abi2.length() == 0 ? Build.UNKNOWN : abi2).equals(Build.CPU_ABI2))
                        .outcome(obj("build_abi", Build.CPU_ABI, "build_abi2", Build.CPU_ABI2, "property_abi", abi,
                                "property_abi2", abi2, "arch", arch, "machine", machine, "processor", processor,
                                "features", features),
                        obj("same_arm_family", true, "abi_properties_match_build", true, "features_present", true),
                        "Build, properties, Java, procfs and uname identify the same ARM family");
            }
        });
        add(probes, new Probe("device.cpu.freq", "info", false, "Ljava/io/FileInputStream;.<init>(Ljava/io/File;)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                JSONObject values = new JSONObject();
                int expectedKhz = session.expect.optInt("cpu_frequency_mhz", 0) * 1000;
                String base = "/sys/devices/system/cpu/cpu0/cpufreq/";
                boolean match = true;
                int readable = 0;
                for (String name : new String[] {"cpuinfo_max_freq", "scaling_cur_freq"}) {
                    try {
                        int khz = Integer.parseInt(text(base + name).trim());
                        values.put(name, khz);
                        match &= khz == expectedKhz;
                        readable++;
                    } catch (IOException missing) {
                        values.put(name, JSONObject.NULL);
                    }
                }
                return new Outcome("info", obj("khz", values, "matches_config", expectedKhz > 0 && readable > 0 ? match : null),
                        obj("khz", expectedKhz > 0 ? expectedKhz : null), "CPU 0 cpufreq is reported as an observation", null);
            }
        });
    }

    private static void memoryAndTime(Map<String, Probe> probes) {
        add(probes, new Probe("device.memory.consistency", "in_scope", false, PROP,
                "Landroid/app/ActivityManager;.getMemoryInfo(Landroid/app/ActivityManager$MemoryInfo;)V",
                "Landroid/app/ActivityManager;.getMemoryClass()I", "Landroid/app/ActivityManager;.getLargeMemoryClass()I",
                "Ljava/lang/Runtime;.maxMemory()J") {
            @Override Outcome run(ProbeSession session) throws Exception {
                ActivityManager activity = manager(session);
                ActivityManager.MemoryInfo info = new ActivityManager.MemoryInfo();
                activity.getMemoryInfo(info);
                String field = procField(text("/proc/meminfo"), "MemTotal");
                long procBytes = field == null ? -1 : Long.parseLong(field.split("\\s+")[0]) * 1024;
                int memory = activity.getMemoryClass();
                int large = activity.getLargeMemoryClass();
                long heapLimit = heapBytes(prop("dalvik.vm.heapgrowthlimit"));
                long heapSize = heapBytes(prop("dalvik.vm.heapsize"));
                long javaHeap = Runtime.getRuntime().maxMemory();
                long mib = 1024L * 1024L;
                return check(info.totalMem > 0 && info.totalMem == procBytes && info.availMem >= 0
                                && info.availMem <= info.totalMem && memory > 0 && memory <= large
                                && memory * mib == heapLimit && large * mib == heapSize && javaHeap == heapLimit,
                        obj("activity_total_bytes", info.totalMem, "proc_total_bytes", procBytes, "available_bytes", info.availMem,
                                "class_mib", memory, "large_class_mib", large, "growth_limit_bytes", heapLimit,
                                "heap_size_bytes", heapSize, "runtime_max_bytes", javaHeap),
                        obj("activity_proc_total_equal", true, "available_within_total", true, "heap_limits_equal_after_unit_conversion", true),
                        "ActivityManager, procfs, properties and Java agree about memory");
            }
        });
        add(probes, new Probe("device.display.refresh", "in_scope", false,
                "Landroid/view/Display;.getRefreshRate()F", "Landroid/view/Choreographer;.getInstance()Landroid/view/Choreographer;",
                "Landroid/view/Choreographer;.postFrameCallback(Landroid/view/Choreographer$FrameCallback;)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                float hz = session.activity.getWindowManager().getDefaultDisplay().getRefreshRate();
                FrameSamples samples = new FrameSamples();
                session.activity.runOnUiThread(samples);
                try {
                    if (!samples.done.await(3500, TimeUnit.MILLISECONDS)) {
                        return check(false, obj("reported_hz", hz, "samples", samples.count), obj("samples", 60),
                                "Choreographer failed to deliver 60 frame timestamps in 3.5 seconds");
                    }
                    if (samples.failure != null) return error(samples.failure);
                    double intervalMs = (samples.times[59] - samples.times[0]) / 59e6;
                    double expectedMs = hz > 0 ? 1000.0 / hz : 0;
                    boolean increasing = true;
                    for (int i = 1; i < 60; i++) increasing &= samples.times[i] > samples.times[i - 1];
                    double tolerance = Math.max(3.0, expectedMs * 0.25);
                    return check(hz > 0 && increasing && Math.abs(intervalMs - expectedMs) <= tolerance,
                            obj("reported_hz", hz, "samples", samples.count, "mean_interval_ms", intervalMs,
                                    "strictly_increasing", increasing),
                            obj("samples", 60, "interval_ms", expectedMs, "tolerance_ms", tolerance),
                            "Choreographer timestamps agree with the display refresh period within 25%");
                } finally {
                    samples.cancelled = true;
                }
            }
        });
        add(probes, new Probe("device.locale", "in_scope", false, PROP,
                "Ljava/util/Locale;.getDefault()Ljava/util/Locale;",
                "Ljava/util/TimeZone;.getDefault()Ljava/util/TimeZone;",
                "Ljava/util/TimeZone;.getTimeZone(Ljava/lang/String;)Ljava/util/TimeZone;",
                "Landroid/content/res/Resources;.getConfiguration()Landroid/content/res/Configuration;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Locale java = Locale.getDefault();
                Locale config = session.activity.getResources().getConfiguration().locale;
                String language = prop("persist.sys.language");
                String country = prop("persist.sys.country");
                String languageSource = "persist.sys.language";
                String countrySource = "persist.sys.country";
                // AndroidRuntime::readLocale falls back only when both persistent fields are empty.
                if (language.isEmpty() && country.isEmpty()) {
                    languageSource = "ro.product.locale.language";
                    countrySource = "ro.product.locale.region";
                    language = prop(languageSource);
                    country = prop(countrySource);
                    if (language.isEmpty()) {
                        language = "en";
                        languageSource = "default";
                    }
                    if (country.isEmpty()) {
                        country = "US";
                        countrySource = "default";
                    }
                }
                Locale boot = new Locale(language.substring(0, Math.min(2, language.length())),
                        country.substring(0, Math.min(2, country.length())));

                String timezone = TimeZone.getDefault().getID();
                String tzProperty = prop("persist.sys.timezone");
                String tz = tzProperty.trim();
                String tzSource = "persist.sys.timezone";
                // RuntimeInit installs the property getter. TimeZone.getDefault then tries the file.
                if (tz.isEmpty()) {
                    try {
                        tz = text("/etc/timezone");
                        tzSource = "/etc/timezone";
                    } catch (IOException missing) {
                        tz = "GMT";
                        tzSource = "default";
                    }
                }
                String bootTimezone = TimeZone.getTimeZone(tz).getID();
                return new Checks()
                        .require("java_locale_matches_configuration", java.equals(config))
                        .require("java_locale_matches_boot_locale", java.getLanguage().equals(boot.getLanguage())
                                && java.getCountry().equals(boot.getCountry()))
                        .require("default_timezone_matches_rom_resolution", timezone.equals(bootTimezone))
                        .outcome(obj("java_locale", java.toString(), "configuration_locale", String.valueOf(config),
                                        "language_source", languageSource, "language_value", language,
                                        "country_source", countrySource, "country_value", country,
                                        "timezone", timezone, "timezone_property", tzProperty,
                                        "timezone_source", tzSource, "timezone_value", tz),
                                obj("java_locale", boot.toString(), "timezone", bootTimezone),
                                "Java locale and timezone follow the API 19 startup fallbacks");
            }
        });
        add(probes, new Probe("device.time.clocks", "in_scope", false, "clock_gettime",
                "Ljava/lang/System;.currentTimeMillis()J", "Ljava/lang/System;.nanoTime()J",
                "Landroid/os/SystemClock;.uptimeMillis()J", "Landroid/os/SystemClock;.elapsedRealtime()J",
                "Ljava/lang/Thread;.sleep(J)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                NativeProbe.load();
                long[] before = time();
                Thread.sleep(200);
                long[] after = time();
                String[] names = {"current", "uptime", "elapsed", "nano", "monotonic", "realtime", "boottime"};
                boolean valid = before[2] >= before[1] && after[2] >= after[1];
                long min = Long.MAX_VALUE;
                long max = Long.MIN_VALUE;
                JSONObject observed = new JSONObject();
                for (int i = 0; i < names.length; i++) {
                    long delta = after[i] - before[i];
                    valid &= i < 4 || before[i] >= 0 && after[i] >= 0;
                    min = Math.min(min, delta);
                    max = Math.max(max, delta);
                    observed.put(names[i], obj("before_ms", before[i], "after_ms", after[i], "delta_ms", delta));
                }
                return check(valid && min >= 150 && max <= 650 && max - min <= 120,
                        observed, obj("sleep_ms", 200, "minimum_delta_ms", 150, "maximum_delta_ms", 650, "max_spread_ms", 120),
                        "Java and native clocks advance together while the thread sleeps");
            }
        });
    }

    private static void installation(Map<String, Probe> probes) {
        add(probes, new Probe(IDENTITY, "in_scope", false,
                "Landroid/provider/Settings$Secure;.getString(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                String first = androidId(session.activity);
                String second = androidId(session.activity);
                String prior = session.identityBeforeRestart();
                return check(first != null && first.matches("[0-9a-fA-F]{16}") && first.equals(second)
                                && (!session.restarted() || first.equals(prior)),
                        obj("first", first, "second", second, "restarted", session.restarted(), "before_restart", prior),
                        obj("hex_digits", 16, "stable_across_reads", true, "stable_across_restart_if_restarted", true),
                        "ANDROID_ID is a stable sixteen-digit hexadecimal installation identity");
            }
        });
        add(probes, new Probe("device.identity.telephony", "info", false,
                "Landroid/telephony/TelephonyManager;.getDeviceId()Ljava/lang/String;",
                "Landroid/telephony/TelephonyManager;.getSimState()I",
                "Landroid/telephony/TelephonyManager;.getPhoneType()I",
                "Landroid/telephony/TelephonyManager;.getNetworkOperator()Ljava/lang/String;",
                "Landroid/telephony/TelephonyManager;.getLine1Number()Ljava/lang/String;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                TelephonyManager phone = (TelephonyManager) session.activity.getSystemService(Context.TELEPHONY_SERVICE);
                return info(obj("device_id", phone.getDeviceId(), "sim_state", phone.getSimState(),
                                "phone_type", phone.getPhoneType(), "network_operator", phone.getNetworkOperator(),
                                "line1_number", phone.getLine1Number()), "Telephony values are observations");
            }
        });
        add(probes, new Probe("device.identity.sim_consistency", "in_scope", false,
                "Landroid/telephony/TelephonyManager;.getSimState()I",
                "Landroid/telephony/TelephonyManager;.getSubscriberId()Ljava/lang/String;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                TelephonyManager phone = (TelephonyManager) session.activity.getSystemService(Context.TELEPHONY_SERVICE);
                int state = phone.getSimState();
                String subscriber = phone.getSubscriberId();
                return check(state != TelephonyManager.SIM_STATE_ABSENT || subscriber == null,
                        obj("sim_state", state, "subscriber_id", subscriber), obj("absent_implies_no_subscriber", true),
                        "A device without a SIM has no subscriber identity");
            }
        });
        add(probes, new Probe("device.storage", "in_scope", false,
                "Landroid/os/Environment;.getExternalStorageState()Ljava/lang/String;",
                "Landroid/content/Context;.getFilesDir()Ljava/io/File;", "Landroid/content/Context;.getCacheDir()Ljava/io/File;",
                "Landroid/content/Context;.getExternalFilesDir(Ljava/lang/String;)Ljava/io/File;",
                "Landroid/content/Context;.getObbDir()Ljava/io/File;", "Landroid/os/StatFs;.<init>(Ljava/lang/String;)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Context context = session.activity;
                String state = Environment.getExternalStorageState();
                File[] paths = {context.getFilesDir(), context.getCacheDir(), context.getExternalFilesDir(null), context.getObbDir()};
                String[] names = {"files", "cache", "external_files", "obb"};
                JSONObject directories = new JSONObject();
                boolean good = Environment.MEDIA_MOUNTED.equals(state);
                for (int i = 0; i < paths.length; i++) {
                    boolean writable = canWrite(paths[i]);
                    directories.put(names[i], obj("path", paths[i] == null ? null : paths[i].getAbsolutePath(), "write_read", writable));
                    good &= writable;
                }
                JSONObject internal = stat(Environment.getDataDirectory());
                JSONObject external = stat(Environment.getExternalStorageDirectory());
                return check(good && sane(internal) && sane(external),
                        obj("state", state, "directories", directories, "data_fs", internal, "external_fs", external),
                        obj("state", "mounted", "all_directories_support_write_and_read", true, "valid_statfs_totals", true),
                        "Application storage roots accept I/O and expose valid capacity");
            }
        });
        add(probes, new Probe("device.package_self", "in_scope", false,
                "Landroid/content/pm/PackageManager;.getPackageInfo(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
                "Landroid/content/pm/PackageManager;.checkSignatures(Ljava/lang/String;Ljava/lang/String;)I",
                "Landroid/content/pm/PackageManager;.getInstalledPackages(I)Ljava/util/List;", "Ljava/util/zip/ZipFile;.<init>(Ljava/io/File;)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Context context = session.activity;
                PackageManager pm = context.getPackageManager();
                String name = context.getPackageName();
                PackageInfo first = pm.getPackageInfo(name, PackageManager.GET_SIGNATURES);
                PackageInfo second = pm.getPackageInfo(name, PackageManager.GET_SIGNATURES);
                int signatures = first.signatures == null ? 0 : first.signatures.length;
                boolean stable = Arrays.equals(first.signatures, second.signatures);
                int match = pm.checkSignatures(name, name);
                ApplicationInfo app = first.applicationInfo;
                boolean hasDex = false;
                String sourceError = null;
                try {
                    ZipFile zip = new ZipFile(new File(app.sourceDir));
                    try { hasDex = zip.getEntry("classes.dex") != null; } finally { zip.close(); }
                } catch (IOException failure) {
                    sourceError = failure.toString();
                }
                boolean libraryPresent = app.nativeLibraryDir != null && new File(app.nativeLibraryDir, "libogprobe.so").isFile();
                boolean filesChild = app.dataDir != null && app.dataDir.equals(context.getFilesDir().getParent());
                boolean installed = false;
                for (PackageInfo candidate : pm.getInstalledPackages(0)) installed |= name.equals(candidate.packageName);
                boolean ok = first.versionCode == 1 && "1.0".equals(first.versionName)
                        && signatures > 0 && stable && match == PackageManager.SIGNATURE_MATCH
                        && hasDex && libraryPresent && filesChild && installed;
                return check(ok, obj("name", name, "version_code", first.versionCode, "version_name", first.versionName,
                                "signature_count", signatures, "repeat_signatures_match", stable, "self_signature_result", match,
                                "source_dir", app.sourceDir, "source_has_dex", hasDex, "source_error", sourceError,
                                "native_library_dir", app.nativeLibraryDir, "jni_library_present", libraryPresent,
                                "data_dir", app.dataDir, "files_is_child", filesChild, "in_installed_list", installed),
                        obj("version_code", 1, "version_name", "1.0", "signatures_present_and_stable", true,
                                "self_signature_result", PackageManager.SIGNATURE_MATCH, "source_has_dex", true,
                                "jni_library_present", true, "files_is_child", true, "in_installed_list", true),
                        "PackageManager metadata matches the installed APK, signature and application paths");
            }
        });
    }

    private static void features(Map<String, Probe> probes) {
        add(probes, new Probe("device.features", "in_scope", false,
                "Landroid/content/pm/PackageManager;.hasSystemFeature(Ljava/lang/String;)Z",
                "Landroid/content/pm/PackageManager;.getSystemAvailableFeatures()[Landroid/content/pm/FeatureInfo;",
                "Landroid/hardware/SensorManager;.getSensorList(I)Ljava/util/List;",
                "Landroid/app/ActivityManager;.getDeviceConfigurationInfo()Landroid/content/pm/ConfigurationInfo;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Context context = session.activity;
                PackageManager pm = context.getPackageManager();
                boolean touch = pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN);
                boolean multi = pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN_MULTITOUCH);
                boolean accelerometer = pm.hasSystemFeature(PackageManager.FEATURE_SENSOR_ACCELEROMETER);
                boolean configTouch = context.getResources().getConfiguration().touchscreen == Configuration.TOUCHSCREEN_FINGER;
                SensorManager sensors = (SensorManager) context.getSystemService(Context.SENSOR_SERVICE);
                boolean hasAccel = false;
                JSONArray inventory = new JSONArray();
                for (Sensor sensor : sensors.getSensorList(Sensor.TYPE_ALL)) {
                    inventory.put(obj("type", sensor.getType(), "name", sensor.getName()));
                    hasAccel |= sensor.getType() == Sensor.TYPE_ACCELEROMETER;
                }
                int declaredGl = 0;
                FeatureInfo[] features = pm.getSystemAvailableFeatures();
                if (features != null) for (FeatureInfo feature : features) {
                    if (feature.name == null) declaredGl = Math.max(declaredGl, feature.reqGlEsVersion);
                }
                int activityGl = manager(session).getDeviceConfigurationInfo().reqGlEsVersion;
                return check(touch == configTouch && (!multi || touch) && accelerometer == hasAccel
                                && declaredGl > 0 && declaredGl == activityGl,
                        obj("touchscreen", touch, "multitouch", multi, "configuration_touchscreen", configTouch,
                                "accelerometer_feature", accelerometer, "sensors", inventory, "feature_gles", declaredGl,
                                "activity_gles", activityGl),
                        obj("touchscreen_consistent", true, "multitouch_implies_touch", true,
                                "accelerometer_consistent", true, "gles_consistent", true),
                        "Package features agree with the resource configuration and framework service inventories");
            }
        });
    }

    static String androidId(Context context) {
        return Settings.Secure.getString(context.getContentResolver(), Settings.Secure.ANDROID_ID);
    }

    static String prop(String name) throws Exception {
        Class<?> properties = Class.forName("android.os.SystemProperties");
        Method get = properties.getMethod("get", String.class);
        return (String) get.invoke(null, name);
    }

    private static ActivityManager manager(ProbeSession session) {
        return (ActivityManager) session.activity.getSystemService(Context.ACTIVITY_SERVICE);
    }

    private static String procField(String contents, String key) {
        for (String line : contents.split("\n")) {
            int colon = line.indexOf(':');
            if (colon > 0 && key.equals(line.substring(0, colon).trim())) return line.substring(colon + 1).trim();
        }
        return null;
    }

    private static long heapBytes(String value) {
        if (value.length() < 2) return -1;
        char unit = value.charAt(value.length() - 1);
        int multiplier = unit == 'm' ? 1024 * 1024 : unit == 'k' ? 1024 : 0;
        if (multiplier == 0) return -1;
        return Long.parseLong(value.substring(0, value.length() - 1)) * multiplier;
    }

    private static long[] time() {
        long current = System.currentTimeMillis();
        long uptime = SystemClock.uptimeMillis();
        long elapsed = SystemClock.elapsedRealtime();
        long nano = System.nanoTime() / 1000000;
        long[] nativeTime = NativeProbe.clocks();
        return new long[] {current, uptime, elapsed, nano, milliseconds(nativeTime[0]), milliseconds(nativeTime[1]), milliseconds(nativeTime[2])};
    }

    private static long milliseconds(long nanos) {
        return nanos < 0 ? nanos : nanos / 1000000;
    }

    private static boolean canWrite(File directory) throws IOException {
        if (directory == null || !directory.isDirectory()) return false;
        File item = new File(directory, "probe-file-" + Long.toHexString(System.nanoTime()));
        try {
            FileOutputStream out = new FileOutputStream(item);
            try { out.write(19); } finally { out.close(); }
            FileInputStream in = new FileInputStream(item);
            try { return in.read() == 19 && in.read() == -1; } finally { in.close(); }
        } finally {
            item.delete();
        }
    }

    private static JSONObject stat(File directory) throws JSONException {
        StatFs stat = new StatFs(directory.getAbsolutePath());
        return obj("available_bytes", stat.getAvailableBytes(), "total_bytes", stat.getTotalBytes());
    }

    private static boolean sane(JSONObject stats) {
        long available = stats.optLong("available_bytes");
        long total = stats.optLong("total_bytes");
        return available >= 0 && total > 0 && available <= total;
    }

    static void environment(String id, Object observed, Map<String, JSONObject> groups) throws JSONException {
        if (!(observed instanceof JSONObject)) return;
        JSONObject value = (JSONObject) observed;
        if ("device.memory.consistency".equals(id)) groups.put("memory", value);
        if ("device.storage".equals(id)) groups.put("storage", value);
        if ("device.gl.info".equals(id)) groups.put("gl", value);
        if ("device.locale".equals(id)) groups.put("locale", value);
        if ("device.time.clocks".equals(id)) groups.put("time", value);
        if ("device.features".equals(id)) groups.put("sensors", obj("inventory", value.opt("sensors")));
        String group = id.startsWith("device.cpu.") ? "cpu" : id.startsWith("device.build.") ? "build"
                : id.startsWith("device.display.") ? "display" : null;
        if (group != null && groups.containsKey(group) && value != groups.get(group)) {
            JSONObject copy = new JSONObject(ProbeFiles.encode(groups.get(group)));
            copy.put(id.substring(("device." + group + ".").length()), value);
            groups.put(group, copy);
        }
    }

    private static final class FrameSamples implements Runnable, Choreographer.FrameCallback {
        final CountDownLatch done = new CountDownLatch(1);
        final long[] times = new long[60];
        volatile int count;
        volatile boolean cancelled;
        Throwable failure;
        Choreographer choreographer;

        @Override public void run() {
            try {
                choreographer = Choreographer.getInstance();
                choreographer.postFrameCallback(this);
            } catch (Throwable error) {
                failure = error;
                done.countDown();
            }
        }

        @Override public void doFrame(long nanos) {
            if (cancelled) return;
            try {
                times[count++] = nanos;
                if (count == times.length) done.countDown();
                else choreographer.postFrameCallback(this);
            } catch (Throwable error) {
                failure = error;
                done.countDown();
            }
        }
    }
}

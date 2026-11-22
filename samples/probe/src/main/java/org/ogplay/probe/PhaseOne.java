package org.ogplay.probe;

import android.content.res.Configuration;
import android.graphics.Point;
import android.os.Build;
import android.util.DisplayMetrics;
import android.view.Display;
import java.io.File;
import java.io.IOException;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import static org.ogplay.probe.Probe.obj;

final class PhaseOne {
    static final String PERSIST = "storage.prefs.persist_across_restart";

    static void register(Map<String, Probe> probes, boolean selftest) {
        add(probes, new Probe("native.load", "in_scope", false,
                "Ljava/lang/System;.loadLibrary(Ljava/lang/String;)V", "Java_org_ogplay_probe_NativeProbe_ping") {
            @Override Outcome run(ProbeSession session) throws JSONException {
                NativeProbe.load();
                int ping = NativeProbe.ping();
                return check(ping == 19, obj("jni_value", ping), obj("jni_value", 19), "Load the ARM JNI library and call its export");
            }
        });
        if (selftest) selftest(probes);
        add(probes, new Probe("device.build.fields", "in_scope", false,
                "Landroid/os/Build$VERSION;.SDK_INT:I", "Landroid/os/Build$VERSION;.RELEASE:Ljava/lang/String;",
                "Landroid/os/Build;.MODEL:Ljava/lang/String;", "Landroid/os/Build;.MANUFACTURER:Ljava/lang/String;",
                "Landroid/os/Build;.BRAND:Ljava/lang/String;", "Landroid/os/Build;.FINGERPRINT:Ljava/lang/String;") {
            @Override Outcome run(ProbeSession session) throws JSONException {
                JSONObject values = obj("sdk", Build.VERSION.SDK_INT, "release", Build.VERSION.RELEASE,
                        "model", Build.MODEL, "manufacturer", Build.MANUFACTURER, "brand", Build.BRAND,
                        "fingerprint", Build.FINGERPRINT);
                JSONArray required = new JSONArray();
                boolean present = true;
                for (String key : new String[] {"model", "manufacturer", "brand", "fingerprint"}) {
                    required.put(key);
                    present &= !values.isNull(key) && values.optString(key).trim().length() > 0;
                }
                return check(present && Build.VERSION.SDK_INT == 19 && values.optString("release").startsWith("4.4"),
                        values, obj("sdk", 19, "release_prefix", "4.4", "nonempty", required),
                        "API 19 identifies as Android 4.4 and exposes its build identity");
            }
        });
        add(probes, new Probe("device.cpu.count_consistency", "in_scope", false,
                "Ljava/lang/Runtime;.availableProcessors()I",
                "Ljava/io/FileInputStream;.<init>(Ljava/io/File;)V",
                "Ljava/io/FileInputStream;.read([B)I", "sysconf") {
            @Override Outcome run(ProbeSession session) throws Exception {
                NativeProbe.load();
                int runtime = Runtime.getRuntime().availableProcessors();
                String present = text("/sys/devices/system/cpu/present").trim();
                String possible = text("/sys/devices/system/cpu/possible").trim();
                int proc = 0;
                for (String line : text("/proc/cpuinfo").split("\n")) {
                    int colon = line.indexOf(':');
                    if (colon >= 0 && "processor".equals(line.substring(0, colon).trim())) proc++;
                }
                long online = NativeProbe.cpuCount(true);
                long configured = NativeProbe.cpuCount(false);
                int configuredByRunner = session.expect.optInt("cpu_cores", runtime);
                return check(runtime > 0 && cpuListCount(present) == runtime && cpuListCount(possible) == runtime
                                && proc == runtime && online == runtime && configured == runtime
                                && configuredByRunner == runtime,
                        obj("runtime", runtime, "present", present, "possible", possible, "proc", proc,
                                "sysconf_online", online, "sysconf_configured", configured),
                        obj("all_counts", configuredByRunner, "positive", true),
                        "Java, sysfs, procfs and bionic agree on the configured core count");
            }
        });
        add(probes, new Probe("device.display.consistency", "in_scope", true,
                "Landroid/content/res/Resources;.getDisplayMetrics()Landroid/util/DisplayMetrics;",
                "Landroid/content/res/Resources;.getConfiguration()Landroid/content/res/Configuration;",
                "Landroid/view/WindowManager;.getDefaultDisplay()Landroid/view/Display;",
                "Landroid/view/Display;.getSize(Landroid/graphics/Point;)V",
                "Landroid/view/Display;.getMetrics(Landroid/util/DisplayMetrics;)V",
                "Landroid/view/View;.getWidth()I", "Landroid/view/View;.getHeight()I") {
            @Override Outcome run(ProbeSession session) throws JSONException {
                ProbeActivity activity = session.activity;
                Display display = activity.getWindowManager().getDefaultDisplay();
                DisplayMetrics metrics = new DisplayMetrics();
                DisplayMetrics resourceMetrics = activity.getResources().getDisplayMetrics();
                Configuration configuration = activity.getResources().getConfiguration();
                Point size = new Point();
                display.getSize(size);
                display.getMetrics(metrics);
                int configuredWidth = session.expect.optInt("width_px", size.x);
                int configuredHeight = session.expect.optInt("height_px", size.y);
                // The launcher declares portrait; a landscape runner surface rotates after launch.
                int width = Math.min(configuredWidth, configuredHeight);
                int height = Math.max(configuredWidth, configuredHeight);
                int dpi = session.expect.optInt("density_dpi", metrics.densityDpi);
                int rootWidth = activity.contentWidth();
                int rootHeight = activity.contentHeight();
                float density = metrics.density;
                boolean same = width > 0 && height > 0 && dpi > 0 && size.x == width && size.y == height
                        && metrics.widthPixels == width && metrics.heightPixels == height
                        && resourceMetrics.widthPixels == width && resourceMetrics.heightPixels == height
                        && metrics.densityDpi == dpi && resourceMetrics.densityDpi == dpi
                        && Math.abs(density - dpi / 160f) < 0.0001f
                        && Math.abs(resourceMetrics.density - density) < 0.0001f
                        && Math.abs(configuration.screenWidthDp * density - width) <= density
                        && Math.abs(configuration.screenHeightDp * density - height) <= density
                        && rootWidth == width && rootHeight == height;
                return check(same, obj("display_size_px", pair(size.x, size.y),
                                "display_metrics", metrics(metrics), "resource_metrics", metrics(resourceMetrics),
                                "configuration_dp", pair(configuration.screenWidthDp, configuration.screenHeightDp),
                                "root_px", pair(rootWidth, rootHeight)),
                        obj("size_px", pair(width, height), "density_dpi", dpi, "density", dpi / 160f),
                        "Full-screen root, app display size, resource metrics and dp agree");
            }
        });
        add(probes, new Probe("runtime.process.exec_getprop", "in_scope", false,
                "Ljava/lang/Runtime;.exec(Ljava/lang/String;)Ljava/lang/Process;",
                "Ljava/lang/ProcessBuilder;.start()Ljava/lang/Process;") {
            @Override Outcome run(ProbeSession session) throws JSONException {
                JSONObject exec = runGetprop(true);
                JSONObject builder = runGetprop(false);
                boolean pass = Build.CPU_ABI.equals(exec.opt("stdout")) && Build.CPU_ABI.equals(builder.opt("stdout"))
                        && exec.optInt("exit", -1) == 0 && builder.optInt("exit", -1) == 0;
                return new Outcome(pass ? "pass" : "fail", obj("exec", exec, "process_builder", builder),
                        obj("stdout", Build.CPU_ABI, "exit", 0),
                        "Both subprocess entry points run getprop and read ro.product.cpu.abi", null);
            }
        });
        add(probes, new Probe(PERSIST, "in_scope", false,
                "Landroid/content/Context;.getSharedPreferences(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                "Landroid/content/SharedPreferences$Editor;.commit()Z",
                "Landroid/content/SharedPreferences;.getString(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;") {
            @Override Outcome run(ProbeSession session) throws JSONException {
                return session.checkPrefs();
            }
        });
    }

    private static void selftest(Map<String, Probe> probes) {
        add(probes, new Probe("selftest.crash", "native", "in_scope", false, 5000, "abort") {
            @Override Outcome run(ProbeSession session) {
                NativeProbe.load();
                NativeProbe.abortProcess();
                return check(false, "returned", "process death", "abort() returned");
            }
        });
        add(probes, new Probe("selftest.hang", "runtime", "in_scope", false, 400,
                "Ljava/lang/Thread;.sleep(J)V") {
            @Override Outcome run(ProbeSession session) throws InterruptedException {
                while (true) Thread.sleep(1000);
            }
        });
    }

    static void add(Map<String, Probe> probes, Probe probe) {
        if (probes.put(probe.id, probe) != null) throw new IllegalArgumentException("Duplicate probe " + probe.id);
    }

    static String text(String path) throws IOException {
        return ProbeFiles.text(new File(path), 1024 * 1024);
    }

    private static int cpuListCount(String list) {
        int count = 0;
        for (String range : list.split(",")) {
            int dash = range.indexOf('-');
            if (dash < 0) {
                if (Integer.parseInt(range) < 0) return -1;
                count++;
            } else {
                int first = Integer.parseInt(range.substring(0, dash));
                int last = Integer.parseInt(range.substring(dash + 1));
                if (first < 0 || last < first) return -1;
                count += last - first + 1;
            }
        }
        return count;
    }

    private static JSONArray pair(int x, int y) {
        return new JSONArray().put(x).put(y);
    }

    private static JSONObject metrics(DisplayMetrics metrics) throws JSONException {
        return obj("size_px", pair(metrics.widthPixels, metrics.heightPixels), "density", metrics.density,
                "density_dpi", metrics.densityDpi);
    }

    private static JSONObject runGetprop(boolean exec) throws JSONException {
        try {
            Process process = exec ? Runtime.getRuntime().exec("getprop ro.product.cpu.abi")
                    : new ProcessBuilder("getprop", "ro.product.cpu.abi").start();
            java.io.BufferedReader reader = new java.io.BufferedReader(
                    new java.io.InputStreamReader(process.getInputStream()));
            String line = reader.readLine();
            reader.close();
            return obj("started", true, "stdout", line, "exit", process.waitFor());
        } catch (Throwable failure) {
            return rejection(failure);
        }
    }

    private static JSONObject rejection(Throwable failure) throws JSONException {
        if (failure == null) return obj("started", true);
        Throwable cause = failure.getCause();
        return obj("started", false, "exception", failure.getClass().getName(), "message", failure.getMessage(),
                "cause", cause == null ? null : obj("exception", cause.getClass().getName(), "message", cause.getMessage()));
    }

    static void environment(String id, Object observed, Map<String, JSONObject> groups) {
        if (!(observed instanceof JSONObject)) return;
        if ("device.build.fields".equals(id)) groups.put("build", (JSONObject) observed);
        if ("device.cpu.count_consistency".equals(id)) groups.put("cpu", (JSONObject) observed);
        if ("device.display.consistency".equals(id)) groups.put("display", (JSONObject) observed);
    }
}

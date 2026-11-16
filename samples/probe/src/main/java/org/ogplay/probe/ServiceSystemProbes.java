package org.ogplay.probe;

import android.app.ActivityManager;
import android.app.AlarmManager;
import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.ConfigurationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.res.Resources;
import android.database.ContentObserver;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.os.Process;
import android.os.SystemClock;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.ogplay.probe.ApiSweep.Step;

import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.Probe.obj;

/** API sweeps for the process, package, settings and scheduling services that most titles call. */
final class ServiceSystemProbes {
    private static final String WM = "Landroid/view/WindowManager;";
    private static final String DISPLAY = "Landroid/view/Display;";
    private static final String DM = "Landroid/hardware/display/DisplayManager;";
    private static final String PM = "Landroid/content/pm/PackageManager;";
    private static final String CONTEXT = "Landroid/content/Context;";
    private static final String SECURE = "Landroid/provider/Settings$Secure;";
    private static final String SYSTEM = "Landroid/provider/Settings$System;";
    private static final String GLOBAL = "Landroid/provider/Settings$Global;";
    private static final String CR = "Landroid/content/ContentResolver;";
    private static final String AM = "Landroid/app/ActivityManager;";
    private static final String POWER = "Landroid/os/PowerManager;";
    private static final String WAKE = "Landroid/os/PowerManager$WakeLock;";
    private static final String NM = "Landroid/app/NotificationManager;";
    private static final String BUILDER = "Landroid/app/Notification$Builder;";
    private static final String NOTIFICATION = "Landroid/app/Notification;";
    private static final String PI = "Landroid/app/PendingIntent;";
    private static final String ALARM = "Landroid/app/AlarmManager;";
    private static final String INFLATER = "Landroid/view/LayoutInflater;";

    static void register(Map<String, Probe> probes) {
        window(probes);
        packages(probes);
        settings(probes);
        content(probes);
        activity(probes);
        power(probes);
        notification(probes);
        scheduling(probes);
        inflater(probes);
    }

    private static void window(Map<String, Probe> probes) {
        add(probes, new Probe("services.window.api", "services", "in_scope", true, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final WindowManager wm = (WindowManager) session.activity.getSystemService(Context.WINDOW_SERVICE);
                final Display d = (Display) s.call(WM, "getDefaultDisplay", new Step() { Object run() { return wm.getDefaultDisplay(); } });
                if (d == null) return s.outcome(this, obj("display", true), "WindowManager exposes the default display");
                final Point size = new Point();
                final Point real = new Point();
                final DisplayMetrics metrics = new DisplayMetrics();
                final DisplayMetrics realMetrics = new DisplayMetrics();
                Integer width = (Integer) s.call(DISPLAY, "getWidth", new Step() { Object run() { return d.getWidth(); } });
                Integer height = (Integer) s.call(DISPLAY, "getHeight", new Step() { Object run() { return d.getHeight(); } });
                s.call(DISPLAY, "getSize", new Step() { Object run() { d.getSize(size); return size.x + "x" + size.y; } });
                s.call(DISPLAY, "getRealSize", new Step() { Object run() { d.getRealSize(real); return real.x + "x" + real.y; } });
                s.call(DISPLAY, "getMetrics", new Step() { Object run() { d.getMetrics(metrics); return metrics.widthPixels + "x" + metrics.heightPixels; } });
                s.call(DISPLAY, "getRealMetrics", new Step() { Object run() { d.getRealMetrics(realMetrics); return realMetrics.densityDpi; } });
                Integer rotation = (Integer) s.call(DISPLAY, "getRotation", new Step() { Object run() { return d.getRotation(); } });
                Integer orientation = (Integer) s.call(DISPLAY, "getOrientation", new Step() { Object run() { return d.getOrientation(); } });
                Integer id = (Integer) s.call(DISPLAY, "getDisplayId", new Step() { Object run() { return d.getDisplayId(); } });
                Float refresh = (Float) s.call(DISPLAY, "getRefreshRate", new Step() { Object run() { return d.getRefreshRate(); } });
                s.call(DISPLAY, "getPixelFormat", new Step() { Object run() { return d.getPixelFormat(); } });
                s.call(DISPLAY, "getName", new Step() { Object run() { return d.getName(); } });
                Boolean valid = (Boolean) s.call(DISPLAY, "isValid", new Step() { Object run() { return d.isValid(); } });
                s.call(DISPLAY, "getRectSize", new Step() {
                    Object run() { android.graphics.Rect r = new android.graphics.Rect(); d.getRectSize(r); return r.width() + "x" + r.height(); }
                });
                final TextView overlay = new TextView(session.activity);
                overlay.setText("probe");
                s.call(WM, "addView", new Step() {
                    Object run() {
                        WindowManager.LayoutParams params = new WindowManager.LayoutParams(1, 1,
                                WindowManager.LayoutParams.TYPE_APPLICATION, WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                                        | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE, android.graphics.PixelFormat.TRANSLUCENT);
                        session.activity.getWindowManager().addView(overlay, params);
                        return true;
                    }
                });
                s.call(WM, "updateViewLayout", new Step() {
                    Object run() {
                        ViewGroup.LayoutParams params = overlay.getLayoutParams();
                        params.width = 2;
                        session.activity.getWindowManager().updateViewLayout(overlay, params);
                        return true;
                    }
                });
                s.call(WM, "removeView", new Step() { Object run() { session.activity.getWindowManager().removeView(overlay); return true; } });
                s.require("display_id_default", id != null && id == Display.DEFAULT_DISPLAY)
                        .require("width_matches_size", width != null && width == size.x)
                        .require("height_matches_size", height != null && height == size.y)
                        .require("metrics_match_size", metrics.widthPixels == size.x && metrics.heightPixels == size.y)
                        .require("real_size_covers_app_size", real.x >= size.x && real.y >= size.y)
                        .require("orientation_equals_rotation", rotation != null && rotation.equals(orientation))
                        .require("refresh_positive", refresh != null && refresh > 0)
                        .require("display_valid", Boolean.TRUE.equals(valid));
                return s.outcome(this, obj("display_id", 0, "size_sources_agree", true, "orientation_equals_rotation", true,
                        "overlay_window", "added, updated and removed"), "Default display getters agree and an app window can be added");
            }
        });
        add(probes, new Probe("services.display.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final DisplayManager dm = (DisplayManager) session.activity.getSystemService(Context.DISPLAY_SERVICE);
                Display[] all = (Display[]) s.call(DM, "getDisplays", new Step() { Object run() { return dm.getDisplays(); } });
                Display[] presentation = (Display[]) s.call(DM, "getDisplays", "presentation", new Step() {
                    Object run() { return dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION); }
                });
                Display main = (Display) s.call(DM, "getDisplay", "0", new Step() { Object run() { return dm.getDisplay(Display.DEFAULT_DISPLAY); } });
                Display missing = (Display) s.call(DM, "getDisplay", "99", new Step() { Object run() { return dm.getDisplay(99); } });
                final DisplayManager.DisplayListener listener = new DisplayManager.DisplayListener() {
                    @Override public void onDisplayAdded(int displayId) {}
                    @Override public void onDisplayRemoved(int displayId) {}
                    @Override public void onDisplayChanged(int displayId) {}
                };
                s.call(DM, "registerDisplayListener", new Step() {
                    Object run() { dm.registerDisplayListener(listener, new Handler(Looper.getMainLooper())); return true; }
                });
                s.call(DM, "unregisterDisplayListener", new Step() { Object run() { dm.unregisterDisplayListener(listener); return true; } });
                boolean listed = false;
                if (all != null) for (Display display : all) listed |= display.getDisplayId() == Display.DEFAULT_DISPLAY;
                s.require("default_display_listed", listed)
                        .require("no_presentation_display", presentation != null && presentation.length == 0)
                        .require("default_display_resolves", main != null && main.getDisplayId() == Display.DEFAULT_DISPLAY)
                        .require("unknown_display_is_null", missing == null && presentation != null);
                return s.outcome(this, obj("displays", "contains 0", "presentation_displays", 0, "unknown_display", null),
                        "DisplayManager lists exactly the built-in display");
            }
        });
    }

    private static void packages(Map<String, Probe> probes) {
        add(probes, new Probe("services.package.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final Context context = session.activity;
                final PackageManager pm = context.getPackageManager();
                final String self = context.getPackageName();
                final ComponentName activity = new ComponentName(context, ProbeActivity.class);
                final PackageInfo info = (PackageInfo) s.call(PM, "getPackageInfo", "self", new Step() {
                    Object run() throws Exception {
                        return pm.getPackageInfo(self, PackageManager.GET_ACTIVITIES | PackageManager.GET_PERMISSIONS
                                | PackageManager.GET_SIGNATURES | PackageManager.GET_SERVICES);
                    }
                });
                s.call("Landroid/content/pm/PackageInfo;", "firstInstallTime", new Step() { Object run() { return info == null ? null : info.firstInstallTime; } });
                s.call("Landroid/content/pm/PackageInfo;", "lastUpdateTime", new Step() { Object run() { return info == null ? null : info.lastUpdateTime; } });
                final ApplicationInfo app = (ApplicationInfo) s.call(PM, "getApplicationInfo", new Step() {
                    Object run() throws Exception { return pm.getApplicationInfo(self, PackageManager.GET_META_DATA); }
                });
                CharSequence label = (CharSequence) s.call(PM, "getApplicationLabel", new Step() {
                    Object run() { return app == null ? null : pm.getApplicationLabel(app); }
                });
                ActivityInfo activityInfo = (ActivityInfo) s.call(PM, "getActivityInfo", new Step() {
                    Object run() throws Exception { return pm.getActivityInfo(activity, PackageManager.GET_META_DATA); }
                });
                Integer granted = (Integer) s.call(PM, "checkPermission", "declared", new Step() {
                    Object run() { return pm.checkPermission(android.Manifest.permission.READ_PHONE_STATE, self); }
                });
                Integer denied = (Integer) s.call(PM, "checkPermission", "undeclared", new Step() {
                    Object run() { return pm.checkPermission(android.Manifest.permission.CAMERA, self); }
                });
                Integer selfGranted = (Integer) s.call(CONTEXT, "checkCallingOrSelfPermission", new Step() {
                    Object run() { return context.checkCallingOrSelfPermission(android.Manifest.permission.ACCESS_NETWORK_STATE); }
                });
                Boolean touch = (Boolean) s.call(PM, "hasSystemFeature", "touchscreen", new Step() {
                    Object run() { return pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN); }
                });
                Boolean bogus = (Boolean) s.call(PM, "hasSystemFeature", "unknown", new Step() {
                    Object run() { return pm.hasSystemFeature("org.ogplay.probe.feature"); }
                });
                s.call(PM, "getSystemAvailableFeatures", new Step() { Object run() { return pm.getSystemAvailableFeatures(); } });
                Intent launch = (Intent) s.call(PM, "getLaunchIntentForPackage", new Step() { Object run() { return pm.getLaunchIntentForPackage(self); } });
                final Intent main = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER).setPackage(self);
                @SuppressWarnings("unchecked") List<ResolveInfo> launchers = (List<ResolveInfo>) s.call(PM, "queryIntentActivities", new Step() {
                    Object run() { return pm.queryIntentActivities(main, 0); }
                });
                ResolveInfo resolved = (ResolveInfo) s.call(PM, "resolveActivity", new Step() { Object run() { return pm.resolveActivity(main, 0); } });
                // In-app billing discovery is the most common service query in the title corpus.
                @SuppressWarnings("unchecked") List<ResolveInfo> billing = (List<ResolveInfo>) s.call(PM, "queryIntentServices", "billing", new Step() {
                    Object run() {
                        return pm.queryIntentServices(new Intent("com.android.vending.billing.InAppBillingService.BIND")
                                .setPackage("com.android.vending"), 0);
                    }
                });
                s.call(PM, "queryBroadcastReceivers", new Step() {
                    Object run() { return pm.queryBroadcastReceivers(new Intent(Intent.ACTION_BOOT_COMPLETED).setPackage(self), 0); }
                });
                Resources resources = (Resources) s.call(PM, "getResourcesForApplication", new Step() {
                    Object run() throws Exception { return pm.getResourcesForApplication(self); }
                });
                @SuppressWarnings("unchecked") List<PackageInfo> installed = (List<PackageInfo>) s.call(PM, "getInstalledPackages", new Step() {
                    Object run() { return pm.getInstalledPackages(0); }
                });
                @SuppressWarnings("unchecked") List<ApplicationInfo> applications = (List<ApplicationInfo>) s.call(PM, "getInstalledApplications", new Step() {
                    Object run() { return pm.getInstalledApplications(0); }
                });
                String[] uidPackages = (String[]) s.call(PM, "getPackagesForUid", new Step() { Object run() { return pm.getPackagesForUid(Process.myUid()); } });
                String uidName = (String) s.call(PM, "getNameForUid", new Step() { Object run() { return pm.getNameForUid(Process.myUid()); } });
                s.call(PM, "getInstallerPackageName", new Step() { Object run() { return pm.getInstallerPackageName(self); } });
                Integer enabled = (Integer) s.call(PM, "getComponentEnabledSetting", new Step() { Object run() { return pm.getComponentEnabledSetting(activity); } });
                Object icon = s.call(PM, "getApplicationIcon", new Step() { Object run() throws Exception { return pm.getApplicationIcon(self); } });
                s.call(PM, "getDefaultActivityIcon", new Step() { Object run() { return pm.getDefaultActivityIcon(); } });
                s.throwsType(PM, "getPackageInfo", "missing", PackageManager.NameNotFoundException.class, new Step() {
                    Object run() throws Exception { return pm.getPackageInfo("org.ogplay.probe.missing", 0); }
                });
                boolean probeActivity = false;
                boolean requested = false;
                boolean signed = false;
                if (info != null) {
                    if (info.activities != null) for (ActivityInfo item : info.activities) probeActivity |= ProbeActivity.class.getName().equals(item.name);
                    if (info.requestedPermissions != null) for (String item : info.requestedPermissions)
                        requested |= android.Manifest.permission.READ_PHONE_STATE.equals(item);
                    signed = info.signatures != null && info.signatures.length > 0 && info.signatures[0].toByteArray().length > 0;
                }
                boolean listedLauncher = launchers != null && launchers.size() == 1
                        && ProbeActivity.class.getName().equals(launchers.get(0).activityInfo.name);
                boolean listedPackage = false;
                if (installed != null) for (PackageInfo item : installed) listedPackage |= self.equals(item.packageName);
                boolean listedApplication = false;
                if (applications != null) for (ApplicationInfo item : applications) listedApplication |= self.equals(item.packageName);
                boolean uidListed = false;
                if (uidPackages != null) for (String item : uidPackages) uidListed |= self.equals(item);
                s.require("package_version", info != null && info.versionCode == 1 && "1.0".equals(info.versionName))
                        .require("billing_query_empty", billing != null && billing.isEmpty())
                        .require("install_times", info != null && info.firstInstallTime > 0 && info.lastUpdateTime >= info.firstInstallTime)
                        .require("activities_listed", probeActivity)
                        .require("requested_permissions_listed", requested)
                        .require("signature_present", signed)
                        .require("application_identity", app != null && app.uid == Process.myUid() && app.targetSdkVersion == 19
                                && (app.flags & ApplicationInfo.FLAG_SYSTEM) == 0)
                        .require("source_dir_readable", app != null && new File(app.sourceDir).canRead())
                        .require("native_dir_exists", app != null && new File(app.nativeLibraryDir).isDirectory())
                        .require("data_dir_matches_files_dir", app != null
                                && new File(app.dataDir).getAbsolutePath().equals(context.getFilesDir().getParentFile().getAbsolutePath()))
                        .require("label_matches_resource", label != null && label.toString().equals(context.getString(R.string.app_name)))
                        .require("activity_info_matches", activityInfo != null && ProbeActivity.class.getName().equals(activityInfo.name))
                        .require("declared_permission_granted", granted != null && granted == PackageManager.PERMISSION_GRANTED)
                        .require("undeclared_permission_denied", denied != null && denied == PackageManager.PERMISSION_DENIED)
                        .require("context_permission_granted", selfGranted != null && selfGranted == PackageManager.PERMISSION_GRANTED)
                        .require("touchscreen_feature", Boolean.TRUE.equals(touch))
                        .require("unknown_feature_absent", Boolean.FALSE.equals(bogus))
                        .require("launch_intent_targets_probe", launch != null && launch.getComponent() != null
                                && ProbeActivity.class.getName().equals(launch.getComponent().getClassName()))
                        .require("launcher_query_lists_probe", listedLauncher)
                        .require("launcher_resolves_probe", resolved != null && resolved.activityInfo != null
                                && ProbeActivity.class.getName().equals(resolved.activityInfo.name))
                        .require("own_resources_resolve", resources != null
                                && context.getString(R.string.app_name).equals(resources.getString(R.string.app_name)))
                        .require("installed_packages_list_self", listedPackage)
                        .require("installed_applications_list_self", listedApplication)
                        .require("uid_maps_to_self", uidListed && self.equals(uidName))
                        .require("component_state_default", enabled != null && enabled == PackageManager.COMPONENT_ENABLED_STATE_DEFAULT)
                        .require("application_icon_present", icon != null);
                return s.outcome(this, obj("self_package", "complete and consistent", "undeclared_permission", "denied",
                        "missing_package", "NameNotFoundException"), "PackageManager answers the common self-inspection queries");
            }
        });
    }

    private static void settings(Map<String, Probe> probes) {
        add(probes, new Probe("services.settings.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final ContentResolver cr = session.activity.getContentResolver();
                String androidId = (String) s.identifier(SECURE, "getString", new Step() {
                    Object run() { return Settings.Secure.getString(cr, Settings.Secure.ANDROID_ID); }
                });
                Integer brightness = (Integer) s.call(SYSTEM, "getInt", "screen_brightness", new Step() {
                    Object run() { return Settings.System.getInt(cr, Settings.System.SCREEN_BRIGHTNESS, -1); }
                });
                Integer rotation = (Integer) s.call(SYSTEM, "getInt", "accelerometer_rotation", new Step() {
                    Object run() { return Settings.System.getInt(cr, Settings.System.ACCELEROMETER_ROTATION, -1); }
                });
                String clock = (String) s.call(SYSTEM, "getString", "time_12_24", new Step() {
                    Object run() { return Settings.System.getString(cr, Settings.System.TIME_12_24); }
                });
                Integer airplane = (Integer) s.call(GLOBAL, "getInt", "airplane_mode_on", new Step() {
                    Object run() { return Settings.Global.getInt(cr, Settings.Global.AIRPLANE_MODE_ON, -1); }
                });
                s.call(GLOBAL, "getInt", "adb_enabled", new Step() { Object run() { return Settings.Global.getInt(cr, Settings.Global.ADB_ENABLED, -1); } });
                String allowed = (String) s.call(SECURE, "getString", "location_providers_allowed", new Step() {
                    Object run() { return Settings.Secure.getString(cr, Settings.Secure.LOCATION_PROVIDERS_ALLOWED); }
                });
                Integer mode = (Integer) s.call(SECURE, "getInt", "location_mode", new Step() {
                    Object run() { return Settings.Secure.getInt(cr, Settings.Secure.LOCATION_MODE, -1); }
                });
                s.call(SECURE, "getString", "default_input_method", new Step() {
                    Object run() { return Settings.Secure.getString(cr, Settings.Secure.DEFAULT_INPUT_METHOD); }
                });
                s.throwsType(SYSTEM, "getInt", "missing", Settings.SettingNotFoundException.class, new Step() {
                    Object run() throws Exception { return Settings.System.getInt(cr, "ogplay_probe_missing"); }
                });
                final Uri uri = (Uri) s.call(SYSTEM, "getUriFor", new Step() { Object run() { return Settings.System.getUriFor(Settings.System.SCREEN_BRIGHTNESS); } });
                final ContentObserver observer = new ContentObserver(new Handler(Looper.getMainLooper())) {};
                s.call(CR, "registerContentObserver", "settings", new Step() { Object run() { cr.registerContentObserver(uri, false, observer); return true; } });
                s.call(CR, "unregisterContentObserver", "settings", new Step() { Object run() { cr.unregisterContentObserver(observer); return true; } });
                final android.location.LocationManager lm =
                        (android.location.LocationManager) session.activity.getSystemService(Context.LOCATION_SERVICE);
                Boolean gps = (Boolean) s.call("Landroid/location/LocationManager;", "isProviderEnabled", "gps", new Step() {
                    Object run() { return lm.isProviderEnabled(android.location.LocationManager.GPS_PROVIDER); }
                });
                Boolean network = (Boolean) s.call("Landroid/location/LocationManager;", "isProviderEnabled", "network", new Step() {
                    Object run() { return lm.isProviderEnabled(android.location.LocationManager.NETWORK_PROVIDER); }
                });
                String providers = allowed == null ? "" : allowed;
                s.require("android_id_hex", androidId != null && androidId.matches("[0-9a-f]{1,16}"))
                        .require("brightness_in_range", brightness != null && brightness >= 0 && brightness <= 255)
                        .require("rotation_flag", rotation != null && (rotation == 0 || rotation == 1))
                        .require("time_format_valid", clock == null || "12".equals(clock) || "24".equals(clock))
                        .require("airplane_flag", airplane != null && (airplane == 0 || airplane == 1))
                        .require("gps_setting_matches_manager", gps != null && gps == contains(providers, "gps"))
                        .require("network_setting_matches_manager", network != null && network == contains(providers, "network"))
                        .require("location_mode_matches_providers", mode != null && gps != null && network != null
                                && (mode == Settings.Secure.LOCATION_MODE_OFF) == (!gps && !network));
                return s.outcome(this, obj("android_id", "hex", "values_in_range", true, "location_settings_match_manager", true,
                        "missing_setting", "SettingNotFoundException"), "Settings provider values are valid and agree with the services");
            }
        });
    }

    private static boolean contains(String list, String name) {
        for (String item : list.split(",")) if (name.equals(item.trim())) return true;
        return false;
    }

    private static void content(Map<String, Probe> probes) {
        add(probes, new Probe("services.content.api", "services", "in_scope", false, 6000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final Context context = session.activity;
                final ContentResolver cr = context.getContentResolver();
                Integer rows = (Integer) s.call(CR, "query", "settings", new Step() {
                    Object run() {
                        Cursor cursor = cr.query(Settings.System.CONTENT_URI, new String[] {Settings.System.NAME, Settings.System.VALUE},
                                Settings.System.NAME + "=?", new String[] {Settings.System.SCREEN_BRIGHTNESS}, null);
                        if (cursor == null) return null;
                        try { return cursor.getCount(); } finally { cursor.close(); }
                    }
                });
                final Uri missing = Uri.parse("content://org.ogplay.probe.missing/items");
                Object unknown = s.call(CR, "query", "unknown_authority", new Step() { Object run() { return cr.query(missing, null, null, null, null); } });
                s.call(CR, "getType", "unknown_authority", new Step() { Object run() { return cr.getType(missing); } });
                Object client = s.call(CR, "acquireContentProviderClient", "unknown_authority", new Step() {
                    Object run() { return cr.acquireContentProviderClient("org.ogplay.probe.missing"); }
                });
                final File file = new File(context.getCacheDir(), "content-probe.bin");
                FileOutputStream output = new FileOutputStream(file);
                try { output.write(new byte[] {19, 4, 4}); } finally { output.close(); }
                Integer fileBytes = (Integer) s.call(CR, "openInputStream", "file", new Step() {
                    Object run() throws Exception { return readAll(cr.openInputStream(Uri.fromFile(file))); }
                });
                file.delete();
                final Uri resource = Uri.parse(ContentResolver.SCHEME_ANDROID_RESOURCE + "://" + context.getPackageName() + "/" + R.mipmap.probe_icon);
                Integer png = (Integer) s.call(CR, "openInputStream", "android_resource", new Step() {
                    Object run() throws Exception {
                        InputStream input = cr.openInputStream(resource);
                        try { return input.read() == 0x89 && input.read() == 'P' ? 1 : 0; } finally { input.close(); }
                    }
                });
                final Uri local = Uri.parse("content://org.ogplay.probe.notify/item");
                final CountDownLatch changed = new CountDownLatch(1);
                final ContentObserver observer = new ContentObserver(new Handler(Looper.getMainLooper())) {
                    @Override public void onChange(boolean selfChange) { changed.countDown(); }
                };
                s.call(CR, "registerContentObserver", new Step() { Object run() { cr.registerContentObserver(local, true, observer); return true; } });
                s.call(CR, "notifyChange", new Step() { Object run() { cr.notifyChange(local, null); return true; } });
                boolean delivered = changed.await(1500, TimeUnit.MILLISECONDS);
                s.call(CR, "unregisterContentObserver", new Step() { Object run() { cr.unregisterContentObserver(observer); return true; } });
                s.call(CR, "validateSyncExtrasBundle", new Step() {
                    Object run() { Bundle extras = new Bundle(); extras.putString("probe", "19"); ContentResolver.validateSyncExtrasBundle(extras); return true; }
                });
                s.throwsType(CR, "validateSyncExtrasBundle", "invalid", IllegalArgumentException.class, new Step() {
                    Object run() {
                        Bundle extras = new Bundle();
                        extras.putParcelable("probe", new Intent());
                        ContentResolver.validateSyncExtrasBundle(extras);
                        return true;
                    }
                });
                s.require("settings_query_returns_row", rows != null && rows == 1)
                        .require("unknown_authority_query_null", unknown == null)
                        .require("unknown_authority_client_null", client == null)
                        .require("file_uri_stream", fileBytes != null && fileBytes == 3)
                        .require("resource_uri_stream_png", png != null && png == 1)
                        .require("observer_notified", delivered);
                return s.outcome(this, obj("settings_rows", 1, "unknown_authority", null, "file_bytes", 3, "resource", "PNG",
                        "observer_notified_within_ms", 1500), "ContentResolver serves settings, files, resources and change notifications");
            }
        });
    }

    private static int readAll(InputStream input) throws Exception {
        try {
            int count = 0;
            while (input.read() >= 0) count++;
            return count;
        } finally {
            input.close();
        }
    }

    private static void activity(Map<String, Probe> probes) {
        add(probes, new Probe("services.activity.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final ActivityManager am = (ActivityManager) session.activity.getSystemService(Context.ACTIVITY_SERVICE);
                final ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
                s.call(AM, "getMemoryInfo", new Step() { Object run() { am.getMemoryInfo(memory); return memory.availMem; } });
                Integer memoryClass = (Integer) s.call(AM, "getMemoryClass", new Step() { Object run() { return am.getMemoryClass(); } });
                Integer large = (Integer) s.call(AM, "getLargeMemoryClass", new Step() { Object run() { return am.getLargeMemoryClass(); } });
                s.call(AM, "isLowRamDevice", new Step() { Object run() { return am.isLowRamDevice(); } });
                @SuppressWarnings("unchecked") List<ActivityManager.RunningAppProcessInfo> processes =
                        (List<ActivityManager.RunningAppProcessInfo>) s.call(AM, "getRunningAppProcesses", new Step() { Object run() { return am.getRunningAppProcesses(); } });
                @SuppressWarnings("unchecked") List<ActivityManager.RunningTaskInfo> tasks =
                        (List<ActivityManager.RunningTaskInfo>) s.call(AM, "getRunningTasks", new Step() { Object run() { return am.getRunningTasks(1); } });
                @SuppressWarnings("unchecked") List<ActivityManager.RecentTaskInfo> recent =
                        (List<ActivityManager.RecentTaskInfo>) s.call(AM, "getRecentTasks", new Step() { Object run() { return am.getRecentTasks(5, 0); } });
                s.call(AM, "getRunningServices", new Step() { Object run() { return am.getRunningServices(20); } });
                Debug.MemoryInfo[] pss = (Debug.MemoryInfo[]) s.call(AM, "getProcessMemoryInfo", new Step() {
                    Object run() { return am.getProcessMemoryInfo(new int[] {Process.myPid()}); }
                });
                ConfigurationInfo config = (ConfigurationInfo) s.call(AM, "getDeviceConfigurationInfo", new Step() { Object run() { return am.getDeviceConfigurationInfo(); } });
                Integer iconSize = (Integer) s.call(AM, "getLauncherLargeIconSize", new Step() { Object run() { return am.getLauncherLargeIconSize(); } });
                Integer iconDensity = (Integer) s.call(AM, "getLauncherLargeIconDensity", new Step() { Object run() { return am.getLauncherLargeIconDensity(); } });
                Boolean monkey = (Boolean) s.call(AM, "isUserAMonkey", new Step() { Object run() { return ActivityManager.isUserAMonkey(); } });
                Boolean harness = (Boolean) s.call(AM, "isRunningInTestHarness", new Step() { Object run() { return ActivityManager.isRunningInTestHarness(); } });
                final ActivityManager.RunningAppProcessInfo state = new ActivityManager.RunningAppProcessInfo();
                s.call(AM, "getMyMemoryState", new Step() { Object run() { ActivityManager.getMyMemoryState(state); return state.importance; } });
                boolean foreground = false;
                if (processes != null) for (ActivityManager.RunningAppProcessInfo item : processes)
                    foreground |= item.pid == Process.myPid()
                            && item.importance == ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND;
                String self = session.activity.getPackageName();
                boolean recentSelf = false;
                if (recent != null) for (ActivityManager.RecentTaskInfo item : recent)
                    recentSelf |= item.baseIntent != null && item.baseIntent.getComponent() != null
                            && self.equals(item.baseIntent.getComponent().getPackageName());
                s.require("memory_info_valid", memory.availMem > 0 && memory.totalMem >= memory.availMem && memory.threshold > 0)
                        .require("memory_class_positive", memoryClass != null && memoryClass > 0)
                        .require("large_memory_class_not_smaller", large != null && memoryClass != null && large >= memoryClass)
                        .require("own_process_foreground", foreground)
                        .require("top_task_is_self", tasks != null && !tasks.isEmpty() && tasks.get(0).topActivity != null
                                && self.equals(tasks.get(0).topActivity.getPackageName()))
                        .require("recent_tasks_include_self", recentSelf)
                        .require("own_pss_reported", pss != null && pss.length == 1 && pss[0].getTotalPss() >= 0)
                        .require("gles2_and_finger_touch", config != null && config.reqGlEsVersion >= 0x20000
                                && config.reqTouchScreen == android.content.res.Configuration.TOUCHSCREEN_FINGER)
                        .require("launcher_icon_metrics", iconSize != null && iconSize > 0 && iconDensity != null && iconDensity > 0)
                        .require("not_monkey_or_harness", Boolean.FALSE.equals(monkey) && Boolean.FALSE.equals(harness))
                        .require("my_memory_state_foreground", state.importance == ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND);
                return s.outcome(this, obj("memory", "consistent", "own_process", "foreground", "top_task", self),
                        "ActivityManager describes this foreground process and the device memory");
            }
        });
    }

    private static void power(Map<String, Probe> probes) {
        add(probes, new Probe("services.power.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final PowerManager pm = (PowerManager) session.activity.getSystemService(Context.POWER_SERVICE);
                Boolean screen = (Boolean) s.call(POWER, "isScreenOn", new Step() { Object run() { return pm.isScreenOn(); } });
                final PowerManager.WakeLock counted = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "OGProbe:counted");
                Boolean twiceOnce = (Boolean) s.call(WAKE, "release", "counted_2_1", new Step() {
                    Object run() { counted.acquire(); counted.acquire(); counted.release(); return counted.isHeld(); }
                });
                Boolean twiceTwice = (Boolean) s.call(WAKE, "release", "counted_2_2", new Step() { Object run() { counted.release(); return counted.isHeld(); } });
                s.throwsType(WAKE, "release", "under_locked", RuntimeException.class, new Step() { Object run() { counted.release(); return true; } });
                final PowerManager.WakeLock plain = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "OGProbe:plain");
                Boolean uncounted = (Boolean) s.call(WAKE, "setReferenceCounted", new Step() {
                    Object run() { plain.setReferenceCounted(false); plain.acquire(); plain.acquire(); plain.release(); return plain.isHeld(); }
                });
                final PowerManager.WakeLock timed = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "OGProbe:timed");
                Boolean timedHeld = (Boolean) s.call(WAKE, "acquire", "timeout", new Step() { Object run() { timed.acquire(200); return timed.isHeld(); } });
                SystemClock.sleep(700);
                boolean timedReleased = !timed.isHeld();
                if (timed.isHeld()) timed.release();
                final PowerManager.WakeLock screenLock = pm.newWakeLock(PowerManager.SCREEN_BRIGHT_WAKE_LOCK
                        | PowerManager.ACQUIRE_CAUSES_WAKEUP | PowerManager.ON_AFTER_RELEASE, "OGProbe:screen");
                s.call(WAKE, "acquire", "screen_bright", new Step() { Object run() { screenLock.acquire(); boolean held = screenLock.isHeld(); screenLock.release(); return held; } });
                s.throwsType(POWER, "newWakeLock", "invalid_level", IllegalArgumentException.class, new Step() {
                    Object run() { return pm.newWakeLock(0x7fff, "OGProbe:invalid"); }
                });
                s.require("screen_on", Boolean.TRUE.equals(screen))
                        .require("counted_lock_still_held_after_one_release", Boolean.TRUE.equals(twiceOnce))
                        .require("counted_lock_released_after_balanced_release", Boolean.FALSE.equals(twiceTwice))
                        .require("uncounted_lock_released_by_one_release", Boolean.FALSE.equals(uncounted))
                        .require("timed_lock_held_then_expires", Boolean.TRUE.equals(timedHeld) && timedReleased);
                return s.outcome(this, obj("screen_on", true, "reference_counting", "API 19", "timeout_ms", 200,
                        "under_locked_release", "RuntimeException"), "WakeLock reference counting and timeouts follow API 19");
            }
        });
    }

    private static void notification(Map<String, Probe> probes) {
        add(probes, new Probe("services.notification.api", "services", "in_scope", true, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final Context context = session.activity;
                final NotificationManager nm = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
                final PendingIntent content = PendingIntent.getActivity(context, 4, new Intent(context, ProbeActivity.class), 0);
                final Bitmap large = Bitmap.createBitmap(64, 64, Bitmap.Config.ARGB_8888);
                final long when = 1388534400000L;
                final Notification built = (Notification) s.call(BUILDER, "build", new Step() {
                    Object run() {
                        return new Notification.Builder(context).setContentTitle("Probe").setContentText("API 19").setContentInfo("19")
                                .setSmallIcon(android.R.drawable.stat_notify_more).setLargeIcon(large).setTicker("Probe ticker")
                                .setWhen(when).setOngoing(true).setAutoCancel(true).setContentIntent(content).setNumber(7)
                                .setDefaults(Notification.DEFAULT_LIGHTS).setPriority(Notification.PRIORITY_LOW).setProgress(100, 19, false)
                                .setStyle(new Notification.BigTextStyle().bigText("Probe big text")).build();
                    }
                });
                s.call(BUILDER, "getNotification", new Step() {
                    Object run() { return new Notification.Builder(context).setSmallIcon(android.R.drawable.stat_notify_more).getNotification(); }
                });
                final Notification legacy = new Notification(android.R.drawable.stat_notify_more, "Legacy", when);
                s.call(NOTIFICATION, "setLatestEventInfo", new Step() { Object run() { legacy.setLatestEventInfo(context, "Legacy", "Text", content); return true; } });
                s.call(NM, "notify", "id", new Step() { Object run() { nm.notify(1904, built); return true; } });
                s.call(NM, "notify", "tag", new Step() { Object run() { nm.notify("probe", 1905, legacy); return true; } });
                s.call(NM, "cancel", "id", new Step() { Object run() { nm.cancel(1904); return true; } });
                s.call(NM, "cancel", "tag", new Step() { Object run() { nm.cancel("probe", 1905); return true; } });
                s.call(NM, "cancelAll", new Step() { Object run() { nm.cancelAll(); return true; } });
                content.cancel();
                s.require("builder_fields", built != null && built.when == when && built.number == 7 && built.icon == android.R.drawable.stat_notify_more
                                && built.contentIntent == content && "Probe ticker".equals(String.valueOf(built.tickerText)))
                        .require("builder_flags", built != null && (built.flags & Notification.FLAG_ONGOING_EVENT) != 0
                                && (built.flags & Notification.FLAG_AUTO_CANCEL) != 0 && (built.flags & Notification.FLAG_SHOW_LIGHTS) != 0)
                        .require("builder_extras", built != null && built.extras != null
                                && "Probe".equals(String.valueOf(built.extras.getCharSequence(Notification.EXTRA_TITLE))))
                        .require("legacy_fields", legacy.contentIntent == content && legacy.when == when);
                return s.outcome(this, obj("builder_fields", "round-trip", "legacy_api", "setLatestEventInfo", "posted_and_cancelled", true),
                        "Notification builders keep their fields and NotificationManager accepts posts and cancellations");
            }
        });
    }

    private static void scheduling(Map<String, Probe> probes) {
        add(probes, new Probe("services.pending_intent.api", "services", "in_scope", false, 6000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final Context context = session.activity;
                final String action = "org.ogplay.probe.PENDING";
                final CountDownLatch direct = new CountDownLatch(1);
                final CountDownLatch pending = new CountDownLatch(1);
                final String[] extra = new String[1];
                BroadcastReceiver receiver = new BroadcastReceiver() {
                    @Override public void onReceive(Context ignored, Intent intent) {
                        if ("pending".equals(intent.getStringExtra("route"))) {
                            extra[0] = intent.getStringExtra("fill");
                            pending.countDown();
                        } else {
                            direct.countDown();
                        }
                    }
                };
                context.registerReceiver(receiver, new IntentFilter(action), null, new Handler(Looper.getMainLooper()));
                try {
                    s.call(CONTEXT, "sendBroadcast", new Step() { Object run() { context.sendBroadcast(new Intent(action).putExtra("route", "direct")); return true; } });
                    boolean directDelivered = direct.await(1500, TimeUnit.MILLISECONDS);
                    final Intent target = new Intent(action).putExtra("route", "pending");
                    final PendingIntent first = (PendingIntent) s.call(PI, "getBroadcast", new Step() { Object run() { return PendingIntent.getBroadcast(context, 31, target, 0); } });
                    PendingIntent second = (PendingIntent) s.call(PI, "getBroadcast", "same", new Step() { Object run() { return PendingIntent.getBroadcast(context, 31, target, 0); } });
                    Object absent = s.call(PI, "getBroadcast", "no_create", new Step() {
                        Object run() { return PendingIntent.getBroadcast(context, 32, target, PendingIntent.FLAG_NO_CREATE); }
                    });
                    s.call(PI, "getActivity", new Step() { Object run() { return PendingIntent.getActivity(context, 33, new Intent(context, ProbeActivity.class), 0); } });
                    s.call(PI, "getService", new Step() { Object run() { return PendingIntent.getService(context, 34, new Intent(context, RemoteProbeService.class), 0); } });
                    String targetPackage = (String) s.call(PI, "getTargetPackage", new Step() { Object run() { return first == null ? null : first.getTargetPackage(); } });
                    String creator = (String) s.call(PI, "getCreatorPackage", new Step() { Object run() { return first == null ? null : first.getCreatorPackage(); } });
                    s.call(PI, "getIntentSender", new Step() { Object run() { return first == null ? null : first.getIntentSender(); } });
                    s.call(PI, "send", new Step() {
                        Object run() throws Exception { first.send(context, 0, new Intent().putExtra("fill", "filled")); return true; }
                    });
                    boolean pendingDelivered = pending.await(1500, TimeUnit.MILLISECONDS);
                    s.call(PI, "cancel", new Step() { Object run() { first.cancel(); return true; } });
                    Object cancelled = s.call(PI, "getBroadcast", "after_cancel", new Step() {
                        Object run() { return PendingIntent.getBroadcast(context, 31, target, PendingIntent.FLAG_NO_CREATE); }
                    });
                    s.require("direct_broadcast_delivered", directDelivered)
                            .require("same_request_is_equal", first != null && first.equals(second))
                            .require("no_create_returns_null", absent == null)
                            .require("target_and_creator_are_self", context.getPackageName().equals(targetPackage)
                                    && context.getPackageName().equals(creator))
                            .require("pending_broadcast_delivered_with_fill_in", pendingDelivered && "filled".equals(extra[0]))
                            .require("cancelled_is_gone", cancelled == null);
                } finally {
                    context.unregisterReceiver(receiver);
                }
                return s.outcome(this, obj("broadcast_delivery_ms", 1500, "same_request", "equal", "fill_in_extra", "filled",
                        "after_cancel", null), "PendingIntent identity, delivery and cancellation follow API 19");
            }
        });
        add(probes, new Probe("services.alarm.api", "services", "in_scope", false, 7000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final Context context = session.activity;
                final AlarmManager am = (AlarmManager) context.getSystemService(Context.ALARM_SERVICE);
                final String action = "org.ogplay.probe.ALARM";
                final CountDownLatch fired = new CountDownLatch(1);
                BroadcastReceiver receiver = new BroadcastReceiver() {
                    @Override public void onReceive(Context ignored, Intent intent) { fired.countDown(); }
                };
                context.registerReceiver(receiver, new IntentFilter(action), null, new Handler(Looper.getMainLooper()));
                final PendingIntent future = PendingIntent.getBroadcast(context, 41, new Intent(action).putExtra("kind", "future"), 0);
                final PendingIntent soon = PendingIntent.getBroadcast(context, 42, new Intent(action).putExtra("kind", "soon"), 0);
                final long now = SystemClock.elapsedRealtime();
                final long hour = 3600000L;
                try {
                    s.call(ALARM, "set", "rtc", new Step() { Object run() { am.set(AlarmManager.RTC, System.currentTimeMillis() + hour, future); return true; } });
                    s.call(ALARM, "setRepeating", new Step() { Object run() { am.setRepeating(AlarmManager.ELAPSED_REALTIME, now + hour, hour, future); return true; } });
                    s.call(ALARM, "setInexactRepeating", new Step() {
                        Object run() { am.setInexactRepeating(AlarmManager.ELAPSED_REALTIME, now + hour, AlarmManager.INTERVAL_FIFTEEN_MINUTES, future); return true; }
                    });
                    s.call(ALARM, "setExact", new Step() { Object run() { am.setExact(AlarmManager.ELAPSED_REALTIME_WAKEUP, now + hour, future); return true; } });
                    s.call(ALARM, "setWindow", new Step() { Object run() { am.setWindow(AlarmManager.RTC_WAKEUP, System.currentTimeMillis() + hour, hour, future); return true; } });
                    s.call(ALARM, "cancel", "future", new Step() { Object run() { am.cancel(future); return true; } });
                    s.call(ALARM, "setExact", "deliver", new Step() {
                        Object run() { am.setExact(AlarmManager.ELAPSED_REALTIME, SystemClock.elapsedRealtime() + 300, soon); return true; }
                    });
                    boolean delivered = fired.await(3000, TimeUnit.MILLISECONDS);
                    s.require("short_alarm_delivered", delivered);
                } finally {
                    am.cancel(future);
                    am.cancel(soon);
                    future.cancel();
                    soon.cancel();
                    context.unregisterReceiver(receiver);
                }
                return s.outcome(this, obj("scheduling_calls", "accepted", "alarm_after_ms", 300, "delivered_within_ms", 3000),
                        "AlarmManager accepts every API 19 scheduling form and delivers a short alarm");
            }
        });
    }

    private static void inflater(Map<String, Probe> probes) {
        add(probes, new Probe("services.layout_inflater.api", "services", "in_scope", true, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final Context context = session.activity;
                final LayoutInflater inflater = (LayoutInflater) s.call(INFLATER, "from", new Step() { Object run() { return LayoutInflater.from(context); } });
                Object service = context.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
                View view = (View) s.call(INFLATER, "inflate", new Step() { Object run() { return inflater.inflate(R.layout.probe_measured, null); } });
                final LayoutInflater clone = (LayoutInflater) s.call(INFLATER, "cloneInContext", new Step() { Object run() { return inflater.cloneInContext(context); } });
                Object cloneContext = s.call(INFLATER, "getContext", new Step() { Object run() { return clone.getContext(); } });
                final LayoutInflater.Factory factory = new LayoutInflater.Factory() {
                    @Override public View onCreateView(String name, Context viewContext, android.util.AttributeSet attrs) { return null; }
                };
                s.call(INFLATER, "setFactory", new Step() { Object run() { clone.setFactory(factory); return true; } });
                Object installed = s.call(INFLATER, "getFactory", new Step() { Object run() { return clone.getFactory(); } });
                s.throwsType(INFLATER, "setFactory", "twice", IllegalStateException.class, new Step() { Object run() { clone.setFactory(factory); return true; } });
                s.require("from_is_service", inflater != null && inflater == service)
                        .require("inflated_view", view instanceof ViewGroup && ((ViewGroup) view).getChildCount() > 0)
                        .require("clone_uses_context", cloneContext == context)
                        .require("factory_installed", installed != null);
                return s.outcome(this, obj("from", "service instance", "inflate", "ViewGroup", "second_setFactory", "IllegalStateException"),
                        "LayoutInflater service, cloning and factories follow API 19");
            }
        });
    }
}

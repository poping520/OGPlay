package org.ogplay.probe;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.accounts.AccountManagerFuture;
import android.accounts.OnAccountsUpdateListener;
import android.app.AppOpsManager;
import android.app.DownloadManager;
import android.app.KeyguardManager;
import android.app.SearchManager;
import android.app.WallpaperManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.hardware.ConsumerIrManager;
import android.hardware.input.InputManager;
import android.hardware.usb.UsbManager;
import android.net.Uri;
import android.net.wifi.p2p.WifiP2pManager;
import android.nfc.NfcManager;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.os.UserManager;
import android.print.PrintManager;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.View;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.CaptioningManager;
import android.view.inputmethod.InputMethodManager;
import android.view.textservice.SpellCheckerSession;
import android.view.textservice.SuggestionsInfo;
import android.view.textservice.TextServicesManager;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.ogplay.probe.ApiSweep.Step;

import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.Probe.obj;

/** API sweeps for input, accessibility, accounts and the less common managers returned by getSystemService. */
final class ServiceOtherProbes {
    private static final String IMM = "Landroid/view/inputmethod/InputMethodManager;";
    private static final String A11Y = "Landroid/view/accessibility/AccessibilityManager;";
    private static final String KEYGUARD = "Landroid/app/KeyguardManager;";
    private static final String CLIP = "Landroid/content/ClipboardManager;";
    private static final String ACCOUNTS = "Landroid/accounts/AccountManager;";
    private static final String DOWNLOAD = "Landroid/app/DownloadManager;";
    private static final String SEARCH = "Landroid/app/SearchManager;";
    private static final String APPOPS = "Landroid/app/AppOpsManager;";
    private static final String USER = "Landroid/os/UserManager;";
    private static final String INPUT = "Landroid/hardware/input/InputManager;";
    private static final String INPUT_DEVICE = "Landroid/view/InputDevice;";
    private static final String CAPTION = "Landroid/view/accessibility/CaptioningManager;";
    private static final String PRINT = "Landroid/print/PrintManager;";
    private static final String USB = "Landroid/hardware/usb/UsbManager;";
    private static final String IR = "Landroid/hardware/ConsumerIrManager;";
    private static final String WALLPAPER = "Landroid/app/WallpaperManager;";
    private static final String NFC = "Landroid/nfc/NfcManager;";
    private static final String TEXT = "Landroid/view/textservice/TextServicesManager;";
    private static final String P2P = "Landroid/net/wifi/p2p/WifiP2pManager;";
    private static final String CAMERA = "Landroid/hardware/Camera;";

    static void register(Map<String, Probe> probes) {
        input(probes);
        accessibility(probes);
        clipboard(probes);
        accounts(probes);
        platform(probes);
        rare(probes);
    }

    private static void input(Map<String, Probe> probes) {
        add(probes, new Probe("services.input_method.api", "services", "in_scope", true, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final InputMethodManager imm = (InputMethodManager) session.activity.getSystemService(Context.INPUT_METHOD_SERVICE);
                final View decor = session.activity.getWindow().getDecorView();
                List<?> installed = (List<?>) s.call(IMM, "getInputMethodList", new Step() { Object run() { return imm.getInputMethodList(); } });
                List<?> enabled = (List<?>) s.call(IMM, "getEnabledInputMethodList", new Step() { Object run() { return imm.getEnabledInputMethodList(); } });
                s.call(IMM, "isActive", new Step() { Object run() { return imm.isActive(); } });
                s.call(IMM, "isActive", "view", new Step() { Object run() { return imm.isActive(decor); } });
                s.call(IMM, "isAcceptingText", new Step() { Object run() { return imm.isAcceptingText(); } });
                s.call(IMM, "isFullscreenMode", new Step() { Object run() { return imm.isFullscreenMode(); } });
                s.call(IMM, "getCurrentInputMethodSubtype", new Step() { Object run() { return imm.getCurrentInputMethodSubtype(); } });
                s.call(IMM, "showSoftInput", new Step() { Object run() { return imm.showSoftInput(decor, 0); } });
                s.call(IMM, "hideSoftInputFromWindow", new Step() { Object run() { return imm.hideSoftInputFromWindow(decor.getWindowToken(), 0); } });
                s.call(IMM, "toggleSoftInput", new Step() { Object run() { imm.toggleSoftInput(0, 0); imm.toggleSoftInput(0, 0); return true; } });
                s.call(IMM, "restartInput", new Step() { Object run() { imm.restartInput(decor); return true; } });
                s.require("enabled_subset_of_installed", installed != null && enabled != null && installed.containsAll(enabled));
                return s.outcome(this, obj("enabled_methods", "subset of installed", "soft_input_calls", "return normally"),
                        "InputMethodManager queries and soft-keyboard requests return normally");
            }
        });
        add(probes, new Probe("services.input.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final InputManager im = (InputManager) session.activity.getSystemService(Context.INPUT_SERVICE);
                int[] ids = (int[]) s.call(INPUT, "getInputDeviceIds", new Step() { Object run() { return im.getInputDeviceIds(); } });
                int[] legacy = (int[]) s.call(INPUT_DEVICE, "getDeviceIds", new Step() { Object run() { return InputDevice.getDeviceIds(); } });
                boolean touch = false;
                if (ids != null) for (final int id : ids) {
                    final InputDevice device = (InputDevice) s.call(INPUT, "getInputDevice", String.valueOf(id), new Step() { Object run() { return im.getInputDevice(id); } });
                    if (device == null) continue;
                    s.call(INPUT_DEVICE, "getName", String.valueOf(id), new Step() { Object run() { return device.getName(); } });
                    Integer sources = (Integer) s.call(INPUT_DEVICE, "getSources", String.valueOf(id), new Step() { Object run() { return device.getSources(); } });
                    s.call(INPUT_DEVICE, "getKeyboardType", String.valueOf(id), new Step() { Object run() { return device.getKeyboardType(); } });
                    touch |= sources != null && (sources & InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN;
                }
                Object virtual = s.call(INPUT_DEVICE, "getDevice", "virtual_keyboard", new Step() { Object run() { return InputDevice.getDevice(KeyCharacterMap.VIRTUAL_KEYBOARD); } });
                Object map = s.call("Landroid/view/KeyCharacterMap;", "load", new Step() { Object run() { return KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD); } });
                final InputManager.InputDeviceListener listener = new InputManager.InputDeviceListener() {
                    @Override public void onInputDeviceAdded(int deviceId) {}
                    @Override public void onInputDeviceRemoved(int deviceId) {}
                    @Override public void onInputDeviceChanged(int deviceId) {}
                };
                s.call(INPUT, "registerInputDeviceListener", new Step() { Object run() { im.registerInputDeviceListener(listener, new Handler(Looper.getMainLooper())); return true; } });
                s.call(INPUT, "unregisterInputDeviceListener", new Step() { Object run() { im.unregisterInputDeviceListener(listener); return true; } });
                s.require("devices_listed", ids != null && ids.length > 0)
                        .require("legacy_ids_match", ids != null && legacy != null && java.util.Arrays.equals(ids, legacy))
                        .require("touchscreen_device_present", touch)
                        .require("virtual_keyboard_present", virtual != null && map != null);
                return s.outcome(this, obj("devices", "touchscreen and virtual keyboard", "legacy_ids", "same as InputManager"),
                        "InputManager lists the touchscreen and the virtual keyboard used by key events");
            }
        });
    }

    private static void accessibility(Map<String, Probe> probes) {
        add(probes, new Probe("services.accessibility.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final AccessibilityManager am = (AccessibilityManager) session.activity.getSystemService(Context.ACCESSIBILITY_SERVICE);
                Boolean enabled = (Boolean) s.call(A11Y, "isEnabled", new Step() { Object run() { return am.isEnabled(); } });
                Boolean touch = (Boolean) s.call(A11Y, "isTouchExplorationEnabled", new Step() { Object run() { return am.isTouchExplorationEnabled(); } });
                List<?> installed = (List<?>) s.call(A11Y, "getInstalledAccessibilityServiceList", new Step() { Object run() { return am.getInstalledAccessibilityServiceList(); } });
                List<?> active = (List<?>) s.call(A11Y, "getEnabledAccessibilityServiceList", new Step() {
                    Object run() { return am.getEnabledAccessibilityServiceList(android.accessibilityservice.AccessibilityServiceInfo.FEEDBACK_ALL_MASK); }
                });
                s.call(A11Y, "getAccessibilityServiceList", new Step() { Object run() { return am.getAccessibilityServiceList(); } });
                final AccessibilityManager.AccessibilityStateChangeListener listener = new AccessibilityManager.AccessibilityStateChangeListener() {
                    @Override public void onAccessibilityStateChanged(boolean state) {}
                };
                Boolean added = (Boolean) s.call(A11Y, "addAccessibilityStateChangeListener", new Step() { Object run() { return am.addAccessibilityStateChangeListener(listener); } });
                Boolean removed = (Boolean) s.call(A11Y, "removeAccessibilityStateChangeListener", new Step() { Object run() { return am.removeAccessibilityStateChangeListener(listener); } });
                if (Boolean.FALSE.equals(enabled)) {
                    s.throwsType(A11Y, "sendAccessibilityEvent", "disabled", IllegalStateException.class, new Step() {
                        Object run() { am.sendAccessibilityEvent(AccessibilityEvent.obtain(AccessibilityEvent.TYPE_VIEW_CLICKED)); return true; }
                    });
                    s.throwsType(A11Y, "interrupt", "disabled", IllegalStateException.class, new Step() { Object run() { am.interrupt(); return true; } });
                }
                s.require("lists_present", installed != null && active != null)
                        .require("enabled_requires_active_service", enabled != null && (!enabled || active != null && !active.isEmpty()))
                        .require("touch_exploration_requires_enabled", touch != null && enabled != null && (!touch || enabled))
                        .require("state_listener_add_remove", Boolean.TRUE.equals(added) && Boolean.TRUE.equals(removed));
                return s.outcome(this, obj("lists", "present", "listener", "added and removed",
                        "disabled_sendAccessibilityEvent", "IllegalStateException"), "AccessibilityManager follows the API 19 state rules");
            }
        });
        add(probes, new Probe("services.keyguard.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final KeyguardManager km = (KeyguardManager) session.activity.getSystemService(Context.KEYGUARD_SERVICE);
                Boolean restricted = (Boolean) s.call(KEYGUARD, "inKeyguardRestrictedInputMode", new Step() { Object run() { return km.inKeyguardRestrictedInputMode(); } });
                Boolean locked = (Boolean) s.call(KEYGUARD, "isKeyguardLocked", new Step() { Object run() { return km.isKeyguardLocked(); } });
                s.call(KEYGUARD, "isKeyguardSecure", new Step() { Object run() { return km.isKeyguardSecure(); } });
                s.call(KEYGUARD, "newKeyguardLock", new Step() { Object run() { return km.newKeyguardLock("OGProbe"); } });
                s.require("foreground_not_restricted", Boolean.FALSE.equals(restricted)).require("foreground_not_locked", Boolean.FALSE.equals(locked));
                return s.outcome(this, obj("restricted_input", false, "locked", false), "A foreground app runs with the keyguard dismissed");
            }
        });
    }

    private static void clipboard(Map<String, Probe> probes) {
        add(probes, new Probe("services.clipboard.api", "services", "in_scope", false, 5000) {
            @SuppressWarnings("deprecation")
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final ClipboardManager cm = (ClipboardManager) session.activity.getSystemService(Context.CLIPBOARD_SERVICE);
                final ClipData previous = cm.getPrimaryClip();
                final CountDownLatch changed = new CountDownLatch(1);
                final ClipboardManager.OnPrimaryClipChangedListener listener = new ClipboardManager.OnPrimaryClipChangedListener() {
                    @Override public void onPrimaryClipChanged() { changed.countDown(); }
                };
                boolean notified;
                CharSequence legacy;
                String described;
                Boolean has;
                CharSequence coerced;
                try {
                    s.call(CLIP, "addPrimaryClipChangedListener", new Step() { Object run() { cm.addPrimaryClipChangedListener(listener); return true; } });
                    s.call(CLIP, "setPrimaryClip", new Step() { Object run() { cm.setPrimaryClip(ClipData.newPlainText("probe", "OGProbe clip")); return true; } });
                    notified = changed.await(1500, TimeUnit.MILLISECONDS);
                    has = (Boolean) s.call(CLIP, "hasPrimaryClip", new Step() { Object run() { return cm.hasPrimaryClip(); } });
                    described = (String) s.call(CLIP, "getPrimaryClipDescription", new Step() {
                        Object run() { return cm.getPrimaryClipDescription() == null ? null : cm.getPrimaryClipDescription().getMimeType(0); }
                    });
                    coerced = (CharSequence) s.call(CLIP, "getPrimaryClip", new Step() {
                        Object run() { ClipData clip = cm.getPrimaryClip(); return clip == null ? null : clip.getItemAt(0).coerceToText(session.activity); }
                    });
                    s.call(CLIP, "setText", new Step() { Object run() { cm.setText("OGProbe legacy"); return true; } });
                    legacy = (CharSequence) s.call(CLIP, "getText", new Step() { Object run() { return cm.getText(); } });
                    s.call(CLIP, "hasText", new Step() { Object run() { return cm.hasText(); } });
                    s.call(CLIP, "removePrimaryClipChangedListener", new Step() { Object run() { cm.removePrimaryClipChangedListener(listener); return true; } });
                } finally {
                    if (previous != null) cm.setPrimaryClip(previous);
                }
                ClipData intent = (ClipData) s.call("Landroid/content/ClipData;", "newIntent", new Step() {
                    Object run() { return ClipData.newIntent("probe", new Intent(Intent.ACTION_VIEW, Uri.parse("http://localhost/"))); }
                });
                s.require("listener_notified", notified)
                        .require("clip_present_and_plain", Boolean.TRUE.equals(has) && "text/plain".equals(described))
                        .require("clip_text_round_trip", coerced != null && "OGProbe clip".equals(coerced.toString()))
                        .require("legacy_text_round_trip", legacy != null && "OGProbe legacy".equals(legacy.toString()))
                        .require("intent_clip", intent != null && intent.getItemAt(0).getIntent() != null);
                return s.outcome(this, obj("listener_notified_within_ms", 1500, "mime", "text/plain", "legacy_text", "round-trip"),
                        "ClipboardManager round-trips clips through both APIs and notifies listeners");
            }
        });
    }

    private static void accounts(Map<String, Probe> probes) {
        add(probes, new Probe("services.account.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final Context context = session.activity;
                final AccountManager am = (AccountManager) s.call(ACCOUNTS, "get", new Step() { Object run() { return AccountManager.get(context); } });
                Object service = context.getSystemService(Context.ACCOUNT_SERVICE);
                if (am == null) return s.outcome(this, obj("manager", "present"), "AccountManager is available");
                Account[] all = (Account[]) s.call(ACCOUNTS, "getAccounts", new Step() { Object run() { return am.getAccounts(); } });
                Account[] google = (Account[]) s.call(ACCOUNTS, "getAccountsByType", new Step() { Object run() { return am.getAccountsByType("com.google"); } });
                Object types = s.call(ACCOUNTS, "getAuthenticatorTypes", new Step() { Object run() { return am.getAuthenticatorTypes(); } });
                final OnAccountsUpdateListener listener = new OnAccountsUpdateListener() { @Override public void onAccountsUpdated(Account[] accounts) {} };
                s.call(ACCOUNTS, "addOnAccountsUpdatedListener", new Step() {
                    Object run() { am.addOnAccountsUpdatedListener(listener, new Handler(Looper.getMainLooper()), false); return true; }
                });
                s.call(ACCOUNTS, "removeOnAccountsUpdatedListener", new Step() { Object run() { am.removeOnAccountsUpdatedListener(listener); return true; } });
                s.call(ACCOUNTS, "invalidateAuthToken", new Step() { Object run() { am.invalidateAuthToken("com.google", "ogplay-probe-token"); return true; } });
                Account[] featured = (Account[]) s.call(ACCOUNTS, "getAccountsByTypeAndFeatures", new Step() {
                    Object run() throws Exception {
                        AccountManagerFuture<Account[]> future = am.getAccountsByTypeAndFeatures("com.google", null, null, null);
                        return future.getResult(2, TimeUnit.SECONDS);
                    }
                });
                int count = all == null ? -1 : all.length;
                boolean subset = google != null && all != null && java.util.Arrays.asList(all).containsAll(java.util.Arrays.asList(google));
                s.require("get_is_service", am == service)
                        .require("account_arrays_present", all != null && google != null && featured != null && types != null)
                        .require("typed_accounts_subset", subset)
                        .require("featured_accounts_match_type", featured != null && google != null && featured.length == google.length);
                return s.outcome(this, obj("arrays", "non-null", "account_count", count >= 0 ? "observed" : "missing",
                        "invalidateAuthToken", "accepted"), "AccountManager answers queries with arrays instead of failing");
            }
        });
    }

    private static void platform(Map<String, Probe> probes) {
        add(probes, new Probe("services.search.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final SearchManager sm = (SearchManager) session.activity.getSystemService(Context.SEARCH_SERVICE);
                final ComponentName self = new ComponentName(session.activity, ProbeActivity.class);
                Object info = s.call(SEARCH, "getSearchableInfo", new Step() { Object run() { return sm.getSearchableInfo(self); } });
                Object global = s.call(SEARCH, "getSearchablesInGlobalSearch", new Step() { Object run() { return sm.getSearchablesInGlobalSearch(); } });
                s.call(SEARCH, "getGlobalSearchActivity", new Step() { Object run() { return sm.getGlobalSearchActivity(); } });
                s.require("non_searchable_activity_is_null", info == null).require("global_list_present", global != null);
                return s.outcome(this, obj("searchable_info", null, "global_searchables", "list"), "SearchManager answers for an activity without search metadata");
            }
        });
        add(probes, new Probe("services.appops.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final AppOpsManager ops = (AppOpsManager) session.activity.getSystemService(Context.APP_OPS_SERVICE);
                final String self = session.activity.getPackageName();
                s.call(APPOPS, "checkPackage", "self", new Step() { Object run() { ops.checkPackage(Process.myUid(), self); return true; } });
                s.throwsType(APPOPS, "checkPackage", "foreign", SecurityException.class, new Step() { Object run() { ops.checkPackage(Process.myUid(), "android"); return true; } });
                Integer coarse = (Integer) s.call(APPOPS, "checkOpNoThrow", "coarse_location", new Step() {
                    Object run() { return ops.checkOpNoThrow(AppOpsManager.OPSTR_COARSE_LOCATION, Process.myUid(), self); }
                });
                Integer fine = (Integer) s.call(APPOPS, "noteOp", "fine_location", new Step() {
                    Object run() { return ops.noteOp(AppOpsManager.OPSTR_FINE_LOCATION, Process.myUid(), self); }
                });
                s.require("location_ops_allowed", coarse != null && coarse == AppOpsManager.MODE_ALLOWED && fine != null && fine == AppOpsManager.MODE_ALLOWED);
                return s.outcome(this, obj("own_package", "accepted", "foreign_package", "SecurityException", "location_ops", "MODE_ALLOWED"),
                        "AppOpsManager verifies package ownership and allows the declared operations");
            }
        });
        add(probes, new Probe("services.user.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final UserManager um = (UserManager) session.activity.getSystemService(Context.USER_SERVICE);
                Boolean goat = (Boolean) s.call(USER, "isUserAGoat", new Step() { Object run() { return um.isUserAGoat(); } });
                android.os.Bundle restrictions = (android.os.Bundle) s.call(USER, "getUserRestrictions", new Step() { Object run() { return um.getUserRestrictions(); } });
                Boolean restricted = restrictions == null ? null : restrictions.getBoolean(UserManager.DISALLOW_INSTALL_APPS, false);
                Long serial = (Long) s.call(USER, "getSerialNumberForUser", new Step() { Object run() { return um.getSerialNumberForUser(Process.myUserHandle()); } });
                Object user = s.call(USER, "getUserForSerialNumber", new Step() { Object run() { return um.getUserForSerialNumber(0); } });
                s.require("not_goat", Boolean.FALSE.equals(goat)).require("restrictions_present", restrictions != null)
                        .require("owner_unrestricted", Boolean.FALSE.equals(restricted))
                        .require("owner_serial_zero", serial != null && serial == 0 && Process.myUserHandle().equals(user));
                return s.outcome(this, obj("user", "owner", "serial", 0, "restrictions", "none"), "UserManager describes the single owner user");
            }
        });
    }

    private static void rare(Map<String, Probe> probes) {
        add(probes, new Probe("services.minor.api", "services", "info", false, 6000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final Context context = session.activity;
                final PrintManager print = (PrintManager) context.getSystemService(Context.PRINT_SERVICE);
                s.call(PRINT, "getPrintJobs", new Step() { Object run() { return print.getPrintJobs(); } });
                final UsbManager usb = (UsbManager) context.getSystemService(Context.USB_SERVICE);
                s.call(USB, "getDeviceList", new Step() { Object run() { return usb.getDeviceList(); } });
                s.call(USB, "getAccessoryList", new Step() { Object run() { return usb.getAccessoryList(); } });
                final ConsumerIrManager ir = (ConsumerIrManager) context.getSystemService(Context.CONSUMER_IR_SERVICE);
                s.call(IR, "hasIrEmitter", new Step() { Object run() { return ir.hasIrEmitter(); } });
                final WallpaperManager wallpaper = WallpaperManager.getInstance(context);
                s.call(WALLPAPER, "getDesiredMinimumWidth", new Step() { Object run() { return wallpaper.getDesiredMinimumWidth(); } });
                s.call(WALLPAPER, "getDesiredMinimumHeight", new Step() { Object run() { return wallpaper.getDesiredMinimumHeight(); } });
                s.call(WALLPAPER, "getWallpaperInfo", new Step() { Object run() { return wallpaper.getWallpaperInfo(); } });
                final NfcManager nfc = (NfcManager) context.getSystemService(Context.NFC_SERVICE);
                s.call(NFC, "getDefaultAdapter", new Step() { Object run() { return nfc == null ? "no manager" : nfc.getDefaultAdapter(); } });
                s.call(CAMERA, "getNumberOfCameras", new Step() { Object run() { return android.hardware.Camera.getNumberOfCameras(); } });
                final DownloadManager downloads = (DownloadManager) context.getSystemService(Context.DOWNLOAD_SERVICE);
                s.call(DOWNLOAD, "query", new Step() {
                    Object run() {
                        Cursor cursor = downloads.query(new DownloadManager.Query());
                        if (cursor == null) return null;
                        try { return cursor.getCount(); } finally { cursor.close(); }
                    }
                });
                s.call(DOWNLOAD, "getMaxBytesOverMobile", new Step() { Object run() { return DownloadManager.getMaxBytesOverMobile(context); } });
                final Long id = (Long) s.call(DOWNLOAD, "enqueue", new Step() {
                    Object run() {
                        DownloadManager.Request request = new DownloadManager.Request(Uri.parse("http://127.0.0.1:9/ogprobe.bin"));
                        request.setDestinationInExternalFilesDir(context, null, "ogprobe.bin");
                        return downloads.enqueue(request);
                    }
                });
                if (id != null) s.call(DOWNLOAD, "remove", new Step() { Object run() { return downloads.remove(id); } });
                return s.outcome(this, null, "Less common managers answer read-only queries without failing");
            }
        });
        add(probes, new Probe("services.minor.looper_api", "services", "info", true, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final Context context = session.activity;
                // CaptioningManager, SpellCheckerSession and WifiP2pManager create Handlers on the calling thread.
                final CaptioningManager captions = (CaptioningManager) context.getSystemService(Context.CAPTIONING_SERVICE);
                s.call(CAPTION, "isEnabled", new Step() { Object run() { return captions.isEnabled(); } });
                s.call(CAPTION, "getFontScale", new Step() { Object run() { return captions.getFontScale(); } });
                s.call(CAPTION, "getLocale", new Step() { Object run() { return captions.getLocale(); } });
                s.call(CAPTION, "getUserStyle", new Step() { Object run() { return captions.getUserStyle(); } });
                final TextServicesManager text = (TextServicesManager) context.getSystemService(Context.TEXT_SERVICES_MANAGER_SERVICE);
                final SpellCheckerSession.SpellCheckerSessionListener listener = new SpellCheckerSession.SpellCheckerSessionListener() {
                    @Override public void onGetSuggestions(SuggestionsInfo[] results) {}
                    @Override public void onGetSentenceSuggestions(android.view.textservice.SentenceSuggestionsInfo[] results) {}
                };
                final SpellCheckerSession spelling = (SpellCheckerSession) s.call(TEXT, "newSpellCheckerSession", new Step() {
                    Object run() { return text.newSpellCheckerSession(null, Locale.US, listener, false); }
                });
                if (spelling != null) spelling.close();
                final WifiP2pManager p2p = (WifiP2pManager) context.getSystemService(Context.WIFI_P2P_SERVICE);
                s.call(P2P, "initialize", new Step() { Object run() { return p2p.initialize(context, Looper.getMainLooper(), null); } });
                return s.outcome(this, null, "Managers that need the main Looper construct their sessions without failing");
            }
        });
    }
}

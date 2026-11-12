package org.ogplay.webview;

import android.content.ContentResolver;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.webkit.CookieManager;
import android.webkit.GeolocationPermissions;
import android.webkit.ValueCallback;
import android.webkit.WebIconDatabase;
import android.webkit.WebStorage;
import android.webkit.WebView;
import android.webkit.WebViewDatabase;
import android.webkit.WebViewFactoryProvider;
import android.webkit.WebViewProvider;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/** Owns the process-wide WebView services returned by the ROM factory. */
public final class WebServices implements WebViewFactoryProvider {
    private final Cookies cookies = new Cookies();
    private final Geolocation geolocation = new Geolocation();
    private final Storage storage = new Storage();
    private final Database database = new Database();
    private final Icons icons = new Icons();
    private final Statics statics = new Statics() {
        @Override public String findAddress(String text) { return null; }
        @Override public void setPlatformNotificationsEnabled(boolean enabled) {}
        @Override public String getDefaultUserAgent(Context context) { return Settings.DEFAULT_USER_AGENT; }
        @Override public void freeMemoryForTests() {}
        @Override public void setWebContentsDebuggingEnabled(boolean enabled) {}
    };

    @Override public Statics getStatics() { return statics; }
    @Override public WebViewProvider createWebView(WebView view, WebView.PrivateAccess access) {
        return new Provider(view, access, database);
    }
    @Override public CookieManager getCookieManager() { return cookies; }
    @Override public GeolocationPermissions getGeolocationPermissions() { return geolocation; }
    @Override public WebStorage getWebStorage() { return storage; }
    @Override public WebViewDatabase getWebViewDatabase(Context context) { return database; }
    @Override public WebIconDatabase getWebIconDatabase() { return icons; }

    private static <T> void deliver(final ValueCallback<T> callback, final T value) {
        if (callback == null) return;
        Looper looper = Looper.myLooper();
        new Handler(looper == null ? Looper.getMainLooper() : looper).post(new Runnable() {
            @Override public void run() { callback.onReceiveValue(value); }
        });
    }

    private static final class Geolocation extends GeolocationPermissions {
        private final Set<String> allowed = new HashSet<String>();

        @Override public synchronized void getOrigins(ValueCallback<Set<String>> callback) {
            deliver(callback, new HashSet<String>(allowed));
        }
        @Override public synchronized void getAllowed(String origin, ValueCallback<Boolean> callback) {
            deliver(callback, allowed.contains(origin));
        }
        @Override public synchronized void allow(String origin) {
            if (origin != null) allowed.add(origin);
        }
        @Override public synchronized void clear(String origin) { allowed.remove(origin); }
        @Override public synchronized void clearAll() { allowed.clear(); }
    }

    private static final class Storage extends WebStorage {
        private static final class OriginQuota extends Origin {
            OriginQuota(String origin, long quota) { super(origin, quota, 0); }
        }

        private final Map<String, Long> quotas = new HashMap<String, Long>();

        @Override public synchronized void getOrigins(ValueCallback<Map> callback) {
            Map<String, Origin> origins = new HashMap<String, Origin>();
            for (Map.Entry<String, Long> entry : quotas.entrySet()) {
                origins.put(entry.getKey(), new OriginQuota(entry.getKey(), entry.getValue()));
            }
            deliver(callback, origins);
        }
        @Override public void getUsageForOrigin(String origin, ValueCallback<Long> callback) {
            deliver(callback, 0L);
        }
        @Override public synchronized void getQuotaForOrigin(String origin, ValueCallback<Long> callback) {
            Long quota = quotas.get(origin);
            deliver(callback, quota == null ? 0L : quota);
        }
        @Override public synchronized void setQuotaForOrigin(String origin, long quota) {
            if (origin != null && quota >= 0) quotas.put(origin, quota);
        }
        @Override public synchronized void deleteOrigin(String origin) { quotas.remove(origin); }
        @Override public synchronized void deleteAllData() { quotas.clear(); }
    }

    static final class Database extends WebViewDatabase {
        private final Map<String, Map<String, String>> passwords = new HashMap<String, Map<String, String>>();
        private final Map<String, Map<String, String[]>> auth = new HashMap<String, Map<String, String[]>>();

        synchronized void savePassword(String host, String user, String password) {
            Map<String, String> users = passwords.get(host);
            if (users == null) {
                users = new HashMap<String, String>();
                passwords.put(host, users);
            }
            users.put(user, password);
        }
        synchronized void saveHttpAuth(String host, String realm, String user, String password) {
            Map<String, String[]> realms = auth.get(host);
            if (realms == null) {
                realms = new HashMap<String, String[]>();
                auth.put(host, realms);
            }
            realms.put(realm, new String[] { user, password });
        }
        synchronized String[] httpAuth(String host, String realm) {
            Map<String, String[]> realms = auth.get(host);
            String[] credentials = realms == null ? null : realms.get(realm);
            return credentials == null ? null : credentials.clone();
        }
        @Override public synchronized boolean hasUsernamePassword() { return !passwords.isEmpty(); }
        @Override public synchronized void clearUsernamePassword() { passwords.clear(); }
        @Override public synchronized boolean hasHttpAuthUsernamePassword() { return !auth.isEmpty(); }
        @Override public synchronized void clearHttpAuthUsernamePassword() { auth.clear(); }
        @Override public boolean hasFormData() { return false; }
        @Override public void clearFormData() {}
    }

    private static final class Icons extends WebIconDatabase {
        private final Map<String, Integer> retained = new HashMap<String, Integer>();
        private boolean open;

        @Override public synchronized void open(String path) { open = path != null; }
        @Override public synchronized void close() { open = false; }
        @Override public synchronized void removeAllIcons() { retained.clear(); }
        @Override public void requestIconForPageUrl(String url, IconListener listener) {}
        @Override public void bulkRequestIconForPageUrl(ContentResolver resolver, String where, IconListener listener) {}
        @Override public synchronized void retainIconForPageUrl(String url) {
            if (!open || url == null) return;
            Integer count = retained.get(url);
            retained.put(url, count == null ? 1 : count + 1);
        }
        @Override public synchronized void releaseIconForPageUrl(String url) {
            if (!open) return;
            Integer count = retained.get(url);
            if (count == null || count <= 1) retained.remove(url);
            else retained.put(url, count - 1);
        }
    }
}

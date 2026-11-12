package org.ogplay.webview;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.webkit.WebViewClient;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class NoWebEngine implements WebEngine {
    private static final String TAG = "OGPlayWebView";
    private static final String ASSET_PREFIX = "/android_asset/";
    private static final String RESOURCE_PREFIX = "/android_res/";
    private static final String HISTORY_URLS = "ogplay.webview.urls";
    private static final String HISTORY_LOAD_URLS = "ogplay.webview.loadUrls";
    private static final String HISTORY_TITLES = "ogplay.webview.titles";
    private static final String HISTORY_LOCAL = "ogplay.webview.local";
    private static final String HISTORY_INDEX = "ogplay.webview.index";
    private static final Pattern HTML_TITLE =
            Pattern.compile("<title(?:\\s[^>]*)?>\\s*(.*?)\\s*</title\\s*>",
                    Pattern.CASE_INSENSITIVE | Pattern.DOTALL);

    private static final class Page {
        final String loadUrl;
        final String url;
        final String title;
        final boolean inline;

        Page(String loadUrl, String url, String title, boolean inline) {
            this.loadUrl = loadUrl;
            this.url = url;
            this.title = title;
            this.inline = inline;
        }
    }

    private final Context context;
    private final Handler handler;
    private final Settings settings;
    private final Events events;
    private final ArrayList<Page> history = new ArrayList<Page>();
    private int index = -1;
    private Runnable pending;
    private boolean destroyed;

    NoWebEngine(Context context, Handler handler, Settings settings, Events events) {
        this.context = context;
        this.handler = handler;
        this.settings = settings;
        this.events = events;
    }

    @Override public void load(String url, Map<String, String> headers, byte[] postData) {
        if (url == null) return;
        if (url.regionMatches(true, 0, "javascript:", 0, 11)) {
            executeJavaScript(url.substring(11), null);
            return;
        }
        enqueue(new Page(url, url, null, false), -1);
    }

    @Override public void loadData(String baseUrl, String data, String mimeType,
            String encoding, String historyUrl) {
        String url = baseUrl;
        String entry = historyUrl;
        if (baseUrl == null || baseUrl.regionMatches(true, 0, "data:", 0, 5)) {
            url = "data:" + (mimeType == null ? "text/html" : mimeType)
                    + ("base64".equalsIgnoreCase(encoding) ? ";base64" : "")
                    + "," + (data == null ? "" : data);
            entry = url;
        }
        String title = null;
        if (data != null && (mimeType == null || mimeType.equalsIgnoreCase("text/html"))) {
            Matcher match = HTML_TITLE.matcher(data);
            if (match.find()) title = match.group(1);
        }
        enqueue(new Page(url, entry == null ? "about:blank" : entry, title, true), -1);
    }

    @Override public void executeJavaScript(String script, ScriptResult result) {
        if (!destroyed) Log.w(TAG, "dexvm.webview_javascript");
    }

    // API 19 uses this hint for navigator.onLine only. Network routing uses the local service.
    @Override public void setNetworkAvailable(boolean available) {}

    @Override public void stop() {
        if (pending != null) {
            handler.removeCallbacks(pending);
            pending = null;
        }
    }

    @Override public void reload() {
        if (index >= 0) enqueue(history.get(index), index);
    }

    @Override public boolean canNavigate(int steps) {
        long next = (long) index + steps;
        return next >= 0 && next < history.size();
    }

    @Override public void navigate(int steps) {
        if (canNavigate(steps)) enqueue(history.get(index + steps), index + steps);
    }

    @Override public void clearHistory() {
        Page current = index < 0 ? null : history.get(index);
        stop();
        history.clear();
        index = -1;
        if (current != null) {
            history.add(current);
            index = 0;
        }
    }

    @Override public WebHistory snapshot() {
        ArrayList<WebHistory.Item> items = new ArrayList<WebHistory.Item>(history.size());
        for (Page page : history) items.add(new WebHistory.Item(page.url, page.title));
        return new WebHistory(items, index);
    }

    @Override public boolean saveState(Bundle bundle) {
        if (bundle == null || index < 0) return false;
        int size = history.size();
        String[] urls = new String[size];
        String[] loadUrls = new String[size];
        String[] titles = new String[size];
        boolean[] inline = new boolean[size];
        for (int i = 0; i < size; ++i) {
            Page page = history.get(i);
            urls[i] = page.url;
            loadUrls[i] = page.loadUrl;
            titles[i] = page.title;
            inline[i] = page.inline;
        }
        bundle.putStringArray(HISTORY_URLS, urls);
        bundle.putStringArray(HISTORY_LOAD_URLS, loadUrls);
        bundle.putStringArray(HISTORY_TITLES, titles);
        bundle.putBooleanArray(HISTORY_LOCAL, inline);
        bundle.putInt(HISTORY_INDEX, index);
        return true;
    }

    @Override public boolean restoreState(Bundle bundle) {
        if (bundle == null) return false;
        String[] urls = bundle.getStringArray(HISTORY_URLS);
        String[] loadUrls = bundle.getStringArray(HISTORY_LOAD_URLS);
        String[] titles = bundle.getStringArray(HISTORY_TITLES);
        boolean[] inline = bundle.getBooleanArray(HISTORY_LOCAL);
        int restoredIndex = bundle.getInt(HISTORY_INDEX, -1);
        if (urls == null || loadUrls == null || titles == null || inline == null
                || urls.length != loadUrls.length || urls.length != titles.length
                || urls.length != inline.length || restoredIndex < 0 || restoredIndex >= urls.length) {
            return false;
        }
        for (int i = 0; i < urls.length; ++i) {
            if (urls[i] == null || loadUrls[i] == null) return false;
        }
        stop();
        history.clear();
        for (int i = 0; i < urls.length; ++i) {
            history.add(new Page(loadUrls[i], urls[i], titles[i], inline[i]));
        }
        index = restoredIndex;
        return true;
    }

    @Override public void destroy() {
        stop();
        history.clear();
        index = -1;
        destroyed = true;
    }

    private void enqueue(final Page page, final int historyIndex) {
        if (destroyed) return;
        stop();
        pending = new Runnable() {
            @Override public void run() {
                if (pending != this) return;
                events.pageStarted(page.loadUrl);
                if (pending != this) return;
                int error = checkLoad(page);
                if (error != 0) {
                    events.receivedError(error, describeError(error), page.loadUrl);
                } else {
                    Log.w(TAG, "WebView.pageRendering");
                }
                if (pending != this) return;
                if (historyIndex >= 0) {
                    index = historyIndex;
                } else {
                    while (history.size() > index + 1) history.remove(history.size() - 1);
                    history.add(page);
                    index++;
                }
                if (page.title != null) events.titleChanged(page.title);
                if (pending != this) return;
                events.progressChanged(100);
                if (pending != this) return;
                pending = null;
                events.pageFinished(page.loadUrl);
            }
        };
        if (!handler.post(pending)) pending = null;
    }

    private int checkLoad(Page page) {
        if (page.inline) return 0;
        Uri uri = Uri.parse(page.loadUrl);
        String scheme = uri.getScheme();
        if ("http".equalsIgnoreCase(scheme) || "https".equalsIgnoreCase(scheme)
                || "ftp".equalsIgnoreCase(scheme)) {
            if (hasNetwork()) Log.w(TAG, "dexvm.webview_engine_unavailable");
            return WebViewClient.ERROR_HOST_LOOKUP;
        }
        if ("about:blank".equalsIgnoreCase(page.loadUrl) || "data".equalsIgnoreCase(scheme)) return 0;
        if ("file".equalsIgnoreCase(scheme)) return checkFile(uri);
        if ("content".equalsIgnoreCase(scheme)) {
            if (!settings.getAllowContentAccess()) return WebViewClient.ERROR_FILE_NOT_FOUND;
            try {
                InputStream stream = context.getContentResolver().openInputStream(uri);
                if (stream == null) return WebViewClient.ERROR_FILE_NOT_FOUND;
                stream.close();
                return 0;
            } catch (IOException e) {
                return WebViewClient.ERROR_FILE_NOT_FOUND;
            } catch (SecurityException e) {
                return WebViewClient.ERROR_FILE_NOT_FOUND;
            }
        }
        return WebViewClient.ERROR_UNSUPPORTED_SCHEME;
    }

    private int checkFile(Uri uri) {
        String path = uri.getPath();
        if (path == null) return WebViewClient.ERROR_FILE_NOT_FOUND;
        try {
            if (path.startsWith(ASSET_PREFIX)) {
                context.getAssets().open(path.substring(ASSET_PREFIX.length())).close();
                return 0;
            }
            if (path.startsWith(RESOURCE_PREFIX)) {
                String name = path.substring(RESOURCE_PREFIX.length());
                int extension = name.lastIndexOf('.');
                if (extension >= 0) name = name.substring(0, extension);
                if (context.getResources().getIdentifier(name, null, context.getPackageName()) != 0) {
                    return 0;
                }
            } else if (settings.getAllowFileAccess() && new File(path).isFile()) {
                return 0;
            }
        } catch (IOException e) {
            return WebViewClient.ERROR_FILE_NOT_FOUND;
        }
        return WebViewClient.ERROR_FILE_NOT_FOUND;
    }

    private boolean hasNetwork() {
        try {
            ConnectivityManager connectivity =
                    (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
            NetworkInfo info = connectivity == null ? null : connectivity.getActiveNetworkInfo();
            return info != null && info.isConnected();
        } catch (SecurityException e) {
            // An app without ACCESS_NETWORK_STATE starts with the simulated offline default.
            return false;
        } catch (UnsupportedOperationException e) {
            // The local connectivity service rejects policies without offline facts.
            return true;
        }
    }

    private static String describeError(int code) {
        switch (code) {
            case WebViewClient.ERROR_HOST_LOOKUP: return "net::ERR_NAME_NOT_RESOLVED";
            case WebViewClient.ERROR_FILE_NOT_FOUND: return "net::ERR_FILE_NOT_FOUND";
            default: return "net::ERR_UNKNOWN_URL_SCHEME";
        }
    }
}

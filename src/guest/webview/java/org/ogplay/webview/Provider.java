package org.ogplay.webview;

import android.graphics.Bitmap;
import android.graphics.Picture;
import android.net.http.SslCertificate;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.print.PrintDocumentAdapter;
import android.util.Log;
import android.view.View;
import android.webkit.DownloadListener;
import android.webkit.ValueCallback;
import android.webkit.WebBackForwardList;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.webkit.WebViewProvider;

import java.io.BufferedWriter;
import java.io.File;
import java.util.HashMap;
import java.util.Map;

final class Provider implements WebViewProvider, WebEngine.Events {
    private final WebView view;
    private final WebViewDelegates delegates;
    private final WebServices.Database database;
    private final Map<String, Object> interfaces = new HashMap<String, Object>();
    private Handler handler;
    private Settings settings;
    private WebEngine engine;
    private WebViewClient client;
    private WebChromeClient chrome;
    private WebView.FindListener findListener;
    private Runnable pendingFind;
    private SslCertificate certificate;
    private boolean destroyed;
    private boolean privateBrowsing;
    private boolean horizontalOverlay = true;
    private boolean verticalOverlay;
    private boolean paused;
    private int progress = 100;
    private float scale = 1.0f;
    private String loadingUrl;

    Provider(WebView view, WebView.PrivateAccess access, WebServices.Database database) {
        this.view = view;
        this.delegates = new WebViewDelegates(view, access);
        this.database = database;
    }

    @Override public void init(Map<String, Object> initialInterfaces, boolean privateBrowsing) {
        Looper looper = Looper.myLooper();
        handler = new Handler(looper == null ? Looper.getMainLooper() : looper);
        view.setWillNotDraw(false);
        settings = new Settings(view.getContext());
        engine = new NoWebEngine(view.getContext(), handler, settings, this);
        this.privateBrowsing = privateBrowsing;
        if (initialInterfaces != null) interfaces.putAll(initialInterfaces);
    }

    @Override public void destroy() {
        if (destroyed) return;
        destroyed = true;
        engine.destroy();
        clearMatches();
        interfaces.clear();
        client = null;
        chrome = null;
        findListener = null;
        certificate = null;
        loadingUrl = null;
        progress = 100;
    }

    @Override public ViewDelegate getViewDelegate() { return delegates; }
    @Override public ScrollDelegate getScrollDelegate() { return delegates; }
    @Override public WebSettings getSettings() { return settings; }

    @Override public void loadUrl(String url) { engine.load(url, null, null); }
    @Override public void loadUrl(String url, Map<String, String> headers) { engine.load(url, headers, null); }
    @Override public void postUrl(String url, byte[] data) { engine.load(url, null, data); }
    @Override public void loadData(String data, String mimeType, String encoding) {
        engine.loadData(null, data, mimeType, encoding, null);
    }
    @Override public void loadDataWithBaseURL(String baseUrl, String data, String mimeType,
            String encoding, String historyUrl) {
        engine.loadData(baseUrl == null ? "about:blank" : baseUrl, data, mimeType, encoding, historyUrl);
    }

    @Override public void evaluateJavaScript(String script, final ValueCallback<String> callback) {
        engine.executeJavaScript(script, callback == null ? null : new WebEngine.ScriptResult() {
            @Override public void complete(String value) { callback.onReceiveValue(value); }
        });
    }

    @Override public void setNetworkAvailable(boolean available) { engine.setNetworkAvailable(available); }
    @Override public void stopLoading() {
        engine.stop();
        loadingUrl = null;
    }
    @Override public void reload() { engine.reload(); }
    @Override public boolean canGoBack() { return engine.canNavigate(-1); }
    @Override public boolean canGoForward() { return engine.canNavigate(1); }
    @Override public boolean canGoBackOrForward(int steps) { return engine.canNavigate(steps); }
    @Override public void goBack() { engine.navigate(-1); }
    @Override public void goForward() { engine.navigate(1); }
    @Override public void goBackOrForward(int steps) { engine.navigate(steps); }
    @Override public void clearHistory() { engine.clearHistory(); }
    @Override public WebBackForwardList copyBackForwardList() { return engine.snapshot(); }
    @Override public WebBackForwardList saveState(Bundle bundle) {
        return !privateBrowsing && engine.saveState(bundle) ? engine.snapshot() : null;
    }
    @Override public WebBackForwardList restoreState(Bundle bundle) {
        if (destroyed || privateBrowsing || !engine.restoreState(bundle)) return null;
        loadingUrl = null;
        progress = 100;
        return engine.snapshot();
    }

    @Override public void pageStarted(String url) {
        loadingUrl = url;
        progress = 0;
        if (client != null) client.onPageStarted(view, url, null);
    }
    @Override public void progressChanged(int value) {
        progress = value;
        if (chrome != null) chrome.onProgressChanged(view, value);
    }
    @Override public void titleChanged(String title) {
        if (chrome != null) chrome.onReceivedTitle(view, title);
    }
    @Override public void receivedError(int code, String description, String url) {
        if (client != null) client.onReceivedError(view, code, description, url);
    }
    @Override public void pageFinished(String url) {
        loadingUrl = null;
        if (client != null) client.onPageFinished(view, url);
    }

    @Override public String getUrl() {
        return loadingUrl != null ? loadingUrl : getOriginalUrl();
    }
    @Override public String getOriginalUrl() {
        WebHistory.Item item = engine.snapshot().getCurrentItem();
        return item == null ? null : item.getOriginalUrl();
    }
    @Override public String getTitle() {
        WebHistory.Item item = engine.snapshot().getCurrentItem();
        return item == null ? null : item.getTitle();
    }
    @Override public int getProgress() { return progress; }

    @Override public void setWebViewClient(WebViewClient client) {
        if (!destroyed) this.client = client;
    }
    @Override public void setWebChromeClient(WebChromeClient chrome) {
        if (!destroyed) this.chrome = chrome;
    }
    @Override public void addJavascriptInterface(Object object, String name) {
        if (!destroyed && object != null) interfaces.put(name, object);
    }
    @Override public void removeJavascriptInterface(String name) { interfaces.remove(name); }

    @Override public float getScale() { return scale; }
    @Override public void setInitialScale(int percent) {
        if (!destroyed) scale = percent > 0 ? percent / 100.0f : 1.0f;
    }
    @Override public boolean canZoomIn() { return !destroyed && settings.supportZoom() && scale < 4.0f; }
    @Override public boolean canZoomOut() { return !destroyed && settings.supportZoom() && scale > 0.25f; }
    @Override public boolean zoomIn() {
        if (!canZoomIn()) return false;
        return changeScale(Math.min(4.0f, scale * 1.25f));
    }
    @Override public boolean zoomOut() {
        if (!canZoomOut()) return false;
        return changeScale(Math.max(0.25f, scale / 1.25f));
    }
    private boolean changeScale(final float value) {
        final float previous = scale;
        scale = value;
        handler.post(new Runnable() {
            @Override public void run() {
                if (!destroyed && client != null) client.onScaleChanged(view, previous, value);
            }
        });
        return true;
    }

    @Override public void setFindListener(WebView.FindListener listener) {
        if (!destroyed) findListener = listener;
    }
    @Override public int findAll(String query) {
        findAllAsync(query);
        return 0;
    }
    @Override public void findAllAsync(String query) {
        clearMatches();
        if (destroyed || query == null) return;
        pendingFind = new Runnable() {
            @Override public void run() {
                pendingFind = null;
                if (!destroyed && findListener != null) findListener.onFindResultReceived(0, 0, true);
            }
        };
        handler.post(pendingFind);
    }
    @Override public void findNext(boolean forward) {}
    @Override public boolean showFindDialog(String text, boolean showIme) { return false; }
    @Override public void notifyFindDialogDismissed() { clearMatches(); }
    @Override public void clearMatches() {
        if (pendingFind != null) {
            handler.removeCallbacks(pendingFind);
            pendingFind = null;
        }
    }

    @Override public void setHorizontalScrollbarOverlay(boolean overlay) { horizontalOverlay = overlay; }
    @Override public void setVerticalScrollbarOverlay(boolean overlay) { verticalOverlay = overlay; }
    @Override public boolean overlayHorizontalScrollbar() { return horizontalOverlay; }
    @Override public boolean overlayVerticalScrollbar() { return verticalOverlay; }
    @Override public int getVisibleTitleHeight() { return 0; }
    @Override public SslCertificate getCertificate() { return certificate; }
    @Override public void setCertificate(SslCertificate value) {
        if (!destroyed) certificate = value;
    }
    @Override public void savePassword(String host, String user, String password) {
        if (!destroyed && !privateBrowsing) database.savePassword(host, user, password);
    }
    @Override public void setHttpAuthUsernamePassword(String host, String realm, String user, String password) {
        if (!destroyed && !privateBrowsing) database.saveHttpAuth(host, realm, user, password);
    }
    @Override public String[] getHttpAuthUsernamePassword(String host, String realm) {
        return destroyed || privateBrowsing ? null : database.httpAuth(host, realm);
    }

    @Override public boolean isPrivateBrowsingEnabled() { return privateBrowsing; }
    @Override public boolean pageUp(boolean top) { return false; }
    @Override public boolean pageDown(boolean bottom) { return false; }
    @Override public void clearView() { view.invalidate(); }
    @Override public Picture capturePicture() { return new Picture(); }
    @Override public boolean savePicture(Bundle bundle, File destination) { return false; }
    @Override public boolean restorePicture(Bundle bundle, File source) { return false; }
    @Override public PrintDocumentAdapter createPrintDocumentAdapter() {
        Log.w("OGPlayWebView", "dexvm.webview_print_document");
        throw new UnsupportedOperationException("WebView printing requires a page engine");
    }
    @Override public void saveWebArchive(String filename) {
        Log.w("OGPlayWebView", "dexvm.webview_save_archive");
    }
    @Override public void saveWebArchive(String basename, boolean autoname, final ValueCallback<String> callback) {
        saveWebArchive(basename);
        if (!destroyed && callback != null) handler.post(new Runnable() {
            @Override public void run() {
                if (!destroyed) callback.onReceiveValue(null); // API 19 signals archive failure this way.
            }
        });
    }

    @Override public void invokeZoomPicker() {}
    @Override public View getZoomControls() { return null; }
    @Override public WebView.HitTestResult getHitTestResult() { return new WebView.HitTestResult(); }
    @Override public void requestFocusNodeHref(Message response) { sendEmptyPageResult(response); }
    @Override public void requestImageRef(Message response) { sendEmptyPageResult(response); }
    @Override public void documentHasImages(Message response) {
        if (response != null) response.arg1 = 0;
        sendEmptyPageResult(response);
    }
    private void sendEmptyPageResult(Message response) {
        if (!destroyed && response != null) response.sendToTarget();
    }

    @Override public Bitmap getFavicon() { return null; }
    @Override public String getTouchIconUrl() { return null; }
    @Override public int getContentHeight() { return 0; }
    @Override public int getContentWidth() { return 0; }
    @Override public void pauseTimers() {}
    @Override public void resumeTimers() {}
    @Override public void onPause() { paused = true; }
    @Override public void onResume() { paused = false; }
    @Override public boolean isPaused() { return paused; }
    @Override public void freeMemory() {}
    @Override public void clearCache(boolean includeDiskFiles) {}
    @Override public void clearFormData() {}
    @Override public void clearSslPreferences() {}
    @Override public void setDownloadListener(DownloadListener listener) {}
    @Override public void setPictureListener(WebView.PictureListener listener) {}
    @Override public void setMapTrackballToArrowKeys(boolean enabled) {}
    @Override public void flingScroll(int vx, int vy) {}
    @Override public void dumpViewHierarchyWithProperties(BufferedWriter writer, int level) {}
    @Override public View findHierarchyView(String className, int hashCode) { return null; }
}

package io.github.poping520.ogplay.launcher;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.ActivityInfo;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.webkit.ConsoleMessage;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.TextView;
import android.window.OnBackInvokedDispatcher;

import io.github.poping520.ogplay.R;
import io.github.poping520.ogplay.game.GameActivity;
import io.github.poping520.ogplay.runtime.RuntimeData;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Collections;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Game library: the bundled webui/gui page in a WebView, talking to the native launcher through
 * {@link LauncherBridge}. Pages come only from the staged runtime data under one private origin;
 * every other request and navigation is refused.
 */
public final class LauncherActivity extends Activity implements LauncherBridge.Host {
    private static final String TAG = "OGPlay";
    private static final String HOST = "launcher.ogplay.invalid";
    private static final String ORIGIN = "https://" + HOST;
    private static final String BRIDGE_SCRIPT = "ogplay-android-bridge.js";
    private static final int PICK_FILE = 1;
    private static final int PICK_DIRECTORY = 2;
    private static final long POLL_INTERVAL_MS = 500;

    // window.rpc for the page, resolved by deliver(); exits, the back key and the system bar insets enter the page
    // here too. The first line names the layout (phone or console), see bridgeSource().
    private static final String BRIDGE_SOURCE = String.join("\n",
            "(() => {",
            "  'use strict';",
            "  // The console library is laid out for desktop windows: a short landscape screen gets a wider layout",
            "  // viewport, scaled to fit, so the page is never shorter than MIN_HEIGHT CSS pixels. Phones use their own.",
            "  const MIN_HEIGHT = 640, viewport = document.querySelector('meta[name=viewport]');",
            "  if (window.__ogplayLayout !== 'phone' && viewport && innerHeight > 0 && innerHeight < MIN_HEIGHT && innerWidth > innerHeight)",
            "    viewport.content = 'width=' + Math.round(innerWidth * MIN_HEIGHT / innerHeight);",
            "  // The phone page is an app screen at its natural size: pinching never zooms it.",
            "  if (window.__ogplayLayout === 'phone' && viewport)",
            "    viewport.content = 'width=device-width, initial-scale=1, minimum-scale=1, maximum-scale=1, user-scalable=no';",
            "  // Heights of the status and navigation bars the phone page draws under, in CSS pixels.",
            "  window.__ogplayInsets = (top, bottom) => {",
            "    document.documentElement.style.setProperty('--safe-top', top + 'px');",
            "    document.documentElement.style.setProperty('--safe-bottom', bottom + 'px');",
            "  };",
            "  const host = window.ogplayHost, pending = new Map();",
            "  let next = 0;",
            "  window.rpc = request => new Promise(resolve => {",
            "    const id = ++next; pending.set(id, resolve); host.post(id, request);",
            "  });",
            "  window.__ogplayResolve = (id, response) => {",
            "    const resolve = pending.get(id); if (!resolve) return; pending.delete(id); resolve(response);",
            "  };",
            "  window.__ogplayExits = events => {",
            "    for (const detail of events) window.dispatchEvent(new CustomEvent('ogplay-exit', { detail }));",
            "  };",
            "  // The back key; true when the page used it (closed a dialog or view), else the launcher leaves.",
            "  window.__ogplayBack = () => {",
            "    const event = new CustomEvent('ogplay-back', { cancelable: true });",
            "    window.dispatchEvent(event);",
            "    return event.defaultPrevented;",
            "  };",
            "})();");

    private static final Map<String, String> MIME_TYPES = Map.ofEntries(
            Map.entry("html", "text/html"), Map.entry("js", "text/javascript"), Map.entry("css", "text/css"),
            Map.entry("json", "application/json"), Map.entry("txt", "text/plain"), Map.entry("png", "image/png"),
            Map.entry("svg", "image/svg+xml"), Map.entry("glb", "model/gltf-binary"),
            Map.entry("wasm", "application/wasm"), Map.entry("woff2", "font/woff2"));

    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());
    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            if (bridge != null) bridge.poll();
            main.postDelayed(this, POLL_INTERVAL_MS);
        }
    };
    private WebView web;
    private TextView status;
    private LauncherBridge bridge;
    private volatile File pageRoot;
    private long pendingPick;
    private ParcelFileDescriptor pickedFile;
    private SafTrees trees;
    // Phones (smallest width under 600dp) get the portrait phone layout drawn edge to edge; tablets the console one.
    private boolean phone;
    private int insetTop;
    private int insetBottom;

    @SuppressLint("SetJavaScriptEnabled")
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        phone = getResources().getConfiguration().smallestScreenWidthDp < 600;
        setRequestedOrientation(phone
                ? ActivityInfo.SCREEN_ORIENTATION_PORTRAIT : ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
            WebView.setWebContentsDebuggingEnabled(true);  // chrome://inspect for debug builds only
        }

        web = new WebView(this);
        web.setBackgroundColor(Color.rgb(0x09, 0x0c, 0x16));
        WebSettings settings = web.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setSupportMultipleWindows(false);
        settings.setJavaScriptCanOpenWindowsAutomatically(false);
        settings.setTextZoom(100);
        // Honour the page's viewport meta, which the bridge widens on short landscape screens, and fit it to the view.
        settings.setUseWideViewPort(true);
        settings.setLoadWithOverviewMode(true);
        settings.setSupportZoom(false);
        // Pages stop at their ends; the view's stretch would slide the whole UI past the screen edge.
        web.setOverScrollMode(View.OVER_SCROLL_NEVER);
        web.setWebViewClient(new PageClient());
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onConsoleMessage(ConsoleMessage message) {
                Log.println(message.messageLevel() == ConsoleMessage.MessageLevel.ERROR ? Log.ERROR : Log.INFO,
                        TAG, "webui: " + message.message() + " (" + message.sourceId() + ":" + message.lineNumber() + ")");
                return true;
            }
        });

        status = new TextView(this);
        status.setText(R.string.preparing_runtime);
        status.setTextColor(Color.rgb(0xc8, 0xcf, 0xe6));
        status.setPadding(48, 48, 48, 48);

        FrameLayout content = new FrameLayout(this);
        content.setBackgroundColor(Color.rgb(0x09, 0x0c, 0x16));
        content.addView(web, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        content.addView(status);
        if (phone && Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // The phone page paints under the system bars and keeps its controls clear of them (--safe-*); only the
            // keyboard shrinks the view. The WebView gets no insets of its own, so nothing is applied twice.
            getWindow().setDecorFitsSystemWindows(false);
            getWindow().setStatusBarColor(Color.TRANSPARENT);
            getWindow().setNavigationBarColor(Color.TRANSPARENT);
            getWindow().setNavigationBarContrastEnforced(false);
            content.setOnApplyWindowInsetsListener((view, insets) -> {
                android.graphics.Insets bars = insets.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                int keyboard = insets.getInsets(WindowInsets.Type.ime()).bottom;
                view.setPadding(0, 0, 0, keyboard);
                status.setPadding(48, 48 + bars.top, 48, 48);
                float density = getResources().getDisplayMetrics().density;
                insetTop = Math.round(bars.top / density);
                insetBottom = keyboard > 0 ? 0 : Math.round(bars.bottom / density);
                applyInsets();
                return WindowInsets.CONSUMED;
            });
        } else {
            content.setOnApplyWindowInsetsListener((view, insets) -> {
                view.setPadding(
                        insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
                return insets;
            });
        }
        setContentView(content);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::back);
        }
        trees = new SafTrees(getContentResolver());
        // Copies made by earlier versions, which staged picks in the cache, belong to no import now.
        io.execute(() -> {
            deleteTree(new File(getCacheDir(), "import"));
            deleteTree(new File(getCacheDir(), "import-data"));
        });
        io.execute(this::prepareRuntime);
    }

    @Override
    public void onConfigurationChanged(android.content.res.Configuration configuration) {
        super.onConfigurationChanged(configuration);
        // The page follows the system language through settings.get; the native side reads it from here.
        if (bridge != null) RuntimeData.publishEnvironment(this);
    }

    @Override
    protected void onResume() {
        super.onResume();
        main.removeCallbacks(poll);
        main.post(poll);
    }

    @Override
    protected void onPause() {
        main.removeCallbacks(poll);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        main.removeCallbacks(poll);
        if (bridge != null) bridge.close();
        closePickedFile();
        io.shutdown();
        web.destroy();
        super.onDestroy();
    }

    @SuppressWarnings("deprecation")
    @Override
    public void onBackPressed() {
        back();
    }

    private void applyInsets() {
        if (phone && bridge != null) evaluate("window.__ogplayInsets && window.__ogplayInsets(" + insetTop + "," + insetBottom + ")");
    }

    private String bridgeSource() {
        return "window.__ogplayLayout = '" + (phone ? "phone" : "console") + "';\n" + BRIDGE_SOURCE;
    }

    private void back() {
        if (bridge == null) {
            finish();
            return;
        }
        web.evaluateJavascript("window.__ogplayBack ? window.__ogplayBack() : false", handled -> {
            if (!"true".equals(handled)) finish();
        });
    }

    private void prepareRuntime() {
        try {
            File runtime = RuntimeData.prepare(this);
            runOnUiThread(() -> start(runtime));
        } catch (IOException error) {
            Log.e(TAG, "runtime data preparation failed", error);
            runOnUiThread(() -> status.setText(getString(R.string.prepare_runtime_failed, error.getMessage())));
        }
    }

    private void start(File runtime) {
        if (isDestroyed()) return;
        RuntimeData.publishEnvironment(this);
        pageRoot = new File(runtime, "data/webui/gui");
        bridge = new LauncherBridge(this);
        web.addJavascriptInterface(bridge, "ogplayHost");
        web.loadUrl(ORIGIN + "/index.html");
    }

    // LauncherBridge.Host

    @Override
    public void deliver(int id, String response) {
        evaluate("window.__ogplayResolve(" + id + "," + response + ")");
    }

    @Override
    public void dispatchExits(String events) {
        evaluate("window.__ogplayExits(" + events + ")");
    }

    private void evaluate(String script) {
        main.post(() -> {
            if (!isDestroyed()) web.evaluateJavascript(script, null);
        });
    }

    @Override
    public void launchGame(String[] arguments) {
        main.post(() -> startActivity(GameActivity.intent(this, arguments)));
    }

    @Override
    public void pickPath(long token, boolean directory) {
        main.post(() -> {
            if (pendingPick != 0) bridge.pickResult(pendingPick, null, null);
            pendingPick = token;
            if (directory) {
                startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_DIRECTORY);
                return;
            }
            // Any file: APKs and game archives (ZIP, RAR, .gpk) carry no dependable MIME type; the host reads the head.
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT)
                    .addCategory(Intent.CATEGORY_OPENABLE)
                    .setType("*/*");
            startActivityForResult(intent, PICK_FILE);
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if ((requestCode != PICK_FILE && requestCode != PICK_DIRECTORY) || pendingPick == 0) return;
        long token = pendingPick;
        pendingPick = 0;
        Uri uri = resultCode == RESULT_OK && data != null ? data.getData() : null;
        if (uri == null) {
            bridge.pickResult(token, null, null);
            return;
        }
        if (requestCode == PICK_DIRECTORY) {
            // Only a handle: the import lists and reads the files itself, nothing is copied here.
            bridge.pickResult(token, trees.register(uri), SafTrees.label(uri));
            return;
        }
        io.execute(() -> {
            // The picked file stays open here and the native side reads it through /proc/self/fd/<n>, so a
            // large APK or game archive is never copied before the import reads it.
            String path = null, label = null;
            try {
                ParcelFileDescriptor descriptor = getContentResolver().openFileDescriptor(uri, "r");
                if (descriptor != null) {
                    closePickedFile();
                    pickedFile = descriptor;
                    path = "/proc/self/fd/" + descriptor.getFd();
                    label = displayName(uri);
                }
            } catch (IOException | RuntimeException error) {
                Log.e(TAG, "cannot open the picked file", error);
            }
            bridge.pickResult(token, path, label);
        });
    }

    private synchronized void closePickedFile() {
        if (pickedFile == null) return;
        try {
            pickedFile.close();
        } catch (IOException error) {
            Log.w(TAG, "cannot close the previous pick", error);
        }
        pickedFile = null;
    }

    // LauncherBridge.Host: a picked directory's files, for the native import.

    @Override
    public byte[] listTree(String handle) {
        return trees.list(handle);
    }

    @Override
    public int openTreeFile(String handle, String path) {
        return trees.open(handle, path);
    }

    private static void deleteTree(File file) {
        File[] children = file.isDirectory() && !Files.isSymbolicLink(file.toPath()) ? file.listFiles() : null;
        if (children != null) for (File child : children) deleteTree(child);
        if (!file.delete() && file.exists()) Log.w(TAG, "cannot delete " + file);
    }

    private String displayName(Uri uri) {
        String name = null;
        try (Cursor cursor = getContentResolver().query(
                uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) name = cursor.getString(0);
        }
        if (name == null) name = "";
        name = name.replace('/', '_').replace('\0', '_').trim();
        return name.isEmpty() ? "picked" : name;
    }

    private final class PageClient extends WebViewClient {
        @Override
        public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
            return true;  // the launcher page never navigates
        }

        @Override
        public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
            Uri url = request.getUrl();
            if (!"https".equals(url.getScheme()) || !HOST.equals(url.getHost()) || pageRoot == null) {
                return refuse(403, "Forbidden");
            }
            String path = url.getPath() == null ? "/" : url.getPath();
            try {
                if (path.equals("/" + BRIDGE_SCRIPT)) {
                    return respond("js", new ByteArrayInputStream(bridgeSource().getBytes(StandardCharsets.UTF_8)));
                }
                File file = new File(pageRoot, path).getCanonicalFile();
                if (!file.getPath().startsWith(pageRoot.getCanonicalPath() + File.separator) || !file.isFile()) {
                    return refuse(404, "Not Found");
                }
                String extension = extension(file.getName());
                if (extension.equals("html")) {
                    // The bridge must define window.rpc before gui.js runs.
                    String html = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8)
                            .replace("<script src=\"./gui.js\">",
                                    "<script src=\"./" + BRIDGE_SCRIPT + "\"></script><script src=\"./gui.js\">");
                    return respond(extension, new ByteArrayInputStream(html.getBytes(StandardCharsets.UTF_8)));
                }
                return respond(extension, new FileInputStream(file));
            } catch (IOException error) {
                Log.e(TAG, "cannot serve " + path, error);
                return refuse(404, "Not Found");
            }
        }

        @Override
        public void onPageFinished(WebView view, String url) {
            status.setVisibility(View.GONE);
            applyInsets();
        }

        @Override
        public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) {
            Log.e(TAG, "WebView renderer gone; restarting the launcher");
            recreate();
            return true;
        }
    }

    private static String extension(String name) {
        int dot = name.lastIndexOf('.');
        return dot < 0 ? "" : name.substring(dot + 1).toLowerCase(Locale.ROOT);
    }

    private static WebResourceResponse respond(String extension, InputStream body) {
        String type = MIME_TYPES.getOrDefault(extension, "application/octet-stream");
        WebResourceResponse response = new WebResourceResponse(type, "UTF-8", body);
        response.setResponseHeaders(Collections.singletonMap("Cache-Control", "no-store"));
        return response;
    }

    private static WebResourceResponse refuse(int code, String reason) {
        return new WebResourceResponse("text/plain", "UTF-8", code, reason,
                Collections.emptyMap(), new ByteArrayInputStream(new byte[0]));
    }
}

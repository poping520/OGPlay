package org.poping520.ogplay.launcher;

import android.util.Log;
import android.webkit.JavascriptInterface;

import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

/**
 * The page's {@code window.rpc} bridge into the native GuiRpcService. Every native call runs on one
 * worker thread, as the desktop host runs them on its UI thread; answers go back through the host.
 */
final class LauncherBridge {
    /** What the launcher activity does for the native side and for the page. */
    interface Host {
        void deliver(int id, String response);

        void dispatchExits(String events);

        void launchGame(String[] arguments);

        /** Brings the running game's task back to the front. */
        void resumeGame();

        /** Asks the running game to quit, as its own close confirmation would. */
        void stopGame();

        /** Opens an http(s) page in the user's browser (the About page's project link). */
        void openUrl(String url);

        void pickPath(long token, boolean directory);

        /** "size\tpath\n" lines for a picked directory handle; null when unknown or unreadable. */
        byte[] listTree(String handle);

        /** A detached descriptor for one listed file, or -1. */
        int openTreeFile(String handle, String path);
    }

    private static final String TAG = "OGPlay";

    static {
        System.loadLibrary("ogplay_launcher");
    }

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Host host;
    private long handle;
    private String failure = "launcher is closing";

    /** Starts the native launcher on the worker; the environment must already name both roots. */
    LauncherBridge(Host host) {
        this.host = host;
        worker.execute(() -> {
            try {
                handle = nativeCreate(this);
            } catch (RuntimeException error) {
                Log.e(TAG, "native launcher failed to start", error);
                failure = String.valueOf(error.getMessage());
            }
        });
    }

    /** Called by the page through the injected rpc shim. */
    @JavascriptInterface
    public void post(int id, String request) {
        byte[] bytes = request.getBytes(StandardCharsets.UTF_8);
        run(() -> host.deliver(id, handle != 0 ? nativeHandle(handle, bytes) : failure(id)));
    }

    /** Reports the games that ended since the last poll as ogplay-exit events. */
    void poll() {
        run(() -> {
            if (handle == 0) return;
            String events = nativePoll(handle);
            if (!events.equals("[]")) host.dispatchExits(events);
        });
    }

    /**
     * The pending native picker's answer: a path or handle for the host and a label to show (file name,
     * /sdcard/…), or null when the user cancelled or the file could not be opened.
     */
    void pickResult(long token, String path, String label) {
        byte[] bytes = path != null ? path.getBytes(StandardCharsets.UTF_8) : null;
        byte[] shown = label != null ? label.getBytes(StandardCharsets.UTF_8) : null;
        run(() -> {
            if (handle != 0) nativePickResult(handle, token, bytes, shown);
        });
    }

    // Work arriving after close (a late picker result, a queued page call) has nobody to answer.
    private void run(Runnable task) {
        try {
            worker.execute(task);
        } catch (RejectedExecutionException closed) {
            Log.w(TAG, "launcher bridge closed; dropping a late call");
        }
    }

    private String failure(int id) {
        String message = failure.replace("\\", "\\\\").replace("\"", "\\\"");
        return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"error\":{\"code\":-32002,\"message\":\""
                + message + "\",\"next_step\":\"重新打开 OGPlay。\"}}";
    }

    void close() {
        worker.execute(() -> {
            if (handle != 0) nativeDestroy(handle);
            handle = 0;
        });
        worker.shutdown();
    }

    // Called from native code on the worker thread.
    @SuppressWarnings("unused")
    private void launchGame(String[] arguments) {
        host.launchGame(arguments);
    }

    @SuppressWarnings("unused")
    private void resumeGame() {
        host.resumeGame();
    }

    @SuppressWarnings("unused")
    private void stopGame() {
        host.stopGame();
    }

    @SuppressWarnings("unused")
    private void openUrl(String url) {
        host.openUrl(url);
    }

    @SuppressWarnings("unused")
    private void pickPath(long token, boolean directory) {
        try {
            host.pickPath(token, directory);
        } catch (RuntimeException error) {
            Log.e(TAG, "picker unavailable", error);
            pickResult(token, null, null);
        }
    }

    // Called from native import threads (any thread): a picked directory's listing and files.
    @SuppressWarnings("unused")
    private byte[] listTree(byte[] handle) {
        return host.listTree(new String(handle, StandardCharsets.UTF_8));
    }

    @SuppressWarnings("unused")
    private int openTreeFile(byte[] handle, byte[] path) {
        return host.openTreeFile(new String(handle, StandardCharsets.UTF_8), new String(path, StandardCharsets.UTF_8));
    }

    private static native long nativeCreate(LauncherBridge bridge);

    private static native String nativeHandle(long handle, byte[] request);

    private static native String nativePoll(long handle);

    private static native void nativePickResult(long handle, long token, byte[] path, byte[] label);

    private static native void nativeDestroy(long handle);
}

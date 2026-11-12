package org.ogplay.webview;

import android.os.Bundle;

import java.util.Map;

interface WebEngine {
    // The implementation delivers events on the owner Looper.
    interface Events {
        void pageStarted(String url);
        void progressChanged(int progress);
        void titleChanged(String title);
        void receivedError(int code, String description, String url);
        void pageFinished(String url);
    }

    interface ScriptResult {
        void complete(String value);
    }

    void load(String url, Map<String, String> headers, byte[] postData);
    void loadData(String baseUrl, String data, String mimeType, String encoding, String historyUrl);
    void executeJavaScript(String script, ScriptResult result);
    void setNetworkAvailable(boolean available);
    void stop();
    void reload();
    boolean canNavigate(int steps);
    void navigate(int steps);
    void clearHistory();
    WebHistory snapshot();
    boolean saveState(Bundle bundle);
    boolean restoreState(Bundle bundle);
    void destroy();
}

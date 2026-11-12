/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Compile-only API 19 ROM declarations. Excluded from ogplay.jar.
package android.webkit;

import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Picture;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import android.net.http.SslCertificate;
import android.os.Bundle;
import android.os.Message;
import android.print.PrintDocumentAdapter;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup.LayoutParams;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.webkit.WebView.HitTestResult;
import android.webkit.WebView.PictureListener;

import java.io.BufferedWriter;
import java.io.File;
import java.util.Map;

public interface WebViewProvider {

    public void init(Map<String, Object> javaScriptInterfaces,
            boolean privateBrowsing);

    public void setHorizontalScrollbarOverlay(boolean overlay);

    public void setVerticalScrollbarOverlay(boolean overlay);

    public boolean overlayHorizontalScrollbar();

    public boolean overlayVerticalScrollbar();

    public int getVisibleTitleHeight();

    public SslCertificate getCertificate();

    public void setCertificate(SslCertificate certificate);

    public void savePassword(String host, String username, String password);

    public void setHttpAuthUsernamePassword(String host, String realm,
            String username, String password);

    public String[] getHttpAuthUsernamePassword(String host, String realm);

    public void destroy();

    public void setNetworkAvailable(boolean networkUp);

    public WebBackForwardList saveState(Bundle outState);

    public boolean savePicture(Bundle b, final File dest);

    public boolean restorePicture(Bundle b, File src);

    public WebBackForwardList restoreState(Bundle inState);

    public void loadUrl(String url, Map<String, String> additionalHttpHeaders);

    public void loadUrl(String url);

    public void postUrl(String url, byte[] postData);

    public void loadData(String data, String mimeType, String encoding);

    public void loadDataWithBaseURL(String baseUrl, String data,
            String mimeType, String encoding, String historyUrl);

    public void evaluateJavaScript(String script, ValueCallback<String> resultCallback);

    public void saveWebArchive(String filename);

    public void saveWebArchive(String basename, boolean autoname, ValueCallback<String> callback);

    public void stopLoading();

    public void reload();

    public boolean canGoBack();

    public void goBack();

    public boolean canGoForward();

    public void goForward();

    public boolean canGoBackOrForward(int steps);

    public void goBackOrForward(int steps);

    public boolean isPrivateBrowsingEnabled();

    public boolean pageUp(boolean top);

    public boolean pageDown(boolean bottom);

    public void clearView();

    public Picture capturePicture();

    public PrintDocumentAdapter createPrintDocumentAdapter();

    public float getScale();

    public void setInitialScale(int scaleInPercent);

    public void invokeZoomPicker();

    public HitTestResult getHitTestResult();

    public void requestFocusNodeHref(Message hrefMsg);

    public void requestImageRef(Message msg);

    public String getUrl();

    public String getOriginalUrl();

    public String getTitle();

    public Bitmap getFavicon();

    public String getTouchIconUrl();

    public int getProgress();

    public int getContentHeight();

    public int getContentWidth();

    public void pauseTimers();

    public void resumeTimers();

    public void onPause();

    public void onResume();

    public boolean isPaused();

    public void freeMemory();

    public void clearCache(boolean includeDiskFiles);

    public void clearFormData();

    public void clearHistory();

    public void clearSslPreferences();

    public WebBackForwardList copyBackForwardList();

    public void setFindListener(WebView.FindListener listener);

    public void findNext(boolean forward);

    public int findAll(String find);

    public void findAllAsync(String find);

    public boolean showFindDialog(String text, boolean showIme);

    public void clearMatches();

    public void documentHasImages(Message response);

    public void setWebViewClient(WebViewClient client);

    public void setDownloadListener(DownloadListener listener);

    public void setWebChromeClient(WebChromeClient client);

    public void setPictureListener(PictureListener listener);

    public void addJavascriptInterface(Object obj, String interfaceName);

    public void removeJavascriptInterface(String interfaceName);

    public WebSettings getSettings();

    public void setMapTrackballToArrowKeys(boolean setMap);

    public void flingScroll(int vx, int vy);

    public View getZoomControls();

    public boolean canZoomIn();

    public boolean canZoomOut();

    public boolean zoomIn();

    public boolean zoomOut();

    public void dumpViewHierarchyWithProperties(BufferedWriter out, int level);

    public View findHierarchyView(String className, int hashCode);

     ViewDelegate getViewDelegate();

     ScrollDelegate getScrollDelegate();

    public void notifyFindDialogDismissed();

    interface ViewDelegate {
        public boolean shouldDelayChildPressedState();

        public AccessibilityNodeProvider getAccessibilityNodeProvider();

        public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info);

        public void onInitializeAccessibilityEvent(AccessibilityEvent event);

        public boolean performAccessibilityAction(int action, Bundle arguments);

        public void setOverScrollMode(int mode);

        public void setScrollBarStyle(int style);

        public void onDrawVerticalScrollBar(Canvas canvas, Drawable scrollBar, int l, int t,
                int r, int b);

        public void onOverScrolled(int scrollX, int scrollY, boolean clampedX, boolean clampedY);

        public void onWindowVisibilityChanged(int visibility);

        public void onDraw(Canvas canvas);

        public void setLayoutParams(LayoutParams layoutParams);

        public boolean performLongClick();

        public void onConfigurationChanged(Configuration newConfig);

        public InputConnection onCreateInputConnection(EditorInfo outAttrs);

        public boolean onKeyMultiple(int keyCode, int repeatCount, KeyEvent event);

        public boolean onKeyDown(int keyCode, KeyEvent event);

        public boolean onKeyUp(int keyCode, KeyEvent event);

        public void onAttachedToWindow();

        public void onDetachedFromWindow();

        public void onVisibilityChanged(View changedView, int visibility);

        public void onWindowFocusChanged(boolean hasWindowFocus);

        public void onFocusChanged(boolean focused, int direction, Rect previouslyFocusedRect);

        public boolean setFrame(int left, int top, int right, int bottom);

        public void onSizeChanged(int w, int h, int ow, int oh);

        public void onScrollChanged(int l, int t, int oldl, int oldt);

        public boolean dispatchKeyEvent(KeyEvent event);

        public boolean onTouchEvent(MotionEvent ev);

        public boolean onHoverEvent(MotionEvent event);

        public boolean onGenericMotionEvent(MotionEvent event);

        public boolean onTrackballEvent(MotionEvent ev);

        public boolean requestFocus(int direction, Rect previouslyFocusedRect);

        public void onMeasure(int widthMeasureSpec, int heightMeasureSpec);

        public boolean requestChildRectangleOnScreen(View child, Rect rect, boolean immediate);

        public void setBackgroundColor(int color);

        public void setLayerType(int layerType, Paint paint);

        public void preDispatchDraw(Canvas canvas);
    }

    interface ScrollDelegate {

        public int computeHorizontalScrollRange();

        public int computeHorizontalScrollOffset();

        public int computeVerticalScrollRange();

        public int computeVerticalScrollOffset();

        public int computeVerticalScrollExtent();

        public void computeScroll();
    }
}

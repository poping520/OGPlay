// Compile-only API 19 ROM declarations. Excluded from ogplay.jar.
package android.webkit;
import android.content.Context;

public abstract class WebSettings {
    protected WebSettings() {}
    public native void setNavDump(boolean enabled);
    public native boolean getNavDump();
    public native void setSupportZoom(boolean support);
    public native boolean supportZoom();
    public native void setMediaPlaybackRequiresUserGesture(boolean require);
    public native boolean getMediaPlaybackRequiresUserGesture();
    public native void setBuiltInZoomControls(boolean enabled);
    public native boolean getBuiltInZoomControls();
    public native void setDisplayZoomControls(boolean enabled);
    public native boolean getDisplayZoomControls();
    public native void setAllowFileAccess(boolean allow);
    public native boolean getAllowFileAccess();
    public native void setAllowContentAccess(boolean allow);
    public native boolean getAllowContentAccess();
    public native void setLoadWithOverviewMode(boolean overview);
    public native boolean getLoadWithOverviewMode();
    public native void setEnableSmoothTransition(boolean enable);
    public native boolean enableSmoothTransition();
    public native void setUseWebViewBackgroundForOverscrollBackground(boolean view);
    public native boolean getUseWebViewBackgroundForOverscrollBackground();
    public native void setSaveFormData(boolean save);
    public native boolean getSaveFormData();
    public native void setSavePassword(boolean save);
    public native boolean getSavePassword();
    public native synchronized void setTextZoom(int textZoom);
    public native synchronized int getTextZoom();
    public native synchronized void setTextSize(TextSize t);
    public native synchronized TextSize getTextSize();
    public native void setDefaultZoom(ZoomDensity zoom);
    public native ZoomDensity getDefaultZoom();
    public native void setLightTouchEnabled(boolean enabled);
    public native boolean getLightTouchEnabled();
    public native synchronized void setUseDoubleTree(boolean use);
    public native synchronized boolean getUseDoubleTree();
    public native synchronized void setUserAgent(int ua);
    public native synchronized int getUserAgent();
    public native synchronized void setUseWideViewPort(boolean use);
    public native synchronized boolean getUseWideViewPort();
    public native synchronized void setSupportMultipleWindows(boolean support);
    public native synchronized boolean supportMultipleWindows();
    public native synchronized void setLayoutAlgorithm(LayoutAlgorithm l);
    public native synchronized LayoutAlgorithm getLayoutAlgorithm();
    public native synchronized void setStandardFontFamily(String font);
    public native synchronized String getStandardFontFamily();
    public native synchronized void setFixedFontFamily(String font);
    public native synchronized String getFixedFontFamily();
    public native synchronized void setSansSerifFontFamily(String font);
    public native synchronized String getSansSerifFontFamily();
    public native synchronized void setSerifFontFamily(String font);
    public native synchronized String getSerifFontFamily();
    public native synchronized void setCursiveFontFamily(String font);
    public native synchronized String getCursiveFontFamily();
    public native synchronized void setFantasyFontFamily(String font);
    public native synchronized String getFantasyFontFamily();
    public native synchronized void setMinimumFontSize(int size);
    public native synchronized int getMinimumFontSize();
    public native synchronized void setMinimumLogicalFontSize(int size);
    public native synchronized int getMinimumLogicalFontSize();
    public native synchronized void setDefaultFontSize(int size);
    public native synchronized int getDefaultFontSize();
    public native synchronized void setDefaultFixedFontSize(int size);
    public native synchronized int getDefaultFixedFontSize();
    public native synchronized void setLoadsImagesAutomatically(boolean flag);
    public native synchronized boolean getLoadsImagesAutomatically();
    public native synchronized void setBlockNetworkImage(boolean flag);
    public native synchronized boolean getBlockNetworkImage();
    public native synchronized void setBlockNetworkLoads(boolean flag);
    public native synchronized boolean getBlockNetworkLoads();
    public native synchronized void setJavaScriptEnabled(boolean flag);
    public abstract void setAllowUniversalAccessFromFileURLs(boolean flag);
    public abstract void setAllowFileAccessFromFileURLs(boolean flag);
    public native synchronized void setPluginsEnabled(boolean flag);
    public native synchronized void setPluginState(PluginState state);
    public native synchronized void setPluginsPath(String pluginsPath);
    public native synchronized void setDatabasePath(String databasePath);
    public native synchronized void setGeolocationDatabasePath(String databasePath);
    public native synchronized void setAppCacheEnabled(boolean flag);
    public native synchronized void setAppCachePath(String appCachePath);
    public native synchronized void setAppCacheMaxSize(long appCacheMaxSize);
    public native synchronized void setDatabaseEnabled(boolean flag);
    public native synchronized void setDomStorageEnabled(boolean flag);
    public native synchronized boolean getDomStorageEnabled();
    public native synchronized String getDatabasePath();
    public native synchronized boolean getDatabaseEnabled();
    public native synchronized void setGeolocationEnabled(boolean flag);
    public native synchronized boolean getJavaScriptEnabled();
    public abstract boolean getAllowUniversalAccessFromFileURLs();
    public abstract boolean getAllowFileAccessFromFileURLs();
    public native synchronized boolean getPluginsEnabled();
    public native synchronized PluginState getPluginState();
    public native synchronized String getPluginsPath();
    public native synchronized void setJavaScriptCanOpenWindowsAutomatically(boolean flag);
    public native synchronized boolean getJavaScriptCanOpenWindowsAutomatically();
    public native synchronized void setDefaultTextEncodingName(String encoding);
    public native synchronized String getDefaultTextEncodingName();
    public native synchronized void setUserAgentString(String ua);
    public native synchronized String getUserAgentString();
    public native static String getDefaultUserAgent(Context context);
    public native void setNeedInitialFocus(boolean flag);
    public native synchronized void setRenderPriority(RenderPriority priority);
    public native void setCacheMode(int mode);
    public native int getCacheMode();

    public static final int LOAD_DEFAULT = -1;
    public enum LayoutAlgorithm { NORMAL, SINGLE_COLUMN, NARROW_COLUMNS, TEXT_AUTOSIZING }
    public enum ZoomDensity { FAR, MEDIUM, CLOSE }
    public enum PluginState { ON, ON_DEMAND, OFF }
    public enum TextSize { SMALLEST, SMALLER, NORMAL, LARGER, LARGEST }
    public enum RenderPriority { NORMAL, HIGH, LOW }
}

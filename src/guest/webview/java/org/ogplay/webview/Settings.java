package org.ogplay.webview;

import android.content.Context;
import android.content.pm.PackageManager;
import android.webkit.WebSettings;

// Defaults follow API 19 AwSettings, including the target SDK and INTERNET checks.
final class Settings extends WebSettings {
    private static final String INTERNET = "android.permission.INTERNET";
    static final String DEFAULT_USER_AGENT = "Mozilla/5.0 (Linux; Android 4.4.4; Mobile) "
            + "AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/30.0.0.0 Mobile Safari/537.36";
    private static final String DESKTOP_USER_AGENT = "Mozilla/5.0 (X11; Linux x86_64) "
            + "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Safari/537.36";

    private final boolean hasInternetPermission;
    private String userAgentString = DEFAULT_USER_AGENT;
    private int userAgent;

    private boolean navDump;
    private boolean supportZoom = true;
    private boolean mediaPlaybackRequiresUserGesture = true;
    private boolean builtInZoomControls;
    private boolean displayZoomControls = true;
    private boolean allowFileAccess = true;
    private boolean allowContentAccess = true;
    private boolean loadWithOverviewMode;
    private boolean enableSmoothTransition;
    private boolean useWebViewBackgroundForOverscrollBackground;
    private boolean saveFormData = true;
    private boolean savePassword = true;
    private int textZoom = 100;
    private ZoomDensity defaultZoom = ZoomDensity.MEDIUM;
    private boolean lightTouchEnabled;
    private boolean useDoubleTree;
    private boolean useWideViewPort;
    private boolean supportMultipleWindows;
    private LayoutAlgorithm layoutAlgorithm = LayoutAlgorithm.NARROW_COLUMNS;
    private String standardFontFamily = "sans-serif";
    private String fixedFontFamily = "monospace";
    private String sansSerifFontFamily = "sans-serif";
    private String serifFontFamily = "serif";
    private String cursiveFontFamily = "cursive";
    private String fantasyFontFamily = "fantasy";
    private int minimumFontSize = 8;
    private int minimumLogicalFontSize = 8;
    private int defaultFontSize = 16;
    private int defaultFixedFontSize = 13;
    private boolean loadsImagesAutomatically = true;
    private boolean blockNetworkImage;
    private boolean blockNetworkLoads;
    private boolean javaScriptEnabled;
    private boolean allowUniversalAccessFromFileURLs;
    private boolean allowFileAccessFromFileURLs;
    private PluginState pluginState = PluginState.OFF;
    private String pluginsPath = "";
    private String databasePath;
    private String geolocationDatabasePath;
    private boolean appCacheEnabled;
    private String appCachePath;
    private long appCacheMaxSize;
    private boolean databaseEnabled;
    private boolean domStorageEnabled;
    private boolean geolocationEnabled = true;
    private boolean javaScriptCanOpenWindowsAutomatically;
    private String defaultTextEncodingName = "Latin-1";
    private boolean needInitialFocus = true;
    private RenderPriority renderPriority = RenderPriority.NORMAL;
    private int cacheMode = LOAD_DEFAULT;

    Settings(Context context) {
        hasInternetPermission = context.checkCallingOrSelfPermission(INTERNET)
                == PackageManager.PERMISSION_GRANTED;
        blockNetworkLoads = !hasInternetPermission;
        allowUniversalAccessFromFileURLs = context.getApplicationInfo().targetSdkVersion < 16;
        allowFileAccessFromFileURLs = allowUniversalAccessFromFileURLs;
    }

    @Override public synchronized void setNavDump(boolean value) { navDump = value; }
    @Override public synchronized boolean getNavDump() { return navDump; }

    @Override public synchronized void setSupportZoom(boolean value) { supportZoom = value; }
    @Override public synchronized boolean supportZoom() { return supportZoom; }

    @Override public synchronized void setMediaPlaybackRequiresUserGesture(boolean value) { mediaPlaybackRequiresUserGesture = value; }
    @Override public synchronized boolean getMediaPlaybackRequiresUserGesture() { return mediaPlaybackRequiresUserGesture; }

    @Override public synchronized void setBuiltInZoomControls(boolean value) { builtInZoomControls = value; }
    @Override public synchronized boolean getBuiltInZoomControls() { return builtInZoomControls; }

    @Override public synchronized void setDisplayZoomControls(boolean value) { displayZoomControls = value; }
    @Override public synchronized boolean getDisplayZoomControls() { return displayZoomControls; }

    @Override public synchronized void setAllowFileAccess(boolean value) { allowFileAccess = value; }
    @Override public synchronized boolean getAllowFileAccess() { return allowFileAccess; }

    @Override public synchronized void setAllowContentAccess(boolean value) { allowContentAccess = value; }
    @Override public synchronized boolean getAllowContentAccess() { return allowContentAccess; }

    @Override public synchronized void setLoadWithOverviewMode(boolean value) { loadWithOverviewMode = value; }
    @Override public synchronized boolean getLoadWithOverviewMode() { return loadWithOverviewMode; }

    @Override public synchronized void setEnableSmoothTransition(boolean value) { enableSmoothTransition = value; }
    @Override public synchronized boolean enableSmoothTransition() { return enableSmoothTransition; }

    @Override public synchronized void setUseWebViewBackgroundForOverscrollBackground(boolean value) { useWebViewBackgroundForOverscrollBackground = value; }
    @Override public synchronized boolean getUseWebViewBackgroundForOverscrollBackground() { return useWebViewBackgroundForOverscrollBackground; }

    @Override public synchronized void setSaveFormData(boolean value) { saveFormData = value; }
    @Override public synchronized boolean getSaveFormData() { return saveFormData; }

    @Override public synchronized void setSavePassword(boolean value) { savePassword = value; }
    @Override public synchronized boolean getSavePassword() { return savePassword; }

    @Override public synchronized void setTextZoom(int value) { textZoom = value; }
    @Override public synchronized int getTextZoom() { return textZoom; }

    @Override public synchronized void setDefaultZoom(ZoomDensity value) { defaultZoom = value; }
    @Override public synchronized ZoomDensity getDefaultZoom() { return defaultZoom; }

    @Override public synchronized void setLightTouchEnabled(boolean value) { lightTouchEnabled = value; }
    @Override public synchronized boolean getLightTouchEnabled() { return lightTouchEnabled; }

    @Override public synchronized void setUseDoubleTree(boolean value) { useDoubleTree = value; }
    @Override public synchronized boolean getUseDoubleTree() { return useDoubleTree; }

    @Override public synchronized void setUseWideViewPort(boolean value) { useWideViewPort = value; }
    @Override public synchronized boolean getUseWideViewPort() { return useWideViewPort; }

    @Override public synchronized void setSupportMultipleWindows(boolean value) { supportMultipleWindows = value; }
    @Override public synchronized boolean supportMultipleWindows() { return supportMultipleWindows; }

    @Override public synchronized void setLayoutAlgorithm(LayoutAlgorithm value) { layoutAlgorithm = value; }
    @Override public synchronized LayoutAlgorithm getLayoutAlgorithm() { return layoutAlgorithm; }

    @Override public synchronized void setStandardFontFamily(String value) { standardFontFamily = value; }
    @Override public synchronized String getStandardFontFamily() { return standardFontFamily; }

    @Override public synchronized void setFixedFontFamily(String value) { fixedFontFamily = value; }
    @Override public synchronized String getFixedFontFamily() { return fixedFontFamily; }

    @Override public synchronized void setSansSerifFontFamily(String value) { sansSerifFontFamily = value; }
    @Override public synchronized String getSansSerifFontFamily() { return sansSerifFontFamily; }

    @Override public synchronized void setSerifFontFamily(String value) { serifFontFamily = value; }
    @Override public synchronized String getSerifFontFamily() { return serifFontFamily; }

    @Override public synchronized void setCursiveFontFamily(String value) { cursiveFontFamily = value; }
    @Override public synchronized String getCursiveFontFamily() { return cursiveFontFamily; }

    @Override public synchronized void setFantasyFontFamily(String value) { fantasyFontFamily = value; }
    @Override public synchronized String getFantasyFontFamily() { return fantasyFontFamily; }

    @Override public synchronized void setMinimumFontSize(int value) { minimumFontSize = value; }
    @Override public synchronized int getMinimumFontSize() { return minimumFontSize; }

    @Override public synchronized void setMinimumLogicalFontSize(int value) { minimumLogicalFontSize = value; }
    @Override public synchronized int getMinimumLogicalFontSize() { return minimumLogicalFontSize; }

    @Override public synchronized void setDefaultFontSize(int value) { defaultFontSize = value; }
    @Override public synchronized int getDefaultFontSize() { return defaultFontSize; }

    @Override public synchronized void setDefaultFixedFontSize(int value) { defaultFixedFontSize = value; }
    @Override public synchronized int getDefaultFixedFontSize() { return defaultFixedFontSize; }

    @Override public synchronized void setLoadsImagesAutomatically(boolean value) { loadsImagesAutomatically = value; }
    @Override public synchronized boolean getLoadsImagesAutomatically() { return loadsImagesAutomatically; }

    @Override public synchronized void setBlockNetworkImage(boolean value) { blockNetworkImage = value; }
    @Override public synchronized boolean getBlockNetworkImage() { return blockNetworkImage; }

    @Override public synchronized void setBlockNetworkLoads(boolean value) {
        if (!value && !hasInternetPermission) throw new SecurityException("INTERNET permission required");
        blockNetworkLoads = value;
    }
    @Override public synchronized boolean getBlockNetworkLoads() { return blockNetworkLoads; }

    @Override public synchronized void setJavaScriptEnabled(boolean value) { javaScriptEnabled = value; }
    @Override public synchronized boolean getJavaScriptEnabled() { return javaScriptEnabled; }

    @Override public synchronized void setAllowUniversalAccessFromFileURLs(boolean value) { allowUniversalAccessFromFileURLs = value; }
    @Override public synchronized boolean getAllowUniversalAccessFromFileURLs() { return allowUniversalAccessFromFileURLs; }

    @Override public synchronized void setAllowFileAccessFromFileURLs(boolean value) { allowFileAccessFromFileURLs = value; }
    @Override public synchronized boolean getAllowFileAccessFromFileURLs() { return allowFileAccessFromFileURLs; }

    @Override public synchronized void setPluginState(PluginState value) { pluginState = value; }
    @Override public synchronized PluginState getPluginState() { return pluginState; }

    @Override public synchronized void setPluginsPath(String value) { pluginsPath = value; }
    @Override public synchronized String getPluginsPath() { return pluginsPath; }

    @Override public synchronized void setDatabasePath(String value) { databasePath = value; }
    @Override public synchronized String getDatabasePath() { return databasePath; }

    @Override public synchronized void setGeolocationDatabasePath(String value) { geolocationDatabasePath = value; }

    @Override public synchronized void setAppCacheEnabled(boolean value) { appCacheEnabled = value; }

    @Override public synchronized void setAppCachePath(String value) { appCachePath = value; }

    @Override public synchronized void setAppCacheMaxSize(long value) { appCacheMaxSize = value; }

    @Override public synchronized void setDatabaseEnabled(boolean value) { databaseEnabled = value; }
    @Override public synchronized boolean getDatabaseEnabled() { return databaseEnabled; }

    @Override public synchronized void setDomStorageEnabled(boolean value) { domStorageEnabled = value; }
    @Override public synchronized boolean getDomStorageEnabled() { return domStorageEnabled; }

    @Override public synchronized void setGeolocationEnabled(boolean value) { geolocationEnabled = value; }

    @Override public synchronized void setJavaScriptCanOpenWindowsAutomatically(boolean value) { javaScriptCanOpenWindowsAutomatically = value; }
    @Override public synchronized boolean getJavaScriptCanOpenWindowsAutomatically() { return javaScriptCanOpenWindowsAutomatically; }

    @Override public synchronized void setDefaultTextEncodingName(String value) { defaultTextEncodingName = value; }
    @Override public synchronized String getDefaultTextEncodingName() { return defaultTextEncodingName; }

    @Override public synchronized void setNeedInitialFocus(boolean value) { needInitialFocus = value; }

    @Override public synchronized void setRenderPriority(RenderPriority value) { renderPriority = value; }

    @Override public synchronized void setCacheMode(int value) { cacheMode = value; }
    @Override public synchronized int getCacheMode() { return cacheMode; }

    @Override public synchronized void setPluginsEnabled(boolean enabled) {
        pluginState = enabled ? PluginState.ON : PluginState.OFF;
    }

    @Override public synchronized boolean getPluginsEnabled() { return pluginState == PluginState.ON; }

    @Override public synchronized void setUserAgent(int value) {
        if (value == 0 || value == 1) {
            userAgent = value;
            userAgentString = value == 0 ? DEFAULT_USER_AGENT : DESKTOP_USER_AGENT;
        }
    }

    @Override public synchronized int getUserAgent() { return userAgent; }

    @Override public synchronized void setUserAgentString(String value) {
        userAgentString = value == null || value.isEmpty() ? DEFAULT_USER_AGENT : value;
        userAgent = userAgentString.equals(DEFAULT_USER_AGENT) ? 0
                : userAgentString.equals(DESKTOP_USER_AGENT) ? 1 : -1;
    }

    @Override public synchronized String getUserAgentString() { return userAgentString; }
}

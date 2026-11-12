package com.android.webview.chromium;

import android.content.Context;
import android.webkit.CookieManager;
import android.webkit.GeolocationPermissions;
import android.webkit.WebIconDatabase;
import android.webkit.WebStorage;
import android.webkit.WebView;
import android.webkit.WebViewDatabase;
import android.webkit.WebViewFactoryProvider;
import android.webkit.WebViewProvider;

import org.ogplay.webview.WebServices;

/** API 19 WebViewFactory's fixed provider entry point. */
public final class WebViewChromiumFactoryProvider implements WebViewFactoryProvider {
    private final WebServices delegate = new WebServices();

    @Override public Statics getStatics() { return delegate.getStatics(); }
    @Override public WebViewProvider createWebView(WebView view, WebView.PrivateAccess access) {
        return delegate.createWebView(view, access);
    }
    @Override public CookieManager getCookieManager() { return delegate.getCookieManager(); }
    @Override public GeolocationPermissions getGeolocationPermissions() { return delegate.getGeolocationPermissions(); }
    @Override public WebStorage getWebStorage() { return delegate.getWebStorage(); }
    @Override public WebViewDatabase getWebViewDatabase(Context context) { return delegate.getWebViewDatabase(context); }
    @Override public WebIconDatabase getWebIconDatabase() { return delegate.getWebIconDatabase(); }
}

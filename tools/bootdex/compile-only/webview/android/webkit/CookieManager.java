// Compile-only API 19 ROM declarations. Excluded from ogplay.jar.
package android.webkit;
import android.net.WebAddress;

public class CookieManager {
    protected CookieManager() {}
    public native static synchronized CookieManager getInstance();
    public native synchronized void setAcceptCookie(boolean accept);
    public native synchronized boolean acceptCookie();
    public native void setCookie(String url, String value);
    public native String getCookie(String url);
    public native String getCookie(String url, boolean privateBrowsing);
    public native synchronized String getCookie(WebAddress uri);
    public native void removeSessionCookie();
    public native void removeAllCookie();
    public native synchronized boolean hasCookies();
    public native synchronized boolean hasCookies(boolean privateBrowsing);
    public native void removeExpiredCookie();
    protected native void flushCookieStore();
    public native static boolean allowFileSchemeCookies();
    protected native boolean allowFileSchemeCookiesImpl();
    public native static void setAcceptFileSchemeCookies(boolean accept);
    protected native void setAcceptFileSchemeCookiesImpl(boolean accept);

}

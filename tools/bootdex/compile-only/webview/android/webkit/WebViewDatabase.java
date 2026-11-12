// Compile-only API 19 ROM declarations. Excluded from ogplay.jar.
package android.webkit;
import android.content.Context;

public class WebViewDatabase {
    protected WebViewDatabase() {}
    public native static WebViewDatabase getInstance(Context context);
    public native boolean hasUsernamePassword();
    public native void clearUsernamePassword();
    public native boolean hasHttpAuthUsernamePassword();
    public native void clearHttpAuthUsernamePassword();
    public native boolean hasFormData();
    public native void clearFormData();

}

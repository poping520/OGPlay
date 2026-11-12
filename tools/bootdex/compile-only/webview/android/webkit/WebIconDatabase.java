// Compile-only API 19 ROM declarations. Excluded from ogplay.jar.
package android.webkit;
import android.content.ContentResolver;
import android.graphics.Bitmap;

public class WebIconDatabase {
    public native void open(String path);
    public native void close();
    public native void removeAllIcons();
    public native void requestIconForPageUrl(String url, IconListener listener);
    public native void bulkRequestIconForPageUrl(ContentResolver cr, String where, IconListener listener);
    public native void retainIconForPageUrl(String url);
    public native void releaseIconForPageUrl(String url);
    public native static WebIconDatabase getInstance();
    protected WebIconDatabase() {}

    public interface IconListener { void onReceivedIcon(String url, Bitmap icon); }
}

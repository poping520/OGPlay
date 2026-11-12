// Compile-only API 19 ROM declarations. Excluded from ogplay.jar.
package android.webkit;
import android.graphics.Bitmap;

public class WebHistoryItem {
    public WebHistoryItem() {}
    public native int getId();
    public native String getUrl();
    public native String getOriginalUrl();
    public native String getTitle();
    public native Bitmap getFavicon();
    protected native synchronized WebHistoryItem clone();

}

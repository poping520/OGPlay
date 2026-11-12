// Compile-only API 19 ROM declarations. Excluded from ogplay.jar.
package android.webkit;
import java.io.Serializable;

public class WebBackForwardList {
    public WebBackForwardList() {}
    public native synchronized WebHistoryItem getCurrentItem();
    public native synchronized int getCurrentIndex();
    public native synchronized WebHistoryItem getItemAtIndex(int index);
    public native synchronized int getSize();
    protected native synchronized WebBackForwardList clone();

}

// Compile-only API 19 ROM declarations. Excluded from ogplay.jar.
package android.webkit;
import java.util.Map;

public class WebStorage {
    public native void getOrigins(ValueCallback<Map> callback);
    public native void getUsageForOrigin(String origin, ValueCallback<Long> callback);
    public native void getQuotaForOrigin(String origin, ValueCallback<Long> callback);
    public native void setQuotaForOrigin(String origin, long quota);
    public native void deleteOrigin(String origin);
    public native void deleteAllData();
    public native static WebStorage getInstance();
    public WebStorage() {}

    public static class Origin {
        protected Origin(String origin, long quota, long usage) {}
    }
}

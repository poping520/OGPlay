// Compile-only API 19 ROM declaration. Excluded from ogplay.jar.
package android.webkit;

import java.util.Set;

public class GeolocationPermissions {
    public GeolocationPermissions() {}
    public native void getOrigins(ValueCallback<Set<String>> callback);
    public native void getAllowed(String origin, ValueCallback<Boolean> callback);
    public native void allow(String origin);
    public native void clear(String origin);
    public native void clearAll();
}

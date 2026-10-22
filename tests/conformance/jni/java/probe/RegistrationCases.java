/* SPDX-License-Identifier: Apache-2.0
 * Standalone Dalvik JNI probes. Oracles cite vm/Jni.cpp (Android 4.4).
 */
package probe;
import static probe.NativeSupport.*;
final class RegistrationCases implements CaseGroup {
    public static class ExportTarget { public static native int value(); }
    public static class DynamicOnly { public static native int value(); }
    private static native String registerTarget(Class target);
    private static native String unregister(Class target);

    public String[] ids() {
        return new String[] {"registration.UnregisterNatives.export",
                             "registration.UnregisterNatives.dynamicOnly"};
    }
    public void run(String id) throws Throwable {
        // Jni.cpp:2507-2516
        if (id.equals("registration.UnregisterNatives.export")) {
            check(registerTarget(ExportTarget.class));
            require(ExportTarget.value() == 29);
            check(unregister(ExportTarget.class));
            require(ExportTarget.value() == 17);
            return;
        }
        // Jni.cpp:2507-2516
        if (id.equals("registration.UnregisterNatives.dynamicOnly")) {
            check(registerTarget(DynamicOnly.class));
            require(DynamicOnly.value() == 29);
            check(unregister(DynamicOnly.class));
            try {
                DynamicOnly.value();
                throw new AssertionError("still registered");
            } catch (UnsatisfiedLinkError expected) {
            };
            return;
        }
        throw new IllegalArgumentException(id);
    }
}

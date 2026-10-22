/* SPDX-License-Identifier: Apache-2.0
 * Standalone Dalvik JNI probes. Oracles cite vm/Jni.cpp (Android 4.4).
 */
package probe;
import static probe.NativeSupport.*;
final class ReferencesCases implements CaseGroup {
    private static native String probe(int mode, Object obj);
    public String[] ids() {
        return new String[] {"reference.NewGlobalRef",
                             "reference.DeleteGlobalRef",
                             "reference.NewLocalRef",
                             "reference.DeleteLocalRef",
                             "reference.NewWeakGlobalRef",
                             "reference.DeleteWeakGlobalRef",
                             "reference.GetObjectRefType",
                             "reference.EnsureLocalCapacity",
                             "reference.EnsureLocalCapacity.dvm_limit",
                             "reference.PushLocalFrame",
                             "reference.PopLocalFrame",
                             "reference.PushLocalFrame.dvm_limit",
                             "reference.GetObjectRefType.dvm_popped"};
    }
    public void run(String id) throws Throwable {
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.NewGlobalRef")) {
            check(probe(0, new Object()));
            return;
        }
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.DeleteGlobalRef")) {
            check(probe(1, new Object()));
            return;
        }
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.NewLocalRef")) {
            check(probe(2, new Object()));
            return;
        }
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.DeleteLocalRef")) {
            check(probe(3, new Object()));
            return;
        }
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.NewWeakGlobalRef")) {
            check(probe(4, new Object()));
            return;
        }
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.DeleteWeakGlobalRef")) {
            check(probe(5, new Object()));
            return;
        }
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.GetObjectRefType")) {
            check(probe(6, new Object()));
            return;
        }
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.EnsureLocalCapacity")) {
            check(probe(7, new Object()));
            return;
        }
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.EnsureLocalCapacity.dvm_limit")) {
            check(probe(8, new Object()));
            return;
        }
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.PushLocalFrame")) {
            check(probe(9, new Object()));
            return;
        }
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.PopLocalFrame")) {
            check(probe(10, new Object()));
            return;
        }
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.PushLocalFrame.dvm_limit")) {
            check(probe(11, new Object()));
            return;
        }
        // Jni.cpp:409-412;1418-1491;2651-2685
        if (id.equals("reference.GetObjectRefType.dvm_popped")) {
            check(probe(12, new Object()));
            return;
        }
        throw new IllegalArgumentException(id);
    }
}

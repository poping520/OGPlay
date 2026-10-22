/* SPDX-License-Identifier: Apache-2.0
 * Standalone Dalvik JNI probes. Oracles cite vm/Jni.cpp (Android 4.4).
 */
package probe;
import static probe.NativeSupport.*;
final class ExceptionsCases implements CaseGroup {
    int calls;
    static int describeCalls;
    static final class Described extends IllegalStateException {
        // Jni.cpp:1383-1387 -> Exception.cpp:564-592 invokes the override and ignores its Java throw.
        public void printStackTrace() {
            describeCalls++;
            System.err.println("describe-marker override");
            throw new IllegalArgumentException("printing-secondary");
        }
    }
    public void callback() { calls++; }
    private static native void throwSame(Throwable original) throws Throwable;
    private static native String probe(int mode, Throwable original, Object receiver);
    private static native void fatal();

    public String[] ids() {
        return new String[] {"exception.Throw",
                             "exception.ExceptionOccurred",
                             "exception.ExceptionOccurred.localRefType",
                             "exception.ExceptionDescribe",
                             "exception.GetMethodID.dvm_pending",
                             "exception.NewStringUTF.dvm_pending",
                             "exception.CallVoidMethod.dvm_pending",
                             "exception.FatalError"};
    }
    public void run(String id) throws Throwable {
        // Jni.cpp:1333-1340
        if (id.equals("exception.Throw")) {
            Throwable t = new IllegalStateException("identity");
            try {
                throwSame(t);
                throw new AssertionError("Throw returned");
            } catch (Throwable got) {
                require(got == t);
            };
            return;
        }
        // Jni.cpp:1333-1387;1625-1668;1909-1973;2161-2170
        if (id.equals("exception.ExceptionOccurred")) {
            ExceptionsCases receiver = new ExceptionsCases();
            check(probe(0, new IllegalStateException("describe-marker"), receiver));
            return;
        }
        // Jni.cpp:1333-1387;1625-1668;1909-1973;2161-2170
        if (id.equals("exception.ExceptionDescribe")) {
            ExceptionsCases receiver = new ExceptionsCases();
            check(probe(1, new IllegalStateException("describe-marker"), receiver));
            describeCalls = 0;
            check(probe(1, new Described(), receiver));
            require(describeCalls == 1);
            return;
        }
        // Jni.cpp:1333-1387;1625-1668;1909-1973;2161-2170
        if (id.equals("exception.GetMethodID.dvm_pending")) {
            ExceptionsCases receiver = new ExceptionsCases();
            check(probe(2, new IllegalStateException("describe-marker"), receiver));
            return;
        }
        // Jni.cpp:1333-1387;1625-1668;1909-1973;2161-2170
        if (id.equals("exception.NewStringUTF.dvm_pending")) {
            ExceptionsCases receiver = new ExceptionsCases();
            check(probe(3, new IllegalStateException("describe-marker"), receiver));
            return;
        }
        // Jni.cpp:1333-1387;1625-1668;1909-1973;2161-2170
        if (id.equals("exception.CallVoidMethod.dvm_pending")) {
            ExceptionsCases receiver = new ExceptionsCases();
            check(probe(4, new IllegalStateException("describe-marker"), receiver));
            require(receiver.calls == 1);
            return;
        }
        // Jni.cpp:1406-1409
        if (id.equals("exception.FatalError")) {
            fatal();
            return;
        }
        // Jni.cpp:1363-1380 adds a local reference; 2683-2685 observes its type.
        if (id.equals("exception.ExceptionOccurred.localRefType")) {
            check(probe(5, new IllegalStateException("local-type"), new ExceptionsCases()));
            return;
        }
        throw new IllegalArgumentException(id);
    }
}

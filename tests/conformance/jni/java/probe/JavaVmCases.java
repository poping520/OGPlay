/* SPDX-License-Identifier: Apache-2.0 */
package probe;
import static probe.NativeSupport.*;
final class JavaVmCases implements CaseGroup {
    private static native String probe(int mode);
    public static boolean appMarker() { return true; }
    public String[] ids() {
        return new String[] {"javavm.GetEnv.detached",
                             "javavm.AttachCurrentThread",
                             "javavm.AttachCurrentThread.named",
                             "javavm.AttachCurrentThreadAsDaemon",
                             "javavm.AttachCurrentThread.idempotent",
                             "javavm.DetachCurrentThread",
                             "javavm.FindClass.attached",
                             "javavm.GetJavaVM",
                             "javavm.DetachCurrentThread.dvm_javaThread"};
    }
    public void run(String id) {
        // Jni.cpp:1225-1247;2555-2558;2780-2925; Java-thread detach delegates to
        // Thread.cpp:2013-2029.
        if (id.equals("javavm.GetEnv.detached")) {
            check(probe(0));
            return;
        }
        // Jni.cpp:1225-1247;2555-2558;2780-2925; Java-thread detach delegates to
        // Thread.cpp:2013-2029.
        if (id.equals("javavm.AttachCurrentThread")) {
            check(probe(1));
            return;
        }
        // Jni.cpp:1225-1247;2555-2558;2780-2925; Java-thread detach delegates to
        // Thread.cpp:2013-2029.
        if (id.equals("javavm.AttachCurrentThread.named")) {
            check(probe(2));
            return;
        }
        // Jni.cpp:1225-1247;2555-2558;2780-2925; Java-thread detach delegates to
        // Thread.cpp:2013-2029.
        if (id.equals("javavm.AttachCurrentThreadAsDaemon")) {
            check(probe(3));
            return;
        }
        // Jni.cpp:1225-1247;2555-2558;2780-2925; Java-thread detach delegates to
        // Thread.cpp:2013-2029.
        if (id.equals("javavm.AttachCurrentThread.idempotent")) {
            check(probe(4));
            return;
        }
        // Jni.cpp:1225-1247;2555-2558;2780-2925; Java-thread detach delegates to
        // Thread.cpp:2013-2029.
        if (id.equals("javavm.DetachCurrentThread")) {
            check(probe(5));
            return;
        }
        // Jni.cpp:1225-1247;2555-2558;2780-2925; Java-thread detach delegates to
        // Thread.cpp:2013-2029.
        if (id.equals("javavm.FindClass.attached")) {
            check(probe(6));
            return;
        }
        // Jni.cpp:1225-1247;2555-2558;2780-2925; Java-thread detach delegates to
        // Thread.cpp:2013-2029.
        if (id.equals("javavm.GetJavaVM")) {
            check(probe(7));
            return;
        }
        // Jni.cpp:1225-1247;2555-2558;2780-2925; Java-thread detach delegates to
        // Thread.cpp:2013-2029.
        if (id.equals("javavm.DetachCurrentThread.dvm_javaThread")) {
            check(probe(8));
            return;
        }
        throw new IllegalArgumentException(id);
    }
}

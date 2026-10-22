/* SPDX-License-Identifier: Apache-2.0
 * Standalone Dalvik JNI probes. Oracles cite vm/Jni.cpp (Android 4.4).
 */
package probe;
import static probe.NativeSupport.*;
final class MonitorNioCases implements CaseGroup {
    private static native String monitor(int mode, Object obj);
    private static native java.nio.ByteBuffer create();
    private static native String buffer(Object obj, int mode);

    public String[] ids() {
        return new String[] {"monitor.MonitorEnter",
                             "monitor.MonitorEnter.reentrant",
                             "monitor.MonitorExit",
                             "monitor.MonitorExit.nonOwner",
                             "nio.NewDirectByteBuffer",
                             "nio.GetDirectBufferAddress",
                             "nio.GetDirectBufferCapacity",
                             "nio.GetDirectBufferAddress.heap",
                             "nio.GetDirectBufferCapacity.dvm_heapCapacity"};
    }
    public void run(String id) throws Throwable {
        // Jni.cpp:2525-2549
        if (id.equals("monitor.MonitorEnter")) {
            check(monitor(0, new Object()));
            return;
        }
        // Jni.cpp:2525-2549
        if (id.equals("monitor.MonitorEnter.reentrant")) {
            check(monitor(1, new Object()));
            return;
        }
        // Jni.cpp:2525-2549
        if (id.equals("monitor.MonitorExit")) {
            check(monitor(0, new Object()));
            return;
        }
        // Jni.cpp:2525-2549
        if (id.equals("monitor.MonitorExit.nonOwner")) {
            check(monitor(2, new Object()));
            return;
        }
        // Jni.cpp:2691-2759
        if (id.equals("nio.NewDirectByteBuffer")) {
            java.nio.ByteBuffer b = create();
            require(b.isDirect() && b.capacity() == 16 && b.get(0) == 11);
            b.put(1, (byte)73);
            check(buffer(b, 0));
            require(b.get(2) == (byte)-55);
            return;
        }
        // Jni.cpp:2691-2759
        if (id.equals("nio.GetDirectBufferAddress")) {
            java.nio.ByteBuffer b = create();
            require(b.isDirect() && b.capacity() == 16 && b.get(0) == 11);
            b.put(1, (byte)73);
            check(buffer(b, 0));
            require(b.get(2) == (byte)-55);
            return;
        }
        // Jni.cpp:2691-2759
        if (id.equals("nio.GetDirectBufferCapacity")) {
            java.nio.ByteBuffer b = create();
            require(b.isDirect() && b.capacity() == 16 && b.get(0) == 11);
            b.put(1, (byte)73);
            check(buffer(b, 0));
            require(b.get(2) == (byte)-55);
            return;
        }
        // Jni.cpp:2733-2738
        if (id.equals("nio.GetDirectBufferAddress.heap")) {
            check(buffer(java.nio.ByteBuffer.allocate(7), 1));
            return;
        }
        // Jni.cpp:2742-2759
        if (id.equals("nio.GetDirectBufferCapacity.dvm_heapCapacity")) {
            check(buffer(java.nio.ByteBuffer.allocate(7), 2));
            return;
        }
        throw new IllegalArgumentException(id);
    }
}

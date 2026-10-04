package org.ogplay.zip;

/** VM GC/teardown boundary; the API19 stream classes retain their original ABI. */
public final class NativeZip {
    private NativeZip() {}
    private static native void release(long token);
}

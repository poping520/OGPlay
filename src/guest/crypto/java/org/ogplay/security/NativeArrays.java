package org.ogplay.security;

/** Array allocation for guest JNI, whose class registry does not resolve array classes. */
final class NativeArrays {
    private NativeArrays() {}

    static byte[][] newByteArrays(int count) {
        return new byte[count][];
    }
}

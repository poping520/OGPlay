package org.ogplay.math;

/** Value-only bridge: byte 0 is the sign (0/1), followed by unsigned big-endian magnitude. */
public final class NativeBigInteger {
    private NativeBigInteger() {}
    public static native byte[] add(byte[] a, byte[] b);
    public static native byte[] multiply(byte[] a, byte[] b);
    public static native byte[] power(byte[] a, byte[] exponent);
    /** Division returns: 4-byte big-endian quotient byte length, quotient value, remainder value. */
    public static native byte[] divide(byte[] a, byte[] b);
    public static native byte[] shift(byte[] a, int count);
    public static native byte[] multiplyWord(byte[] a, int unsignedWord);
}

/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * Sources: libjnitest/android_jni_cts_{Static,Instance}Nonce.c;
 * src/android/jni/cts/Jni{Static,Instance}Test.java. Additional ABI shapes
 * from ART test/MyClassNatives, d5e4ac0; synchronized dispatch verified at
 * dalvik/vm/Jni.cpp:1146.
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */

package probe;
public class BoundaryStatic {
    public static native void nop();
    public static native boolean returnBoolean();
    public static native boolean takeBoolean(boolean v);
    public static native byte returnByte();
    public static native boolean takeByte(byte v);
    public static native short returnShort();
    public static native boolean takeShort(short v);
    public static native char returnChar();
    public static native boolean takeChar(char v);
    public static native int returnInt();
    public static native boolean takeInt(int v);
    public static native long returnLong();
    public static native boolean takeLong(long v);
    public static native float returnFloat();
    public static native boolean takeFloat(float v);
    public static native double returnDouble();
    public static native boolean takeDouble(double v);
    public static native Object returnNull();
    public static native boolean takeNull(Object v);
    public static native String returnString();
    public static native boolean takeString(String v);
    public static native short[] returnShortArray();
    public static native String[] returnStringArray();
    public static native Class returnThisClass();
    public static native boolean takeThisClass(Class v);
    public static native BoundaryStatic returnInstance();
    public static native boolean takeIntLong(int a, long b);
    public static native boolean takeLongInt(long a, int b);
    public static native boolean takeOneOfEach(boolean a, byte b, short c, char d, int e, long f,
                                               String g, float h, double i, int[] j);
    public static native boolean takeCoolHandLuke(
        int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11,
        int a12, int a13, int a14, int a15, int a16, int a17, int a18, int a19, int a20, int a21,
        int a22, int a23, int a24, int a25, int a26, int a27, int a28, int a29, int a30, int a31,
        int a32, int a33, int a34, int a35, int a36, int a37, int a38, int a39, int a40, int a41,
        int a42, int a43, int a44, int a45, int a46, int a47, int a48, int a49, int a50);
    public static synchronized native boolean holdsLock();
    public static native boolean mixedAlignment(long a, Object b, long c, Object d);
    public static native double doublePair(double a, double b);
    public static native void throwException();
}
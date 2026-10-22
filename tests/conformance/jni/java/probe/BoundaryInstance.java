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
public class BoundaryInstance {
    public native void nop();
    public native boolean returnBoolean();
    public native boolean takeBoolean(boolean v);
    public native byte returnByte();
    public native boolean takeByte(byte v);
    public native short returnShort();
    public native boolean takeShort(short v);
    public native char returnChar();
    public native boolean takeChar(char v);
    public native int returnInt();
    public native boolean takeInt(int v);
    public native long returnLong();
    public native boolean takeLong(long v);
    public native float returnFloat();
    public native boolean takeFloat(float v);
    public native double returnDouble();
    public native boolean takeDouble(double v);
    public native Object returnNull();
    public native boolean takeNull(Object v);
    public native String returnString();
    public native boolean takeString(String v);
    public native short[] returnShortArray();
    public native String[] returnStringArray();
    public native Class returnThisClass();
    public native boolean takeThisClass(Class v);
    public native BoundaryInstance returnInstance();
    public native boolean takeIntLong(int a, long b);
    public native boolean takeLongInt(long a, int b);
    public native boolean takeOneOfEach(boolean a, byte b, short c, char d, int e, long f, String g,
                                        float h, double i, int[] j);
    public native boolean takeCoolHandLuke(
        int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11,
        int a12, int a13, int a14, int a15, int a16, int a17, int a18, int a19, int a20, int a21,
        int a22, int a23, int a24, int a25, int a26, int a27, int a28, int a29, int a30, int a31,
        int a32, int a33, int a34, int a35, int a36, int a37, int a38, int a39, int a40, int a41,
        int a42, int a43, int a44, int a45, int a46, int a47, int a48, int a49, int a50);
    public synchronized native boolean holdsLock();
    public native boolean mixedAlignment(long a, Object b, long c, Object d);
    public native double doublePair(double a, double b);
    public native void throwException();
}
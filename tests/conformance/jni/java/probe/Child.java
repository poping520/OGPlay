/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * Sources: libjnitest/macroized_tests.c and
 * src/android/jni/cts/{Static,Instance}FromNative.java.
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */

package probe;
public class Child extends Parent {
    public boolean valueBoolean() { return false; }
    public byte valueByte() { return 0; }
    public short valueShort() { return 0; }
    public char valueChar() { return 0; }
    public int valueInt() { return 0; }
    public long valueLong() { return 0; }
    public float valueFloat() { return 0; }
    public double valueDouble() { return 0; }
    public Object valueObject() { return null; }
    public void valueVoid() { CallCases.touched = -1; }

    @Override
    public boolean takeOneOfEach(double a, float b, long c, int d, char e, short f, byte g,
                                 boolean h, String i) {
        return false;
    }
    @Override
    public boolean takeCoolHandLuke(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                                    int a9, int a10, int a11, int a12, int a13, int a14, int a15,
                                    int a16, int a17, int a18, int a19, int a20, int a21, int a22,
                                    int a23, int a24, int a25, int a26, int a27, int a28, int a29,
                                    int a30, int a31, int a32, int a33, int a34, int a35, int a36,
                                    int a37, int a38, int a39, int a40, int a41, int a42, int a43,
                                    int a44, int a45, int a46, int a47, int a48, int a49, int a50) {
        return false;
    }
}

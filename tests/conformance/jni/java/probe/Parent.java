/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * Sources: libjnitest/macroized_tests.c and
 * src/android/jni/cts/{Static,Instance}FromNative.java.
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */

package probe;
public class Parent {
    public boolean valueBoolean() { return true; }
    public byte valueByte() { return (byte)14; }
    public short valueShort() { return (short)-608; }
    public char valueChar() { return (char)9000; }
    public int valueInt() { return 4004004; }
    public long valueLong() { return -80080080087L; }
    public float valueFloat() { return 2.5e22f; }
    public double valueDouble() { return 7.503e100; }
    public Object valueObject() { return "muffins"; }
    public void valueVoid() { CallCases.touched = 42; }
    public boolean takeOneOfEach(double a, float b, long c, int d, char e, short f, byte g,
                                 boolean h, String i) {
        return a == 0 && b == 1 && c == 2 && d == 3 && e == 4 && f == 5 && g == 6 && h &&
            "biscuits".equals(i);
    }
    public boolean takeCoolHandLuke(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                                    int a9, int a10, int a11, int a12, int a13, int a14, int a15,
                                    int a16, int a17, int a18, int a19, int a20, int a21, int a22,
                                    int a23, int a24, int a25, int a26, int a27, int a28, int a29,
                                    int a30, int a31, int a32, int a33, int a34, int a35, int a36,
                                    int a37, int a38, int a39, int a40, int a41, int a42, int a43,
                                    int a44, int a45, int a46, int a47, int a48, int a49, int a50) {
        return a1 == 1 && a2 == 2 && a3 == 3 && a4 == 4 && a5 == 5 && a6 == 6 && a7 == 7 &&
            a8 == 8 && a9 == 9 && a10 == 10 && a11 == 11 && a12 == 12 && a13 == 13 && a14 == 14 &&
            a15 == 15 && a16 == 16 && a17 == 17 && a18 == 18 && a19 == 19 && a20 == 20 &&
            a21 == 21 && a22 == 22 && a23 == 23 && a24 == 24 && a25 == 25 && a26 == 26 &&
            a27 == 27 && a28 == 28 && a29 == 29 && a30 == 30 && a31 == 31 && a32 == 32 &&
            a33 == 33 && a34 == 34 && a35 == 35 && a36 == 36 && a37 == 37 && a38 == 38 &&
            a39 == 39 && a40 == 40 && a41 == 41 && a42 == 42 && a43 == 43 && a44 == 44 &&
            a45 == 45 && a46 == 46 && a47 == 47 && a48 == 48 && a49 == 49 && a50 == 50;
    }
}
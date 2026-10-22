/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */
package probe;
public class Main {
    public static int touched;
    private static native String runNative(String id);
    private static native int loadCount();
    private static native int loadCount14();
    private static native int shortExport();
    private static native int overloaded(int v);
    private static native long overloaded(long v);
    private static void require(boolean b) {
        if (!b)
            throw new AssertionError("value mismatch");
    }
    private static void checkNative(String id) {
        String why = runNative(id);
        if (why != null)
            throw new AssertionError(why);
    }
    private static void run(String id) {
        if (id.equals("boundary_static.nop")) {
            BoundaryStatic.nop();
            return;
        }
        if (id.equals("boundary_static.returnBoolean")) {
            require(BoundaryStatic.returnBoolean() == true);
            return;
        }
        if (id.equals("boundary_static.takeBoolean")) {
            require(BoundaryStatic.takeBoolean(true));
            return;
        }
        if (id.equals("boundary_static.returnByte")) {
            require(BoundaryStatic.returnByte() == (byte)123);
            return;
        }
        if (id.equals("boundary_static.takeByte")) {
            require(BoundaryStatic.takeByte((byte)-99));
            return;
        }
        if (id.equals("boundary_static.returnShort")) {
            require(BoundaryStatic.returnShort() == (short)-12345);
            return;
        }
        if (id.equals("boundary_static.takeShort")) {
            require(BoundaryStatic.takeShort((short)19991));
            return;
        }
        if (id.equals("boundary_static.returnChar")) {
            require(BoundaryStatic.returnChar() == (char)34567);
            return;
        }
        if (id.equals("boundary_static.takeChar")) {
            require(BoundaryStatic.takeChar((char)999));
            return;
        }
        if (id.equals("boundary_static.returnInt")) {
            require(BoundaryStatic.returnInt() == 12345678);
            return;
        }
        if (id.equals("boundary_static.takeInt")) {
            require(BoundaryStatic.takeInt(-999888777));
            return;
        }
        if (id.equals("boundary_static.returnLong")) {
            require(BoundaryStatic.returnLong() == -1098765432109876543L);
            return;
        }
        if (id.equals("boundary_static.takeLong")) {
            require(BoundaryStatic.takeLong(999888777666555444L));
            return;
        }
        if (id.equals("boundary_static.returnFloat")) {
            require(BoundaryStatic.returnFloat() == -98765.4321F);
            return;
        }
        if (id.equals("boundary_static.takeFloat")) {
            require(BoundaryStatic.takeFloat(-9988.7766F));
            return;
        }
        if (id.equals("boundary_static.returnDouble")) {
            require(BoundaryStatic.returnDouble() == 12345678.9);
            return;
        }
        if (id.equals("boundary_static.takeDouble")) {
            require(BoundaryStatic.takeDouble(999888777.666555));
            return;
        }
        if (id.equals("boundary_static.returnNull")) {
            require(BoundaryStatic.returnNull() == null);
            return;
        }
        if (id.equals("boundary_static.takeNull")) {
            require(BoundaryStatic.takeNull(null));
            return;
        }
        if (id.equals("boundary_static.returnString")) {
            require("blort".equals(BoundaryStatic.returnString()));
            return;
        }
        if (id.equals("boundary_static.takeString")) {
            require(BoundaryStatic.takeString("fuzzbot"));
            return;
        }
        if (id.equals("boundary_static.returnShortArray")) {
            short[] a = BoundaryStatic.returnShortArray();
            require(a.length == 3 && a[0] == 10 && a[1] == 20 && a[2] == 30);
            return;
        }
        if (id.equals("boundary_static.returnStringArray")) {
            String[] a = BoundaryStatic.returnStringArray();
            require(a.length == 100 && "blort".equals(a[0]) && "zorch".equals(a[50]) &&
                    "fizmo".equals(a[99]) && a[1] == null);
            return;
        }
        if (id.equals("boundary_static.returnThisClass")) {
            require(BoundaryStatic.returnThisClass() == BoundaryStatic.class);
            return;
        }
        if (id.equals("boundary_static.takeThisClass")) {
            require(BoundaryStatic.takeThisClass(BoundaryStatic.class));
            return;
        }
        if (id.equals("boundary_static.returnInstance")) {
            require(BoundaryStatic.returnInstance().getClass() == BoundaryStatic.class);
            return;
        }
        if (id.equals("boundary_static.takeIntLong")) {
            require(BoundaryStatic.takeIntLong(914, 9140914091409140914L));
            return;
        }
        if (id.equals("boundary_static.takeLongInt")) {
            require(BoundaryStatic.takeLongInt(-4321L, 12341234));
            return;
        }
        if (id.equals("boundary_static.takeOneOfEach")) {
            require(BoundaryStatic.takeOneOfEach(false, (byte)1, (short)2, (char)3, 4, 5L, "six",
                                                 7.0f, 8.0, new int[] {9, 10}));
            return;
        }
        if (id.equals("boundary_static.takeCoolHandLuke")) {
            require(BoundaryStatic.takeCoolHandLuke(
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
                24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
                45, 46, 47, 48, 49, 50));
            return;
        }
        if (id.equals("boundary_static.holdsLock")) {
            require(BoundaryStatic.holdsLock());
            return;
        }
        if (id.equals("boundary_static.mixedAlignment")) {
            Object x = new Object();
            require(BoundaryStatic.mixedAlignment(0x123456789L, x, -0x234567891L, x));
            return;
        }
        if (id.equals("boundary_static.doublePair")) {
            require(BoundaryStatic.doublePair(1.25, 2.5) == 3.75);
            return;
        }
        if (id.equals("boundary_static.throwException")) {
            try {
                BoundaryStatic.throwException();
                throw new AssertionError("no exception");
            } catch (IllegalStateException e) {
                require("probe".equals(e.getMessage()));
            };
            return;
        }
        if (id.equals("boundary_instance.nop")) {
            new BoundaryInstance().nop();
            return;
        }
        if (id.equals("boundary_instance.returnBoolean")) {
            require(new BoundaryInstance().returnBoolean() == true);
            return;
        }
        if (id.equals("boundary_instance.takeBoolean")) {
            require(new BoundaryInstance().takeBoolean(true));
            return;
        }
        if (id.equals("boundary_instance.returnByte")) {
            require(new BoundaryInstance().returnByte() == (byte)123);
            return;
        }
        if (id.equals("boundary_instance.takeByte")) {
            require(new BoundaryInstance().takeByte((byte)-99));
            return;
        }
        if (id.equals("boundary_instance.returnShort")) {
            require(new BoundaryInstance().returnShort() == (short)-12345);
            return;
        }
        if (id.equals("boundary_instance.takeShort")) {
            require(new BoundaryInstance().takeShort((short)19991));
            return;
        }
        if (id.equals("boundary_instance.returnChar")) {
            require(new BoundaryInstance().returnChar() == (char)34567);
            return;
        }
        if (id.equals("boundary_instance.takeChar")) {
            require(new BoundaryInstance().takeChar((char)999));
            return;
        }
        if (id.equals("boundary_instance.returnInt")) {
            require(new BoundaryInstance().returnInt() == 12345678);
            return;
        }
        if (id.equals("boundary_instance.takeInt")) {
            require(new BoundaryInstance().takeInt(-999888777));
            return;
        }
        if (id.equals("boundary_instance.returnLong")) {
            require(new BoundaryInstance().returnLong() == -1098765432109876543L);
            return;
        }
        if (id.equals("boundary_instance.takeLong")) {
            require(new BoundaryInstance().takeLong(999888777666555444L));
            return;
        }
        if (id.equals("boundary_instance.returnFloat")) {
            require(new BoundaryInstance().returnFloat() == -98765.4321F);
            return;
        }
        if (id.equals("boundary_instance.takeFloat")) {
            require(new BoundaryInstance().takeFloat(-9988.7766F));
            return;
        }
        if (id.equals("boundary_instance.returnDouble")) {
            require(new BoundaryInstance().returnDouble() == 12345678.9);
            return;
        }
        if (id.equals("boundary_instance.takeDouble")) {
            require(new BoundaryInstance().takeDouble(999888777.666555));
            return;
        }
        if (id.equals("boundary_instance.returnNull")) {
            require(new BoundaryInstance().returnNull() == null);
            return;
        }
        if (id.equals("boundary_instance.takeNull")) {
            require(new BoundaryInstance().takeNull(null));
            return;
        }
        if (id.equals("boundary_instance.returnString")) {
            require("blort".equals(new BoundaryInstance().returnString()));
            return;
        }
        if (id.equals("boundary_instance.takeString")) {
            require(new BoundaryInstance().takeString("fuzzbot"));
            return;
        }
        if (id.equals("boundary_instance.returnShortArray")) {
            short[] a = new BoundaryInstance().returnShortArray();
            require(a.length == 3 && a[0] == 10 && a[1] == 20 && a[2] == 30);
            return;
        }
        if (id.equals("boundary_instance.returnStringArray")) {
            String[] a = new BoundaryInstance().returnStringArray();
            require(a.length == 100 && "blort".equals(a[0]) && "zorch".equals(a[50]) &&
                    "fizmo".equals(a[99]) && a[1] == null);
            return;
        }
        if (id.equals("boundary_instance.returnThisClass")) {
            require(new BoundaryInstance().returnThisClass() == BoundaryInstance.class);
            return;
        }
        if (id.equals("boundary_instance.takeThisClass")) {
            require(new BoundaryInstance().takeThisClass(BoundaryInstance.class));
            return;
        }
        if (id.equals("boundary_instance.returnInstance")) {
            require(new BoundaryInstance().returnInstance().getClass() == BoundaryInstance.class);
            return;
        }
        if (id.equals("boundary_instance.takeIntLong")) {
            require(new BoundaryInstance().takeIntLong(914, 9140914091409140914L));
            return;
        }
        if (id.equals("boundary_instance.takeLongInt")) {
            require(new BoundaryInstance().takeLongInt(-4321L, 12341234));
            return;
        }
        if (id.equals("boundary_instance.takeOneOfEach")) {
            require(new BoundaryInstance().takeOneOfEach(false, (byte)1, (short)2, (char)3, 4, 5L,
                                                         "six", 7.0f, 8.0, new int[] {9, 10}));
            return;
        }
        if (id.equals("boundary_instance.takeCoolHandLuke")) {
            require(new BoundaryInstance().takeCoolHandLuke(
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
                24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
                45, 46, 47, 48, 49, 50));
            return;
        }
        if (id.equals("boundary_instance.holdsLock")) {
            require(new BoundaryInstance().holdsLock());
            return;
        }
        if (id.equals("boundary_instance.mixedAlignment")) {
            Object x = new Object();
            require(new BoundaryInstance().mixedAlignment(0x123456789L, x, -0x234567891L, x));
            return;
        }
        if (id.equals("boundary_instance.doublePair")) {
            require(new BoundaryInstance().doublePair(1.25, 2.5) == 3.75);
            return;
        }
        if (id.equals("boundary_instance.throwException")) {
            try {
                new BoundaryInstance().throwException();
                throw new AssertionError("no exception");
            } catch (IllegalStateException e) {
                require("probe".equals(e.getMessage()));
            };
            return;
        }
        if (id.equals("call.CallBooleanMethod")) {
            checkNative("call.CallBooleanMethod");
            return;
        }
        if (id.equals("call.CallBooleanMethodA")) {
            checkNative("call.CallBooleanMethodA");
            return;
        }
        if (id.equals("call.CallBooleanMethodV")) {
            checkNative("call.CallBooleanMethodV");
            return;
        }
        if (id.equals("call.CallByteMethod")) {
            checkNative("call.CallByteMethod");
            return;
        }
        if (id.equals("call.CallByteMethodA")) {
            checkNative("call.CallByteMethodA");
            return;
        }
        if (id.equals("call.CallByteMethodV")) {
            checkNative("call.CallByteMethodV");
            return;
        }
        if (id.equals("call.CallShortMethod")) {
            checkNative("call.CallShortMethod");
            return;
        }
        if (id.equals("call.CallShortMethodA")) {
            checkNative("call.CallShortMethodA");
            return;
        }
        if (id.equals("call.CallShortMethodV")) {
            checkNative("call.CallShortMethodV");
            return;
        }
        if (id.equals("call.CallCharMethod")) {
            checkNative("call.CallCharMethod");
            return;
        }
        if (id.equals("call.CallCharMethodA")) {
            checkNative("call.CallCharMethodA");
            return;
        }
        if (id.equals("call.CallCharMethodV")) {
            checkNative("call.CallCharMethodV");
            return;
        }
        if (id.equals("call.CallIntMethod")) {
            checkNative("call.CallIntMethod");
            return;
        }
        if (id.equals("call.CallIntMethodA")) {
            checkNative("call.CallIntMethodA");
            return;
        }
        if (id.equals("call.CallIntMethodV")) {
            checkNative("call.CallIntMethodV");
            return;
        }
        if (id.equals("call.CallLongMethod")) {
            checkNative("call.CallLongMethod");
            return;
        }
        if (id.equals("call.CallLongMethodA")) {
            checkNative("call.CallLongMethodA");
            return;
        }
        if (id.equals("call.CallLongMethodV")) {
            checkNative("call.CallLongMethodV");
            return;
        }
        if (id.equals("call.CallFloatMethod")) {
            checkNative("call.CallFloatMethod");
            return;
        }
        if (id.equals("call.CallFloatMethodA")) {
            checkNative("call.CallFloatMethodA");
            return;
        }
        if (id.equals("call.CallFloatMethodV")) {
            checkNative("call.CallFloatMethodV");
            return;
        }
        if (id.equals("call.CallDoubleMethod")) {
            checkNative("call.CallDoubleMethod");
            return;
        }
        if (id.equals("call.CallDoubleMethodA")) {
            checkNative("call.CallDoubleMethodA");
            return;
        }
        if (id.equals("call.CallDoubleMethodV")) {
            checkNative("call.CallDoubleMethodV");
            return;
        }
        if (id.equals("call.CallObjectMethod")) {
            checkNative("call.CallObjectMethod");
            return;
        }
        if (id.equals("call.CallObjectMethodA")) {
            checkNative("call.CallObjectMethodA");
            return;
        }
        if (id.equals("call.CallObjectMethodV")) {
            checkNative("call.CallObjectMethodV");
            return;
        }
        if (id.equals("call.CallVoidMethod")) {
            checkNative("call.CallVoidMethod");
            return;
        }
        if (id.equals("call.CallVoidMethodA")) {
            checkNative("call.CallVoidMethodA");
            return;
        }
        if (id.equals("call.CallVoidMethodV")) {
            checkNative("call.CallVoidMethodV");
            return;
        }
        if (id.equals("call.CallBooleanMethod.takeOneOfEach")) {
            checkNative("call.CallBooleanMethod.takeOneOfEach");
            return;
        }
        if (id.equals("call.CallBooleanMethodA.takeOneOfEach")) {
            checkNative("call.CallBooleanMethodA.takeOneOfEach");
            return;
        }
        if (id.equals("call.CallBooleanMethodV.takeOneOfEach")) {
            checkNative("call.CallBooleanMethodV.takeOneOfEach");
            return;
        }
        if (id.equals("call.CallBooleanMethod.takeCoolHandLuke")) {
            checkNative("call.CallBooleanMethod.takeCoolHandLuke");
            return;
        }
        if (id.equals("call.CallBooleanMethodA.takeCoolHandLuke")) {
            checkNative("call.CallBooleanMethodA.takeCoolHandLuke");
            return;
        }
        if (id.equals("call.CallBooleanMethodV.takeCoolHandLuke")) {
            checkNative("call.CallBooleanMethodV.takeCoolHandLuke");
            return;
        }
        if (id.equals("call.CallNonvirtualBooleanMethod")) {
            checkNative("call.CallNonvirtualBooleanMethod");
            return;
        }
        if (id.equals("call.CallNonvirtualBooleanMethodA")) {
            checkNative("call.CallNonvirtualBooleanMethodA");
            return;
        }
        if (id.equals("call.CallNonvirtualBooleanMethodV")) {
            checkNative("call.CallNonvirtualBooleanMethodV");
            return;
        }
        if (id.equals("call.CallNonvirtualByteMethod")) {
            checkNative("call.CallNonvirtualByteMethod");
            return;
        }
        if (id.equals("call.CallNonvirtualByteMethodA")) {
            checkNative("call.CallNonvirtualByteMethodA");
            return;
        }
        if (id.equals("call.CallNonvirtualByteMethodV")) {
            checkNative("call.CallNonvirtualByteMethodV");
            return;
        }
        if (id.equals("call.CallNonvirtualShortMethod")) {
            checkNative("call.CallNonvirtualShortMethod");
            return;
        }
        if (id.equals("call.CallNonvirtualShortMethodA")) {
            checkNative("call.CallNonvirtualShortMethodA");
            return;
        }
        if (id.equals("call.CallNonvirtualShortMethodV")) {
            checkNative("call.CallNonvirtualShortMethodV");
            return;
        }
        if (id.equals("call.CallNonvirtualCharMethod")) {
            checkNative("call.CallNonvirtualCharMethod");
            return;
        }
        if (id.equals("call.CallNonvirtualCharMethodA")) {
            checkNative("call.CallNonvirtualCharMethodA");
            return;
        }
        if (id.equals("call.CallNonvirtualCharMethodV")) {
            checkNative("call.CallNonvirtualCharMethodV");
            return;
        }
        if (id.equals("call.CallNonvirtualIntMethod")) {
            checkNative("call.CallNonvirtualIntMethod");
            return;
        }
        if (id.equals("call.CallNonvirtualIntMethodA")) {
            checkNative("call.CallNonvirtualIntMethodA");
            return;
        }
        if (id.equals("call.CallNonvirtualIntMethodV")) {
            checkNative("call.CallNonvirtualIntMethodV");
            return;
        }
        if (id.equals("call.CallNonvirtualLongMethod")) {
            checkNative("call.CallNonvirtualLongMethod");
            return;
        }
        if (id.equals("call.CallNonvirtualLongMethodA")) {
            checkNative("call.CallNonvirtualLongMethodA");
            return;
        }
        if (id.equals("call.CallNonvirtualLongMethodV")) {
            checkNative("call.CallNonvirtualLongMethodV");
            return;
        }
        if (id.equals("call.CallNonvirtualFloatMethod")) {
            checkNative("call.CallNonvirtualFloatMethod");
            return;
        }
        if (id.equals("call.CallNonvirtualFloatMethodA")) {
            checkNative("call.CallNonvirtualFloatMethodA");
            return;
        }
        if (id.equals("call.CallNonvirtualFloatMethodV")) {
            checkNative("call.CallNonvirtualFloatMethodV");
            return;
        }
        if (id.equals("call.CallNonvirtualDoubleMethod")) {
            checkNative("call.CallNonvirtualDoubleMethod");
            return;
        }
        if (id.equals("call.CallNonvirtualDoubleMethodA")) {
            checkNative("call.CallNonvirtualDoubleMethodA");
            return;
        }
        if (id.equals("call.CallNonvirtualDoubleMethodV")) {
            checkNative("call.CallNonvirtualDoubleMethodV");
            return;
        }
        if (id.equals("call.CallNonvirtualObjectMethod")) {
            checkNative("call.CallNonvirtualObjectMethod");
            return;
        }
        if (id.equals("call.CallNonvirtualObjectMethodA")) {
            checkNative("call.CallNonvirtualObjectMethodA");
            return;
        }
        if (id.equals("call.CallNonvirtualObjectMethodV")) {
            checkNative("call.CallNonvirtualObjectMethodV");
            return;
        }
        if (id.equals("call.CallNonvirtualVoidMethod")) {
            checkNative("call.CallNonvirtualVoidMethod");
            return;
        }
        if (id.equals("call.CallNonvirtualVoidMethodA")) {
            checkNative("call.CallNonvirtualVoidMethodA");
            return;
        }
        if (id.equals("call.CallNonvirtualVoidMethodV")) {
            checkNative("call.CallNonvirtualVoidMethodV");
            return;
        }
        if (id.equals("call.CallNonvirtualBooleanMethod.takeOneOfEach")) {
            checkNative("call.CallNonvirtualBooleanMethod.takeOneOfEach");
            return;
        }
        if (id.equals("call.CallNonvirtualBooleanMethodA.takeOneOfEach")) {
            checkNative("call.CallNonvirtualBooleanMethodA.takeOneOfEach");
            return;
        }
        if (id.equals("call.CallNonvirtualBooleanMethodV.takeOneOfEach")) {
            checkNative("call.CallNonvirtualBooleanMethodV.takeOneOfEach");
            return;
        }
        if (id.equals("call.CallNonvirtualBooleanMethod.takeCoolHandLuke")) {
            checkNative("call.CallNonvirtualBooleanMethod.takeCoolHandLuke");
            return;
        }
        if (id.equals("call.CallNonvirtualBooleanMethodA.takeCoolHandLuke")) {
            checkNative("call.CallNonvirtualBooleanMethodA.takeCoolHandLuke");
            return;
        }
        if (id.equals("call.CallNonvirtualBooleanMethodV.takeCoolHandLuke")) {
            checkNative("call.CallNonvirtualBooleanMethodV.takeCoolHandLuke");
            return;
        }
        if (id.equals("call.CallStaticBooleanMethod")) {
            checkNative("call.CallStaticBooleanMethod");
            return;
        }
        if (id.equals("call.CallStaticBooleanMethodA")) {
            checkNative("call.CallStaticBooleanMethodA");
            return;
        }
        if (id.equals("call.CallStaticBooleanMethodV")) {
            checkNative("call.CallStaticBooleanMethodV");
            return;
        }
        if (id.equals("call.CallStaticByteMethod")) {
            checkNative("call.CallStaticByteMethod");
            return;
        }
        if (id.equals("call.CallStaticByteMethodA")) {
            checkNative("call.CallStaticByteMethodA");
            return;
        }
        if (id.equals("call.CallStaticByteMethodV")) {
            checkNative("call.CallStaticByteMethodV");
            return;
        }
        if (id.equals("call.CallStaticShortMethod")) {
            checkNative("call.CallStaticShortMethod");
            return;
        }
        if (id.equals("call.CallStaticShortMethodA")) {
            checkNative("call.CallStaticShortMethodA");
            return;
        }
        if (id.equals("call.CallStaticShortMethodV")) {
            checkNative("call.CallStaticShortMethodV");
            return;
        }
        if (id.equals("call.CallStaticCharMethod")) {
            checkNative("call.CallStaticCharMethod");
            return;
        }
        if (id.equals("call.CallStaticCharMethodA")) {
            checkNative("call.CallStaticCharMethodA");
            return;
        }
        if (id.equals("call.CallStaticCharMethodV")) {
            checkNative("call.CallStaticCharMethodV");
            return;
        }
        if (id.equals("call.CallStaticIntMethod")) {
            checkNative("call.CallStaticIntMethod");
            return;
        }
        if (id.equals("call.CallStaticIntMethodA")) {
            checkNative("call.CallStaticIntMethodA");
            return;
        }
        if (id.equals("call.CallStaticIntMethodV")) {
            checkNative("call.CallStaticIntMethodV");
            return;
        }
        if (id.equals("call.CallStaticLongMethod")) {
            checkNative("call.CallStaticLongMethod");
            return;
        }
        if (id.equals("call.CallStaticLongMethodA")) {
            checkNative("call.CallStaticLongMethodA");
            return;
        }
        if (id.equals("call.CallStaticLongMethodV")) {
            checkNative("call.CallStaticLongMethodV");
            return;
        }
        if (id.equals("call.CallStaticFloatMethod")) {
            checkNative("call.CallStaticFloatMethod");
            return;
        }
        if (id.equals("call.CallStaticFloatMethodA")) {
            checkNative("call.CallStaticFloatMethodA");
            return;
        }
        if (id.equals("call.CallStaticFloatMethodV")) {
            checkNative("call.CallStaticFloatMethodV");
            return;
        }
        if (id.equals("call.CallStaticDoubleMethod")) {
            checkNative("call.CallStaticDoubleMethod");
            return;
        }
        if (id.equals("call.CallStaticDoubleMethodA")) {
            checkNative("call.CallStaticDoubleMethodA");
            return;
        }
        if (id.equals("call.CallStaticDoubleMethodV")) {
            checkNative("call.CallStaticDoubleMethodV");
            return;
        }
        if (id.equals("call.CallStaticObjectMethod")) {
            checkNative("call.CallStaticObjectMethod");
            return;
        }
        if (id.equals("call.CallStaticObjectMethodA")) {
            checkNative("call.CallStaticObjectMethodA");
            return;
        }
        if (id.equals("call.CallStaticObjectMethodV")) {
            checkNative("call.CallStaticObjectMethodV");
            return;
        }
        if (id.equals("call.CallStaticVoidMethod")) {
            checkNative("call.CallStaticVoidMethod");
            return;
        }
        if (id.equals("call.CallStaticVoidMethodA")) {
            checkNative("call.CallStaticVoidMethodA");
            return;
        }
        if (id.equals("call.CallStaticVoidMethodV")) {
            checkNative("call.CallStaticVoidMethodV");
            return;
        }
        if (id.equals("call.CallStaticBooleanMethod.takeOneOfEach")) {
            checkNative("call.CallStaticBooleanMethod.takeOneOfEach");
            return;
        }
        if (id.equals("call.CallStaticBooleanMethodA.takeOneOfEach")) {
            checkNative("call.CallStaticBooleanMethodA.takeOneOfEach");
            return;
        }
        if (id.equals("call.CallStaticBooleanMethodV.takeOneOfEach")) {
            checkNative("call.CallStaticBooleanMethodV.takeOneOfEach");
            return;
        }
        if (id.equals("call.CallStaticBooleanMethod.takeCoolHandLuke")) {
            checkNative("call.CallStaticBooleanMethod.takeCoolHandLuke");
            return;
        }
        if (id.equals("call.CallStaticBooleanMethodA.takeCoolHandLuke")) {
            checkNative("call.CallStaticBooleanMethodA.takeCoolHandLuke");
            return;
        }
        if (id.equals("call.CallStaticBooleanMethodV.takeCoolHandLuke")) {
            checkNative("call.CallStaticBooleanMethodV.takeCoolHandLuke");
            return;
        }
        if (id.equals("version.GetVersion")) {
            checkNative(id);
            return;
        }
        if (id.equals("version.DefineClass")) {
            checkNative(id);
            return;
        }
        if (id.equals("library.JNI_OnLoad14")) {
            System.loadLibrary("jniprobe14");
            System.loadLibrary("jniprobe14");
            require(loadCount14() == 1);
            return;
        }
        if (id.equals("library.JNI_OnLoad")) {
            require(loadCount() == 1);
            return;
        }
        if (id.equals("library.loadLibraryTwice")) {
            System.loadLibrary("jniprobe");
            System.loadLibrary("jniprobe");
            require(loadCount() == 1);
            return;
        }
        if (id.equals("library.missing")) {
            try {
                System.loadLibrary("ogplay_probe_absent");
                throw new AssertionError("loaded missing library");
            } catch (UnsatisfiedLinkError expected) {
            };
            return;
        }
        if (id.equals("library.shortExport")) {
            require(shortExport() == 73);
            return;
        }
        if (id.equals("library.longExport")) {
            require(overloaded(4) == 5 && overloaded(4L) == 6L);
            return;
        }
        throw new IllegalArgumentException(id);
    }
    private static final String[] CASES = {"boundary_static.nop",
                                           "boundary_static.returnBoolean",
                                           "boundary_static.takeBoolean",
                                           "boundary_static.returnByte",
                                           "boundary_static.takeByte",
                                           "boundary_static.returnShort",
                                           "boundary_static.takeShort",
                                           "boundary_static.returnChar",
                                           "boundary_static.takeChar",
                                           "boundary_static.returnInt",
                                           "boundary_static.takeInt",
                                           "boundary_static.returnLong",
                                           "boundary_static.takeLong",
                                           "boundary_static.returnFloat",
                                           "boundary_static.takeFloat",
                                           "boundary_static.returnDouble",
                                           "boundary_static.takeDouble",
                                           "boundary_static.returnNull",
                                           "boundary_static.takeNull",
                                           "boundary_static.returnString",
                                           "boundary_static.takeString",
                                           "boundary_static.returnShortArray",
                                           "boundary_static.returnStringArray",
                                           "boundary_static.returnThisClass",
                                           "boundary_static.takeThisClass",
                                           "boundary_static.returnInstance",
                                           "boundary_static.takeIntLong",
                                           "boundary_static.takeLongInt",
                                           "boundary_static.takeOneOfEach",
                                           "boundary_static.takeCoolHandLuke",
                                           "boundary_static.holdsLock",
                                           "boundary_static.mixedAlignment",
                                           "boundary_static.doublePair",
                                           "boundary_static.throwException",
                                           "boundary_instance.nop",
                                           "boundary_instance.returnBoolean",
                                           "boundary_instance.takeBoolean",
                                           "boundary_instance.returnByte",
                                           "boundary_instance.takeByte",
                                           "boundary_instance.returnShort",
                                           "boundary_instance.takeShort",
                                           "boundary_instance.returnChar",
                                           "boundary_instance.takeChar",
                                           "boundary_instance.returnInt",
                                           "boundary_instance.takeInt",
                                           "boundary_instance.returnLong",
                                           "boundary_instance.takeLong",
                                           "boundary_instance.returnFloat",
                                           "boundary_instance.takeFloat",
                                           "boundary_instance.returnDouble",
                                           "boundary_instance.takeDouble",
                                           "boundary_instance.returnNull",
                                           "boundary_instance.takeNull",
                                           "boundary_instance.returnString",
                                           "boundary_instance.takeString",
                                           "boundary_instance.returnShortArray",
                                           "boundary_instance.returnStringArray",
                                           "boundary_instance.returnThisClass",
                                           "boundary_instance.takeThisClass",
                                           "boundary_instance.returnInstance",
                                           "boundary_instance.takeIntLong",
                                           "boundary_instance.takeLongInt",
                                           "boundary_instance.takeOneOfEach",
                                           "boundary_instance.takeCoolHandLuke",
                                           "boundary_instance.holdsLock",
                                           "boundary_instance.mixedAlignment",
                                           "boundary_instance.doublePair",
                                           "boundary_instance.throwException",
                                           "call.CallBooleanMethod",
                                           "call.CallBooleanMethodA",
                                           "call.CallBooleanMethodV",
                                           "call.CallByteMethod",
                                           "call.CallByteMethodA",
                                           "call.CallByteMethodV",
                                           "call.CallShortMethod",
                                           "call.CallShortMethodA",
                                           "call.CallShortMethodV",
                                           "call.CallCharMethod",
                                           "call.CallCharMethodA",
                                           "call.CallCharMethodV",
                                           "call.CallIntMethod",
                                           "call.CallIntMethodA",
                                           "call.CallIntMethodV",
                                           "call.CallLongMethod",
                                           "call.CallLongMethodA",
                                           "call.CallLongMethodV",
                                           "call.CallFloatMethod",
                                           "call.CallFloatMethodA",
                                           "call.CallFloatMethodV",
                                           "call.CallDoubleMethod",
                                           "call.CallDoubleMethodA",
                                           "call.CallDoubleMethodV",
                                           "call.CallObjectMethod",
                                           "call.CallObjectMethodA",
                                           "call.CallObjectMethodV",
                                           "call.CallVoidMethod",
                                           "call.CallVoidMethodA",
                                           "call.CallVoidMethodV",
                                           "call.CallBooleanMethod.takeOneOfEach",
                                           "call.CallBooleanMethodA.takeOneOfEach",
                                           "call.CallBooleanMethodV.takeOneOfEach",
                                           "call.CallBooleanMethod.takeCoolHandLuke",
                                           "call.CallBooleanMethodA.takeCoolHandLuke",
                                           "call.CallBooleanMethodV.takeCoolHandLuke",
                                           "call.CallNonvirtualBooleanMethod",
                                           "call.CallNonvirtualBooleanMethodA",
                                           "call.CallNonvirtualBooleanMethodV",
                                           "call.CallNonvirtualByteMethod",
                                           "call.CallNonvirtualByteMethodA",
                                           "call.CallNonvirtualByteMethodV",
                                           "call.CallNonvirtualShortMethod",
                                           "call.CallNonvirtualShortMethodA",
                                           "call.CallNonvirtualShortMethodV",
                                           "call.CallNonvirtualCharMethod",
                                           "call.CallNonvirtualCharMethodA",
                                           "call.CallNonvirtualCharMethodV",
                                           "call.CallNonvirtualIntMethod",
                                           "call.CallNonvirtualIntMethodA",
                                           "call.CallNonvirtualIntMethodV",
                                           "call.CallNonvirtualLongMethod",
                                           "call.CallNonvirtualLongMethodA",
                                           "call.CallNonvirtualLongMethodV",
                                           "call.CallNonvirtualFloatMethod",
                                           "call.CallNonvirtualFloatMethodA",
                                           "call.CallNonvirtualFloatMethodV",
                                           "call.CallNonvirtualDoubleMethod",
                                           "call.CallNonvirtualDoubleMethodA",
                                           "call.CallNonvirtualDoubleMethodV",
                                           "call.CallNonvirtualObjectMethod",
                                           "call.CallNonvirtualObjectMethodA",
                                           "call.CallNonvirtualObjectMethodV",
                                           "call.CallNonvirtualVoidMethod",
                                           "call.CallNonvirtualVoidMethodA",
                                           "call.CallNonvirtualVoidMethodV",
                                           "call.CallNonvirtualBooleanMethod.takeOneOfEach",
                                           "call.CallNonvirtualBooleanMethodA.takeOneOfEach",
                                           "call.CallNonvirtualBooleanMethodV.takeOneOfEach",
                                           "call.CallNonvirtualBooleanMethod.takeCoolHandLuke",
                                           "call.CallNonvirtualBooleanMethodA.takeCoolHandLuke",
                                           "call.CallNonvirtualBooleanMethodV.takeCoolHandLuke",
                                           "call.CallStaticBooleanMethod",
                                           "call.CallStaticBooleanMethodA",
                                           "call.CallStaticBooleanMethodV",
                                           "call.CallStaticByteMethod",
                                           "call.CallStaticByteMethodA",
                                           "call.CallStaticByteMethodV",
                                           "call.CallStaticShortMethod",
                                           "call.CallStaticShortMethodA",
                                           "call.CallStaticShortMethodV",
                                           "call.CallStaticCharMethod",
                                           "call.CallStaticCharMethodA",
                                           "call.CallStaticCharMethodV",
                                           "call.CallStaticIntMethod",
                                           "call.CallStaticIntMethodA",
                                           "call.CallStaticIntMethodV",
                                           "call.CallStaticLongMethod",
                                           "call.CallStaticLongMethodA",
                                           "call.CallStaticLongMethodV",
                                           "call.CallStaticFloatMethod",
                                           "call.CallStaticFloatMethodA",
                                           "call.CallStaticFloatMethodV",
                                           "call.CallStaticDoubleMethod",
                                           "call.CallStaticDoubleMethodA",
                                           "call.CallStaticDoubleMethodV",
                                           "call.CallStaticObjectMethod",
                                           "call.CallStaticObjectMethodA",
                                           "call.CallStaticObjectMethodV",
                                           "call.CallStaticVoidMethod",
                                           "call.CallStaticVoidMethodA",
                                           "call.CallStaticVoidMethodV",
                                           "call.CallStaticBooleanMethod.takeOneOfEach",
                                           "call.CallStaticBooleanMethodA.takeOneOfEach",
                                           "call.CallStaticBooleanMethodV.takeOneOfEach",
                                           "call.CallStaticBooleanMethod.takeCoolHandLuke",
                                           "call.CallStaticBooleanMethodA.takeCoolHandLuke",
                                           "call.CallStaticBooleanMethodV.takeCoolHandLuke",
                                           "version.GetVersion",
                                           "version.DefineClass",
                                           "library.JNI_OnLoad14",
                                           "library.JNI_OnLoad",
                                           "library.loadLibraryTwice",
                                           "library.missing",
                                           "library.shortExport",
                                           "library.longExport"};
    public static void main(String[] args) {
        if (args.length == 1 && args[0].equals("--list")) {
            for (String id : CASES)
                System.out.println(id);
            return;
        }
        String group = null, selected = null;
        if (args.length == 2 && args[0].equals("--group"))
            group = args[1];
        else if (args.length == 2 && args[0].equals("--case"))
            selected = args[1];
        else if (args.length != 0)
            throw new IllegalArgumentException("--list | --group g | --case id");
        int passed = 0, total = 0;
        for (String id : CASES) {
            if (group != null && !id.startsWith(group + "."))
                continue;
            if (selected != null && !id.equals(selected))
                continue;
            total++;
            try {
                System.loadLibrary("jniprobe");
                run(id);
                System.out.println("PASS " + id);
                passed++;
            } catch (Throwable e) {
                System.out.println("FAIL " + id + ": " + e);
            }
        }
        if (total == 0)
            throw new IllegalArgumentException("no matching case");
        System.out.println("DONE " + passed + "/" + total);
    }
}

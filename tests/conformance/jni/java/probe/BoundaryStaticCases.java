/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */
package probe;
import static probe.NativeSupport.*;
final class BoundaryStaticCases implements CaseGroup {
    public String[] ids() {
        return new String[] {"boundary_static.nop",
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
                             "boundary_static.throwException"};
    }
    public void run(String id) {
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
        throw new IllegalArgumentException(id);
    }
}

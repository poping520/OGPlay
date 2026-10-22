/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */
package probe;
import static probe.NativeSupport.*;
final class BoundaryInstanceCases implements CaseGroup {
    public String[] ids() {
        return new String[] {"boundary_instance.nop",
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
                             "boundary_instance.throwException"};
    }
    public void run(String id) {
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
        throw new IllegalArgumentException(id);
    }
}

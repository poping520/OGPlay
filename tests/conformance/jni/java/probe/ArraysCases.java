/* SPDX-License-Identifier: Apache-2.0
 * Independent probes derived from Dalvik vm/Jni.cpp, Android 4.4.
 * Every oracle below cites the corresponding implementation, not HotSpot.
 */
package probe;
import static probe.NativeSupport.*;
final class ArraysCases implements CaseGroup {
    private static native Object create(int type);
    private static native String probe(int test, Object array, int action);
    public String[] ids() {
        return new String[] {"array.NewBooleanArray",
                             "array.GetBooleanArrayElements",
                             "array.ReleaseBooleanArrayElements",
                             "array.GetBooleanArrayRegion",
                             "array.SetBooleanArrayRegion",
                             "array.GetBooleanArrayElements.dvm_isCopy",
                             "array.GetBooleanArrayElements.dvm_alias",
                             "array.ReleaseBooleanArrayElements.dvm_abort",
                             "array.ReleaseBooleanArrayElements.dvm_commit",
                             "array.GetBooleanArrayRegion.bounds",
                             "array.SetBooleanArrayRegion.bounds",
                             "array.NewByteArray",
                             "array.GetByteArrayElements",
                             "array.ReleaseByteArrayElements",
                             "array.GetByteArrayRegion",
                             "array.SetByteArrayRegion",
                             "array.GetByteArrayElements.dvm_isCopy",
                             "array.GetByteArrayElements.dvm_alias",
                             "array.ReleaseByteArrayElements.dvm_abort",
                             "array.ReleaseByteArrayElements.dvm_commit",
                             "array.GetByteArrayRegion.bounds",
                             "array.SetByteArrayRegion.bounds",
                             "array.NewShortArray",
                             "array.GetShortArrayElements",
                             "array.ReleaseShortArrayElements",
                             "array.GetShortArrayRegion",
                             "array.SetShortArrayRegion",
                             "array.GetShortArrayElements.dvm_isCopy",
                             "array.GetShortArrayElements.dvm_alias",
                             "array.ReleaseShortArrayElements.dvm_abort",
                             "array.ReleaseShortArrayElements.dvm_commit",
                             "array.GetShortArrayRegion.bounds",
                             "array.SetShortArrayRegion.bounds",
                             "array.NewCharArray",
                             "array.GetCharArrayElements",
                             "array.ReleaseCharArrayElements",
                             "array.GetCharArrayRegion",
                             "array.SetCharArrayRegion",
                             "array.GetCharArrayElements.dvm_isCopy",
                             "array.GetCharArrayElements.dvm_alias",
                             "array.ReleaseCharArrayElements.dvm_abort",
                             "array.ReleaseCharArrayElements.dvm_commit",
                             "array.GetCharArrayRegion.bounds",
                             "array.SetCharArrayRegion.bounds",
                             "array.NewIntArray",
                             "array.GetIntArrayElements",
                             "array.ReleaseIntArrayElements",
                             "array.GetIntArrayRegion",
                             "array.SetIntArrayRegion",
                             "array.GetIntArrayElements.dvm_isCopy",
                             "array.GetIntArrayElements.dvm_alias",
                             "array.ReleaseIntArrayElements.dvm_abort",
                             "array.ReleaseIntArrayElements.dvm_commit",
                             "array.GetIntArrayRegion.bounds",
                             "array.SetIntArrayRegion.bounds",
                             "array.NewLongArray",
                             "array.GetLongArrayElements",
                             "array.ReleaseLongArrayElements",
                             "array.GetLongArrayRegion",
                             "array.SetLongArrayRegion",
                             "array.GetLongArrayElements.dvm_isCopy",
                             "array.GetLongArrayElements.dvm_alias",
                             "array.ReleaseLongArrayElements.dvm_abort",
                             "array.ReleaseLongArrayElements.dvm_commit",
                             "array.GetLongArrayRegion.bounds",
                             "array.SetLongArrayRegion.bounds",
                             "array.NewFloatArray",
                             "array.GetFloatArrayElements",
                             "array.ReleaseFloatArrayElements",
                             "array.GetFloatArrayRegion",
                             "array.SetFloatArrayRegion",
                             "array.GetFloatArrayElements.dvm_isCopy",
                             "array.GetFloatArrayElements.dvm_alias",
                             "array.ReleaseFloatArrayElements.dvm_abort",
                             "array.ReleaseFloatArrayElements.dvm_commit",
                             "array.GetFloatArrayRegion.bounds",
                             "array.SetFloatArrayRegion.bounds",
                             "array.NewDoubleArray",
                             "array.GetDoubleArrayElements",
                             "array.ReleaseDoubleArrayElements",
                             "array.GetDoubleArrayRegion",
                             "array.SetDoubleArrayRegion",
                             "array.GetDoubleArrayElements.dvm_isCopy",
                             "array.GetDoubleArrayElements.dvm_alias",
                             "array.ReleaseDoubleArrayElements.dvm_abort",
                             "array.ReleaseDoubleArrayElements.dvm_commit",
                             "array.GetDoubleArrayRegion.bounds",
                             "array.SetDoubleArrayRegion.bounds",
                             "array.GetPrimitiveArrayCritical",
                             "array.ReleasePrimitiveArrayCritical",
                             "array.GetPrimitiveArrayCritical.dvm_isCopy",
                             "array.GetPrimitiveArrayCritical.dvm_alias",
                             "array.ReleasePrimitiveArrayCritical.dvm_commit",
                             "array.GetObjectArrayElement",
                             "array.GetObjectArrayElement.nullElement",
                             "array.GetObjectArrayElement.bounds"};
    }
    public void run(String id) {
        // Jni.cpp:2326-2344
        if (id.equals("array.NewBooleanArray")) {
            boolean[] a = (boolean[])create(0);
            require(a.length == 3 && a[0] == false && a[1] == false && a[2] == false);
            return;
        }
        // Jni.cpp:2355-2366
        if (id.equals("array.GetBooleanArrayElements")) {
            boolean[] a = new boolean[] {false, false, false};
            check(probe(1, a, 0));
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseBooleanArrayElements")) {
            boolean[] a = new boolean[] {false, false, false};
            check(probe(2, a, 0));
            require(a[0] == true);
            return;
        }
        // Jni.cpp:2400-2412
        if (id.equals("array.GetBooleanArrayRegion")) {
            boolean[] a = new boolean[] {false, false, false};
            check(probe(3, a, 0));
            return;
        }
        // Jni.cpp:2417-2429
        if (id.equals("array.SetBooleanArrayRegion")) {
            boolean[] a = new boolean[] {false, false, false};
            check(probe(4, a, 0));
            require(a[0] == false && a[1] == true && a[2] == false);
            return;
        }
        // Jni.cpp:2361-2365
        if (id.equals("array.GetBooleanArrayElements.dvm_isCopy")) {
            boolean[] a = new boolean[] {false, false, false};
            check(probe(5, a, 0));
            return;
        }
        // Jni.cpp:2361-2366
        if (id.equals("array.GetBooleanArrayElements.dvm_alias")) {
            boolean[] a = new boolean[] {false, false, false};
            check(probe(6, a, 0));
            try {
                require(a[0] == true);
            } finally {
                check(probe(6, a, 3));
            };
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseBooleanArrayElements.dvm_abort")) {
            boolean[] a = new boolean[] {false, false, false};
            check(probe(7, a, 0));
            check(probe(7, a, 1));
            require(a[0] == true);
            return;
        }
        // Jni.cpp:2380-2385
        if (id.equals("array.ReleaseBooleanArrayElements.dvm_commit")) {
            boolean[] a = new boolean[] {false, false, false};
            check(probe(8, a, 0));
            try {
                check(probe(8, a, 2));
                require(a[0] == false);
            } finally {
                check(probe(8, a, 3));
            };
            return;
        }
        // Jni.cpp:2388-2394;2407-2410
        if (id.equals("array.GetBooleanArrayRegion.bounds")) {
            boolean[] a = new boolean[] {false, false, false};
            check(probe(9, a, 0));
            return;
        }
        // Jni.cpp:2388-2394;2424-2427
        if (id.equals("array.SetBooleanArrayRegion.bounds")) {
            boolean[] a = new boolean[] {false, false, false};
            check(probe(10, a, 0));
            return;
        }
        // Jni.cpp:2326-2344
        if (id.equals("array.NewByteArray")) {
            byte[] a = (byte[])create(1);
            require(a.length == 3 && a[0] == 0 && a[1] == 0 && a[2] == 0);
            return;
        }
        // Jni.cpp:2355-2366
        if (id.equals("array.GetByteArrayElements")) {
            byte[] a = new byte[] {(byte)-99, (byte)-99, (byte)-99};
            check(probe(12, a, 0));
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseByteArrayElements")) {
            byte[] a = new byte[] {(byte)-99, (byte)-99, (byte)-99};
            check(probe(13, a, 0));
            require(a[0] == (byte)-73);
            return;
        }
        // Jni.cpp:2400-2412
        if (id.equals("array.GetByteArrayRegion")) {
            byte[] a = new byte[] {(byte)-99, (byte)-99, (byte)-99};
            check(probe(14, a, 0));
            return;
        }
        // Jni.cpp:2417-2429
        if (id.equals("array.SetByteArrayRegion")) {
            byte[] a = new byte[] {(byte)-99, (byte)-99, (byte)-99};
            check(probe(15, a, 0));
            require(a[0] == (byte)-99 && a[1] == (byte)-73 && a[2] == (byte)-99);
            return;
        }
        // Jni.cpp:2361-2365
        if (id.equals("array.GetByteArrayElements.dvm_isCopy")) {
            byte[] a = new byte[] {(byte)-99, (byte)-99, (byte)-99};
            check(probe(16, a, 0));
            return;
        }
        // Jni.cpp:2361-2366
        if (id.equals("array.GetByteArrayElements.dvm_alias")) {
            byte[] a = new byte[] {(byte)-99, (byte)-99, (byte)-99};
            check(probe(17, a, 0));
            try {
                require(a[0] == (byte)-73);
            } finally {
                check(probe(17, a, 3));
            };
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseByteArrayElements.dvm_abort")) {
            byte[] a = new byte[] {(byte)-99, (byte)-99, (byte)-99};
            check(probe(18, a, 0));
            check(probe(18, a, 1));
            require(a[0] == (byte)-73);
            return;
        }
        // Jni.cpp:2380-2385
        if (id.equals("array.ReleaseByteArrayElements.dvm_commit")) {
            byte[] a = new byte[] {(byte)-99, (byte)-99, (byte)-99};
            check(probe(19, a, 0));
            try {
                check(probe(19, a, 2));
                require(a[0] == (byte)29);
            } finally {
                check(probe(19, a, 3));
            };
            return;
        }
        // Jni.cpp:2388-2394;2407-2410
        if (id.equals("array.GetByteArrayRegion.bounds")) {
            byte[] a = new byte[] {(byte)-99, (byte)-99, (byte)-99};
            check(probe(20, a, 0));
            return;
        }
        // Jni.cpp:2388-2394;2424-2427
        if (id.equals("array.SetByteArrayRegion.bounds")) {
            byte[] a = new byte[] {(byte)-99, (byte)-99, (byte)-99};
            check(probe(21, a, 0));
            return;
        }
        // Jni.cpp:2326-2344
        if (id.equals("array.NewShortArray")) {
            short[] a = (short[])create(2);
            require(a.length == 3 && a[0] == 0 && a[1] == 0 && a[2] == 0);
            return;
        }
        // Jni.cpp:2355-2366
        if (id.equals("array.GetShortArrayElements")) {
            short[] a = new short[] {(short)-23456, (short)-23456, (short)-23456};
            check(probe(23, a, 0));
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseShortArrayElements")) {
            short[] a = new short[] {(short)-23456, (short)-23456, (short)-23456};
            check(probe(24, a, 0));
            require(a[0] == (short)-12345);
            return;
        }
        // Jni.cpp:2400-2412
        if (id.equals("array.GetShortArrayRegion")) {
            short[] a = new short[] {(short)-23456, (short)-23456, (short)-23456};
            check(probe(25, a, 0));
            return;
        }
        // Jni.cpp:2417-2429
        if (id.equals("array.SetShortArrayRegion")) {
            short[] a = new short[] {(short)-23456, (short)-23456, (short)-23456};
            check(probe(26, a, 0));
            require(a[0] == (short)-23456 && a[1] == (short)-12345 && a[2] == (short)-23456);
            return;
        }
        // Jni.cpp:2361-2365
        if (id.equals("array.GetShortArrayElements.dvm_isCopy")) {
            short[] a = new short[] {(short)-23456, (short)-23456, (short)-23456};
            check(probe(27, a, 0));
            return;
        }
        // Jni.cpp:2361-2366
        if (id.equals("array.GetShortArrayElements.dvm_alias")) {
            short[] a = new short[] {(short)-23456, (short)-23456, (short)-23456};
            check(probe(28, a, 0));
            try {
                require(a[0] == (short)-12345);
            } finally {
                check(probe(28, a, 3));
            };
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseShortArrayElements.dvm_abort")) {
            short[] a = new short[] {(short)-23456, (short)-23456, (short)-23456};
            check(probe(29, a, 0));
            check(probe(29, a, 1));
            require(a[0] == (short)-12345);
            return;
        }
        // Jni.cpp:2380-2385
        if (id.equals("array.ReleaseShortArrayElements.dvm_commit")) {
            short[] a = new short[] {(short)-23456, (short)-23456, (short)-23456};
            check(probe(30, a, 0));
            try {
                check(probe(30, a, 2));
                require(a[0] == (short)23456);
            } finally {
                check(probe(30, a, 3));
            };
            return;
        }
        // Jni.cpp:2388-2394;2407-2410
        if (id.equals("array.GetShortArrayRegion.bounds")) {
            short[] a = new short[] {(short)-23456, (short)-23456, (short)-23456};
            check(probe(31, a, 0));
            return;
        }
        // Jni.cpp:2388-2394;2424-2427
        if (id.equals("array.SetShortArrayRegion.bounds")) {
            short[] a = new short[] {(short)-23456, (short)-23456, (short)-23456};
            check(probe(32, a, 0));
            return;
        }
        // Jni.cpp:2326-2344
        if (id.equals("array.NewCharArray")) {
            char[] a = (char[])create(3);
            require(a.length == 3 && a[0] == 0 && a[1] == 0 && a[2] == 0);
            return;
        }
        // Jni.cpp:2355-2366
        if (id.equals("array.GetCharArrayElements")) {
            char[] a = new char[] {(char)0xffff, (char)0xffff, (char)0xffff};
            check(probe(34, a, 0));
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseCharArrayElements")) {
            char[] a = new char[] {(char)0xffff, (char)0xffff, (char)0xffff};
            check(probe(35, a, 0));
            require(a[0] == (char)0xabcd);
            return;
        }
        // Jni.cpp:2400-2412
        if (id.equals("array.GetCharArrayRegion")) {
            char[] a = new char[] {(char)0xffff, (char)0xffff, (char)0xffff};
            check(probe(36, a, 0));
            return;
        }
        // Jni.cpp:2417-2429
        if (id.equals("array.SetCharArrayRegion")) {
            char[] a = new char[] {(char)0xffff, (char)0xffff, (char)0xffff};
            check(probe(37, a, 0));
            require(a[0] == (char)0xffff && a[1] == (char)0xabcd && a[2] == (char)0xffff);
            return;
        }
        // Jni.cpp:2361-2365
        if (id.equals("array.GetCharArrayElements.dvm_isCopy")) {
            char[] a = new char[] {(char)0xffff, (char)0xffff, (char)0xffff};
            check(probe(38, a, 0));
            return;
        }
        // Jni.cpp:2361-2366
        if (id.equals("array.GetCharArrayElements.dvm_alias")) {
            char[] a = new char[] {(char)0xffff, (char)0xffff, (char)0xffff};
            check(probe(39, a, 0));
            try {
                require(a[0] == (char)0xabcd);
            } finally {
                check(probe(39, a, 3));
            };
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseCharArrayElements.dvm_abort")) {
            char[] a = new char[] {(char)0xffff, (char)0xffff, (char)0xffff};
            check(probe(40, a, 0));
            check(probe(40, a, 1));
            require(a[0] == (char)0xabcd);
            return;
        }
        // Jni.cpp:2380-2385
        if (id.equals("array.ReleaseCharArrayElements.dvm_commit")) {
            char[] a = new char[] {(char)0xffff, (char)0xffff, (char)0xffff};
            check(probe(41, a, 0));
            try {
                check(probe(41, a, 2));
                require(a[0] == (char)0x9876);
            } finally {
                check(probe(41, a, 3));
            };
            return;
        }
        // Jni.cpp:2388-2394;2407-2410
        if (id.equals("array.GetCharArrayRegion.bounds")) {
            char[] a = new char[] {(char)0xffff, (char)0xffff, (char)0xffff};
            check(probe(42, a, 0));
            return;
        }
        // Jni.cpp:2388-2394;2424-2427
        if (id.equals("array.SetCharArrayRegion.bounds")) {
            char[] a = new char[] {(char)0xffff, (char)0xffff, (char)0xffff};
            check(probe(43, a, 0));
            return;
        }
        // Jni.cpp:2326-2344
        if (id.equals("array.NewIntArray")) {
            int[] a = (int[])create(4);
            require(a.length == 3 && a[0] == 0 && a[1] == 0 && a[2] == 0);
            return;
        }
        // Jni.cpp:2355-2366
        if (id.equals("array.GetIntArrayElements")) {
            int[] a = new int[] {0x89abcdef, 0x89abcdef, 0x89abcdef};
            check(probe(45, a, 0));
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseIntArrayElements")) {
            int[] a = new int[] {0x89abcdef, 0x89abcdef, 0x89abcdef};
            check(probe(46, a, 0));
            require(a[0] == 0x87654321);
            return;
        }
        // Jni.cpp:2400-2412
        if (id.equals("array.GetIntArrayRegion")) {
            int[] a = new int[] {0x89abcdef, 0x89abcdef, 0x89abcdef};
            check(probe(47, a, 0));
            return;
        }
        // Jni.cpp:2417-2429
        if (id.equals("array.SetIntArrayRegion")) {
            int[] a = new int[] {0x89abcdef, 0x89abcdef, 0x89abcdef};
            check(probe(48, a, 0));
            require(a[0] == 0x89abcdef && a[1] == 0x87654321 && a[2] == 0x89abcdef);
            return;
        }
        // Jni.cpp:2361-2365
        if (id.equals("array.GetIntArrayElements.dvm_isCopy")) {
            int[] a = new int[] {0x89abcdef, 0x89abcdef, 0x89abcdef};
            check(probe(49, a, 0));
            return;
        }
        // Jni.cpp:2361-2366
        if (id.equals("array.GetIntArrayElements.dvm_alias")) {
            int[] a = new int[] {0x89abcdef, 0x89abcdef, 0x89abcdef};
            check(probe(50, a, 0));
            try {
                require(a[0] == 0x87654321);
            } finally {
                check(probe(50, a, 3));
            };
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseIntArrayElements.dvm_abort")) {
            int[] a = new int[] {0x89abcdef, 0x89abcdef, 0x89abcdef};
            check(probe(51, a, 0));
            check(probe(51, a, 1));
            require(a[0] == 0x87654321);
            return;
        }
        // Jni.cpp:2380-2385
        if (id.equals("array.ReleaseIntArrayElements.dvm_commit")) {
            int[] a = new int[] {0x89abcdef, 0x89abcdef, 0x89abcdef};
            check(probe(52, a, 0));
            try {
                check(probe(52, a, 2));
                require(a[0] == 0x76543210);
            } finally {
                check(probe(52, a, 3));
            };
            return;
        }
        // Jni.cpp:2388-2394;2407-2410
        if (id.equals("array.GetIntArrayRegion.bounds")) {
            int[] a = new int[] {0x89abcdef, 0x89abcdef, 0x89abcdef};
            check(probe(53, a, 0));
            return;
        }
        // Jni.cpp:2388-2394;2424-2427
        if (id.equals("array.SetIntArrayRegion.bounds")) {
            int[] a = new int[] {0x89abcdef, 0x89abcdef, 0x89abcdef};
            check(probe(54, a, 0));
            return;
        }
        // Jni.cpp:2326-2344
        if (id.equals("array.NewLongArray")) {
            long[] a = (long[])create(5);
            require(a.length == 3 && a[0] == 0 && a[1] == 0 && a[2] == 0);
            return;
        }
        // Jni.cpp:2355-2366
        if (id.equals("array.GetLongArrayElements")) {
            long[] a = new long[] {0x8123456789abcdefL, 0x8123456789abcdefL, 0x8123456789abcdefL};
            check(probe(56, a, 0));
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseLongArrayElements")) {
            long[] a = new long[] {0x8123456789abcdefL, 0x8123456789abcdefL, 0x8123456789abcdefL};
            check(probe(57, a, 0));
            require(a[0] == 0x823456789abcdef1L);
            return;
        }
        // Jni.cpp:2400-2412
        if (id.equals("array.GetLongArrayRegion")) {
            long[] a = new long[] {0x8123456789abcdefL, 0x8123456789abcdefL, 0x8123456789abcdefL};
            check(probe(58, a, 0));
            return;
        }
        // Jni.cpp:2417-2429
        if (id.equals("array.SetLongArrayRegion")) {
            long[] a = new long[] {0x8123456789abcdefL, 0x8123456789abcdefL, 0x8123456789abcdefL};
            check(probe(59, a, 0));
            require(a[0] == 0x8123456789abcdefL && a[1] == 0x823456789abcdef1L &&
                    a[2] == 0x8123456789abcdefL);
            return;
        }
        // Jni.cpp:2361-2365
        if (id.equals("array.GetLongArrayElements.dvm_isCopy")) {
            long[] a = new long[] {0x8123456789abcdefL, 0x8123456789abcdefL, 0x8123456789abcdefL};
            check(probe(60, a, 0));
            return;
        }
        // Jni.cpp:2361-2366
        if (id.equals("array.GetLongArrayElements.dvm_alias")) {
            long[] a = new long[] {0x8123456789abcdefL, 0x8123456789abcdefL, 0x8123456789abcdefL};
            check(probe(61, a, 0));
            try {
                require(a[0] == 0x823456789abcdef1L);
            } finally {
                check(probe(61, a, 3));
            };
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseLongArrayElements.dvm_abort")) {
            long[] a = new long[] {0x8123456789abcdefL, 0x8123456789abcdefL, 0x8123456789abcdefL};
            check(probe(62, a, 0));
            check(probe(62, a, 1));
            require(a[0] == 0x823456789abcdef1L);
            return;
        }
        // Jni.cpp:2380-2385
        if (id.equals("array.ReleaseLongArrayElements.dvm_commit")) {
            long[] a = new long[] {0x8123456789abcdefL, 0x8123456789abcdefL, 0x8123456789abcdefL};
            check(probe(63, a, 0));
            try {
                check(probe(63, a, 2));
                require(a[0] == 0x73456789abcdef12L);
            } finally {
                check(probe(63, a, 3));
            };
            return;
        }
        // Jni.cpp:2388-2394;2407-2410
        if (id.equals("array.GetLongArrayRegion.bounds")) {
            long[] a = new long[] {0x8123456789abcdefL, 0x8123456789abcdefL, 0x8123456789abcdefL};
            check(probe(64, a, 0));
            return;
        }
        // Jni.cpp:2388-2394;2424-2427
        if (id.equals("array.SetLongArrayRegion.bounds")) {
            long[] a = new long[] {0x8123456789abcdefL, 0x8123456789abcdefL, 0x8123456789abcdefL};
            check(probe(65, a, 0));
            return;
        }
        // Jni.cpp:2326-2344
        if (id.equals("array.NewFloatArray")) {
            float[] a = (float[])create(6);
            require(a.length == 3 && a[0] == 0 && a[1] == 0 && a[2] == 0);
            return;
        }
        // Jni.cpp:2355-2366
        if (id.equals("array.GetFloatArrayElements")) {
            float[] a = new float[] {-1.25f, -1.25f, -1.25f};
            check(probe(67, a, 0));
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseFloatArrayElements")) {
            float[] a = new float[] {-1.25f, -1.25f, -1.25f};
            check(probe(68, a, 0));
            require(a[0] == 13.25f);
            return;
        }
        // Jni.cpp:2400-2412
        if (id.equals("array.GetFloatArrayRegion")) {
            float[] a = new float[] {-1.25f, -1.25f, -1.25f};
            check(probe(69, a, 0));
            return;
        }
        // Jni.cpp:2417-2429
        if (id.equals("array.SetFloatArrayRegion")) {
            float[] a = new float[] {-1.25f, -1.25f, -1.25f};
            check(probe(70, a, 0));
            require(a[0] == -1.25f && a[1] == 13.25f && a[2] == -1.25f);
            return;
        }
        // Jni.cpp:2361-2365
        if (id.equals("array.GetFloatArrayElements.dvm_isCopy")) {
            float[] a = new float[] {-1.25f, -1.25f, -1.25f};
            check(probe(71, a, 0));
            return;
        }
        // Jni.cpp:2361-2366
        if (id.equals("array.GetFloatArrayElements.dvm_alias")) {
            float[] a = new float[] {-1.25f, -1.25f, -1.25f};
            check(probe(72, a, 0));
            try {
                require(a[0] == 13.25f);
            } finally {
                check(probe(72, a, 3));
            };
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseFloatArrayElements.dvm_abort")) {
            float[] a = new float[] {-1.25f, -1.25f, -1.25f};
            check(probe(73, a, 0));
            check(probe(73, a, 1));
            require(a[0] == 13.25f);
            return;
        }
        // Jni.cpp:2380-2385
        if (id.equals("array.ReleaseFloatArrayElements.dvm_commit")) {
            float[] a = new float[] {-1.25f, -1.25f, -1.25f};
            check(probe(74, a, 0));
            try {
                check(probe(74, a, 2));
                require(a[0] == -27.5f);
            } finally {
                check(probe(74, a, 3));
            };
            return;
        }
        // Jni.cpp:2388-2394;2407-2410
        if (id.equals("array.GetFloatArrayRegion.bounds")) {
            float[] a = new float[] {-1.25f, -1.25f, -1.25f};
            check(probe(75, a, 0));
            return;
        }
        // Jni.cpp:2388-2394;2424-2427
        if (id.equals("array.SetFloatArrayRegion.bounds")) {
            float[] a = new float[] {-1.25f, -1.25f, -1.25f};
            check(probe(76, a, 0));
            return;
        }
        // Jni.cpp:2326-2344
        if (id.equals("array.NewDoubleArray")) {
            double[] a = (double[])create(7);
            require(a.length == 3 && a[0] == 0 && a[1] == 0 && a[2] == 0);
            return;
        }
        // Jni.cpp:2355-2366
        if (id.equals("array.GetDoubleArrayElements")) {
            double[] a = new double[] {-7.503e100, -7.503e100, -7.503e100};
            check(probe(78, a, 0));
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseDoubleArrayElements")) {
            double[] a = new double[] {-7.503e100, -7.503e100, -7.503e100};
            check(probe(79, a, 0));
            require(a[0] == 3.125e110);
            return;
        }
        // Jni.cpp:2400-2412
        if (id.equals("array.GetDoubleArrayRegion")) {
            double[] a = new double[] {-7.503e100, -7.503e100, -7.503e100};
            check(probe(80, a, 0));
            return;
        }
        // Jni.cpp:2417-2429
        if (id.equals("array.SetDoubleArrayRegion")) {
            double[] a = new double[] {-7.503e100, -7.503e100, -7.503e100};
            check(probe(81, a, 0));
            require(a[0] == -7.503e100 && a[1] == 3.125e110 && a[2] == -7.503e100);
            return;
        }
        // Jni.cpp:2361-2365
        if (id.equals("array.GetDoubleArrayElements.dvm_isCopy")) {
            double[] a = new double[] {-7.503e100, -7.503e100, -7.503e100};
            check(probe(82, a, 0));
            return;
        }
        // Jni.cpp:2361-2366
        if (id.equals("array.GetDoubleArrayElements.dvm_alias")) {
            double[] a = new double[] {-7.503e100, -7.503e100, -7.503e100};
            check(probe(83, a, 0));
            try {
                require(a[0] == 3.125e110);
            } finally {
                check(probe(83, a, 3));
            };
            return;
        }
        // Jni.cpp:2376-2386
        if (id.equals("array.ReleaseDoubleArrayElements.dvm_abort")) {
            double[] a = new double[] {-7.503e100, -7.503e100, -7.503e100};
            check(probe(84, a, 0));
            check(probe(84, a, 1));
            require(a[0] == 3.125e110);
            return;
        }
        // Jni.cpp:2380-2385
        if (id.equals("array.ReleaseDoubleArrayElements.dvm_commit")) {
            double[] a = new double[] {-7.503e100, -7.503e100, -7.503e100};
            check(probe(85, a, 0));
            try {
                check(probe(85, a, 2));
                require(a[0] == -6.25e120);
            } finally {
                check(probe(85, a, 3));
            };
            return;
        }
        // Jni.cpp:2388-2394;2407-2410
        if (id.equals("array.GetDoubleArrayRegion.bounds")) {
            double[] a = new double[] {-7.503e100, -7.503e100, -7.503e100};
            check(probe(86, a, 0));
            return;
        }
        // Jni.cpp:2388-2394;2424-2427
        if (id.equals("array.SetDoubleArrayRegion.bounds")) {
            double[] a = new double[] {-7.503e100, -7.503e100, -7.503e100};
            check(probe(87, a, 0));
            return;
        }
        // Jni.cpp:2598-2617
        if (id.equals("array.GetPrimitiveArrayCritical")) {
            int[] a = {7, -23, 99};
            check(probe(88, a, 0));
            require(a[0] == 914 && a[1] == -23 && a[2] == 99);
            return;
        }
        // Jni.cpp:2598-2617
        if (id.equals("array.ReleasePrimitiveArrayCritical")) {
            int[] a = {7, -23, 99};
            check(probe(89, a, 0));
            require(a[0] == 914 && a[1] == -23 && a[2] == 99);
            return;
        }
        // Jni.cpp:2598-2617
        if (id.equals("array.GetPrimitiveArrayCritical.dvm_isCopy")) {
            int[] a = {7, -23, 99};
            check(probe(90, a, 0));
            require(a[0] == 914 && a[1] == -23 && a[2] == 99);
            return;
        }
        // Jni.cpp:2598-2617
        if (id.equals("array.GetPrimitiveArrayCritical.dvm_alias")) {
            int[] a = {7, -23, 99};
            check(probe(91, a, 0));
            require(a[0] == 914 && a[1] == -23 && a[2] == 99);
            return;
        }
        // Jni.cpp:2598-2617
        if (id.equals("array.ReleasePrimitiveArrayCritical.dvm_commit")) {
            int[] a = {7, -23, 99};
            check(probe(92, a, 0));
            require(a[0] == 4321 && a[1] == -23 && a[2] == 99);
            return;
        }
        // Jni.cpp:2271-2276;2285-2294
        if (id.equals("array.GetObjectArrayElement")) {
            check(probe(93, new String[] {"object-marker", null}, 0));
            return;
        }
        // Jni.cpp:2271-2276;2285-2294
        if (id.equals("array.GetObjectArrayElement.nullElement")) {
            check(probe(94, new String[] {"object-marker", null}, 0));
            return;
        }
        // Jni.cpp:2271-2276;2285-2294
        if (id.equals("array.GetObjectArrayElement.bounds")) {
            check(probe(95, new String[] {"object-marker", null}, 0));
            return;
        }
        throw new IllegalArgumentException(id);
    }
}

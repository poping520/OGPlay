/* SPDX-License-Identifier: Apache-2.0
 * Independent probes derived from Dalvik vm/Jni.cpp, Android 4.4.
 * Every oracle below cites the corresponding implementation, not HotSpot.
 */
package probe;
import static probe.NativeSupport.*;
final class FieldsCases implements CaseGroup {
    public boolean iBoolean;
    public byte iByte;
    public short iShort;
    public char iChar;
    public int iInt;
    public long iLong;
    public float iFloat;
    public double iDouble;
    public Object iObject;
    public static boolean sBoolean;
    public static byte sByte;
    public static short sShort;
    public static char sChar;
    public static int sInt;
    public static long sLong;
    public static float sFloat;
    public static double sDouble;
    public static Object sObject;
    private static native String probe(int test, Object obj, Object marker);
    public String[] ids() {
        return new String[] {"field.GetBooleanField",
                             "field.SetBooleanField",
                             "field.GetByteField",
                             "field.SetByteField",
                             "field.GetShortField",
                             "field.SetShortField",
                             "field.GetCharField",
                             "field.SetCharField",
                             "field.GetIntField",
                             "field.SetIntField",
                             "field.GetLongField",
                             "field.SetLongField",
                             "field.GetFloatField",
                             "field.SetFloatField",
                             "field.GetDoubleField",
                             "field.SetDoubleField",
                             "field.GetObjectField",
                             "field.SetObjectField",
                             "field.GetStaticBooleanField",
                             "field.SetStaticBooleanField",
                             "field.GetStaticByteField",
                             "field.SetStaticByteField",
                             "field.GetStaticShortField",
                             "field.SetStaticShortField",
                             "field.GetStaticCharField",
                             "field.SetStaticCharField",
                             "field.GetStaticLongField",
                             "field.SetStaticLongField",
                             "field.GetStaticFloatField",
                             "field.SetStaticFloatField",
                             "field.GetStaticDoubleField",
                             "field.SetStaticDoubleField",
                             "field.GetStaticObjectField",
                             "field.SetStaticObjectField",
                             "field.GetFieldID",
                             "field.GetFieldID.missing",
                             "field.GetFieldID.wrongSignature"};
    }
    public void run(String id) throws Throwable {
        // Jni.cpp:1829-1864
        if (id.equals("field.GetBooleanField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            f.iBoolean = true;
            check(probe(0, f, marker));
            return;
        }
        // Jni.cpp:1869-1902
        if (id.equals("field.SetBooleanField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            check(probe(1, f, marker));
            require(f.iBoolean == true);
            return;
        }
        // Jni.cpp:1829-1864
        if (id.equals("field.GetByteField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            f.iByte = (byte)-99;
            check(probe(2, f, marker));
            return;
        }
        // Jni.cpp:1869-1902
        if (id.equals("field.SetByteField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            check(probe(3, f, marker));
            require(f.iByte == (byte)-99);
            return;
        }
        // Jni.cpp:1829-1864
        if (id.equals("field.GetShortField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            f.iShort = (short)-23456;
            check(probe(4, f, marker));
            return;
        }
        // Jni.cpp:1869-1902
        if (id.equals("field.SetShortField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            check(probe(5, f, marker));
            require(f.iShort == (short)-23456);
            return;
        }
        // Jni.cpp:1829-1864
        if (id.equals("field.GetCharField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            f.iChar = (char)0xffff;
            check(probe(6, f, marker));
            return;
        }
        // Jni.cpp:1869-1902
        if (id.equals("field.SetCharField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            check(probe(7, f, marker));
            require(f.iChar == (char)0xffff);
            return;
        }
        // Jni.cpp:1829-1864
        if (id.equals("field.GetIntField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            f.iInt = 0x89abcdef;
            check(probe(8, f, marker));
            return;
        }
        // Jni.cpp:1869-1902
        if (id.equals("field.SetIntField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            check(probe(9, f, marker));
            require(f.iInt == 0x89abcdef);
            return;
        }
        // Jni.cpp:1829-1864
        if (id.equals("field.GetLongField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            f.iLong = 0x8123456789abcdefL;
            check(probe(10, f, marker));
            return;
        }
        // Jni.cpp:1869-1902
        if (id.equals("field.SetLongField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            check(probe(11, f, marker));
            require(f.iLong == 0x8123456789abcdefL);
            return;
        }
        // Jni.cpp:1829-1864
        if (id.equals("field.GetFloatField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            f.iFloat = Float.intBitsToFloat(0x7fc12345);
            check(probe(12, f, marker));
            return;
        }
        // Jni.cpp:1869-1902
        if (id.equals("field.SetFloatField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            check(probe(13, f, marker));
            require(Float.floatToRawIntBits(f.iFloat) == 0x7fc12345);
            return;
        }
        // Jni.cpp:1829-1864
        if (id.equals("field.GetDoubleField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            f.iDouble = Double.longBitsToDouble(0xc123456789abcdefL);
            check(probe(14, f, marker));
            return;
        }
        // Jni.cpp:1869-1902
        if (id.equals("field.SetDoubleField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            check(probe(15, f, marker));
            require(Double.doubleToRawLongBits(f.iDouble) == 0xc123456789abcdefL);
            return;
        }
        // Jni.cpp:1829-1864
        if (id.equals("field.GetObjectField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            f.iObject = marker;
            check(probe(16, f, marker));
            return;
        }
        // Jni.cpp:1869-1902
        if (id.equals("field.SetObjectField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            check(probe(17, f, marker));
            require(f.iObject == marker);
            return;
        }
        // Jni.cpp:1752-1786
        if (id.equals("field.GetStaticBooleanField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sBoolean = true;
            check(probe(18, f, marker));
            return;
        }
        // Jni.cpp:1791-1822
        if (id.equals("field.SetStaticBooleanField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sBoolean = false;
            check(probe(19, f, marker));
            require(FieldsCases.sBoolean == true);
            return;
        }
        // Jni.cpp:1752-1786
        if (id.equals("field.GetStaticByteField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sByte = (byte)-99;
            check(probe(20, f, marker));
            return;
        }
        // Jni.cpp:1791-1822
        if (id.equals("field.SetStaticByteField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sByte = 0;
            check(probe(21, f, marker));
            require(FieldsCases.sByte == (byte)-99);
            return;
        }
        // Jni.cpp:1752-1786
        if (id.equals("field.GetStaticShortField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sShort = (short)-23456;
            check(probe(22, f, marker));
            return;
        }
        // Jni.cpp:1791-1822
        if (id.equals("field.SetStaticShortField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sShort = 0;
            check(probe(23, f, marker));
            require(FieldsCases.sShort == (short)-23456);
            return;
        }
        // Jni.cpp:1752-1786
        if (id.equals("field.GetStaticCharField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sChar = (char)0xffff;
            check(probe(24, f, marker));
            return;
        }
        // Jni.cpp:1791-1822
        if (id.equals("field.SetStaticCharField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sChar = 0;
            check(probe(25, f, marker));
            require(FieldsCases.sChar == (char)0xffff);
            return;
        }
        // Jni.cpp:1752-1786
        if (id.equals("field.GetStaticLongField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sLong = 0x8123456789abcdefL;
            check(probe(26, f, marker));
            return;
        }
        // Jni.cpp:1791-1822
        if (id.equals("field.SetStaticLongField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sLong = 0;
            check(probe(27, f, marker));
            require(FieldsCases.sLong == 0x8123456789abcdefL);
            return;
        }
        // Jni.cpp:1752-1786
        if (id.equals("field.GetStaticFloatField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sFloat = Float.intBitsToFloat(0x7fc12345);
            check(probe(28, f, marker));
            return;
        }
        // Jni.cpp:1791-1822
        if (id.equals("field.SetStaticFloatField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sFloat = 0;
            check(probe(29, f, marker));
            require(Float.floatToRawIntBits(FieldsCases.sFloat) == 0x7fc12345);
            return;
        }
        // Jni.cpp:1752-1786
        if (id.equals("field.GetStaticDoubleField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sDouble = Double.longBitsToDouble(0xc123456789abcdefL);
            check(probe(30, f, marker));
            return;
        }
        // Jni.cpp:1791-1822
        if (id.equals("field.SetStaticDoubleField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sDouble = 0;
            check(probe(31, f, marker));
            require(Double.doubleToRawLongBits(FieldsCases.sDouble) == 0xc123456789abcdefL);
            return;
        }
        // Jni.cpp:1752-1786
        if (id.equals("field.GetStaticObjectField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sObject = marker;
            check(probe(32, f, marker));
            return;
        }
        // Jni.cpp:1791-1822
        if (id.equals("field.SetStaticObjectField")) {
            FieldsCases f = new FieldsCases();
            Object marker = new Object();
            FieldsCases.sObject = null;
            check(probe(33, f, marker));
            require(FieldsCases.sObject == marker);
            return;
        }
        // Jni.cpp:1674-1690
        if (id.equals("field.GetFieldID")) {
            check(probe(34, new FieldsCases(), null));
            return;
        }
        // Jni.cpp:1674-1690
        if (id.equals("field.GetFieldID.missing")) {
            check(probe(35, new FieldsCases(), null));
            return;
        }
        // Jni.cpp:1674-1690
        if (id.equals("field.GetFieldID.wrongSignature")) {
            check(probe(36, new FieldsCases(), null));
            return;
        }
        throw new IllegalArgumentException(id);
    }
}

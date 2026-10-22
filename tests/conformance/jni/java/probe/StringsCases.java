/* SPDX-License-Identifier: Apache-2.0
 * Independent probes derived from Dalvik vm/Jni.cpp, Android 4.4.
 * Every oracle below cites the corresponding implementation, not HotSpot.
 */
package probe;
import static probe.NativeSupport.*;
final class StringsCases implements CaseGroup {
    private static final String TEXT = "A\u0000\u07ff\u0800\ud83d\ude00Z";
    private static native String create(boolean empty);
    private static native String probe(int test, String value);

    public String[] ids() {
        return new String[] {"string.NewString",
                             "string.NewString.dvm_emptyNull",
                             "string.GetStringLength",
                             "string.GetStringUTFLength",
                             "string.GetStringUTFLength.dvm_null",
                             "string.GetStringChars",
                             "string.ReleaseStringChars",
                             "string.GetStringChars.dvm_isCopy",
                             "string.GetStringUTFChars.dvm_isCopy",
                             "string.GetStringUTFChars.dvm_null",
                             "string.GetStringRegion",
                             "string.GetStringUTFRegion",
                             "string.GetStringRegion.negativeStart",
                             "string.GetStringRegion.negativeLength",
                             "string.GetStringRegion.pastEnd",
                             "string.GetStringUTFRegion.negativeStart",
                             "string.GetStringUTFRegion.negativeLength",
                             "string.GetStringUTFRegion.pastEnd",
                             "string.GetStringCritical",
                             "string.ReleaseStringCritical",
                             "string.GetStringCritical.dvm_isCopy"};
    }
    public void run(String id) throws Throwable {
        // Jni.cpp:2104-2111
        if (id.equals("string.NewString")) {
            require(TEXT.equals(create(false)));
            return;
        }
        // Jni.cpp:2101-2102
        if (id.equals("string.NewString.dvm_emptyNull")) {
            require("".equals(create(true)));
            return;
        }
        // Jni.cpp:2117-2120
        if (id.equals("string.GetStringLength")) {
            check(probe(2, TEXT));
            return;
        }
        // Jni.cpp:2176-2182
        if (id.equals("string.GetStringUTFLength")) {
            check(probe(3, TEXT));
            return;
        }
        // Jni.cpp:2178-2180
        if (id.equals("string.GetStringUTFLength.dvm_null")) {
            check(probe(4, TEXT));
            return;
        }
        // Jni.cpp:2130-2152
        if (id.equals("string.GetStringChars")) {
            check(probe(5, TEXT));
            return;
        }
        // Jni.cpp:2130-2152
        if (id.equals("string.ReleaseStringChars")) {
            check(probe(6, TEXT));
            return;
        }
        // Jni.cpp:2130-2142
        if (id.equals("string.GetStringChars.dvm_isCopy")) {
            check(probe(7, TEXT));
            return;
        }
        // Jni.cpp:2199-2214
        if (id.equals("string.GetStringUTFChars.dvm_isCopy")) {
            check(probe(8, TEXT));
            return;
        }
        // Jni.cpp:2201-2203
        if (id.equals("string.GetStringUTFChars.dvm_null")) {
            check(probe(9, TEXT));
            return;
        }
        // Jni.cpp:2564-2572
        if (id.equals("string.GetStringRegion")) {
            check(probe(10, TEXT));
            return;
        }
        // Jni.cpp:2579-2587
        if (id.equals("string.GetStringUTFRegion")) {
            check(probe(11, TEXT));
            return;
        }
        // Jni.cpp:2568-2570;2583-2585
        if (id.equals("string.GetStringRegion.negativeStart")) {
            check(probe(12, TEXT));
            return;
        }
        // Jni.cpp:2568-2570;2583-2585
        if (id.equals("string.GetStringRegion.negativeLength")) {
            check(probe(13, TEXT));
            return;
        }
        // Jni.cpp:2568-2570;2583-2585
        if (id.equals("string.GetStringRegion.pastEnd")) {
            check(probe(14, TEXT));
            return;
        }
        // Jni.cpp:2568-2570;2583-2585
        if (id.equals("string.GetStringUTFRegion.negativeStart")) {
            check(probe(15, TEXT));
            return;
        }
        // Jni.cpp:2568-2570;2583-2585
        if (id.equals("string.GetStringUTFRegion.negativeLength")) {
            check(probe(16, TEXT));
            return;
        }
        // Jni.cpp:2568-2570;2583-2585
        if (id.equals("string.GetStringUTFRegion.pastEnd")) {
            check(probe(17, TEXT));
            return;
        }
        // Jni.cpp:2623-2645
        if (id.equals("string.GetStringCritical")) {
            check(probe(18, TEXT));
            return;
        }
        // Jni.cpp:2623-2645
        if (id.equals("string.ReleaseStringCritical")) {
            check(probe(19, TEXT));
            return;
        }
        // Jni.cpp:2623-2645
        if (id.equals("string.GetStringCritical.dvm_isCopy")) {
            check(probe(20, TEXT));
            return;
        }
        throw new IllegalArgumentException(id);
    }
}

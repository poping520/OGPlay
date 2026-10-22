/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */
package probe;
import static probe.NativeSupport.*;
final class LibraryCases implements CaseGroup {
    private static native int loadCount();
    private static native int loadCount14();
    private static native int shortExport();
    private static native int overloaded(int v);
    private static native long overloaded(long v);
    public String[] ids() {
        return new String[] {"library.JNI_OnLoad14",     "library.JNI_OnLoad",
                             "library.loadLibraryTwice", "library.missing",
                             "library.shortExport",      "library.longExport"};
    }
    public void run(String id) {
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
}

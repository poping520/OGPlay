/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */
package probe;
import static probe.NativeSupport.*;
final class VersionCases implements CaseGroup {
    public String[] ids() { return new String[] {"version.GetVersion", "version.DefineClass"}; }
    public void run(String id) {
        if (id.equals("version.GetVersion")) {
            checkNative(id);
            return;
        }
        if (id.equals("version.DefineClass")) {
            checkNative(id);
            return;
        }
        throw new IllegalArgumentException(id);
    }
}

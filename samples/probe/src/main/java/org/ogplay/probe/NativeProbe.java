package org.ogplay.probe;

final class NativeProbe {
    private static boolean loaded;

    static synchronized void load() {
        if (!loaded) {
            System.loadLibrary("ogprobe");
            loaded = true;
        }
    }

    static native int ping();
    static native long cpuCount(boolean online);
    static native String machine();
    static native long[] clocks();
    static native long[] processIds();
    static native long[] threads();
    static native long[] condWait();
    static native long[] sleep();
    static native long[] memory();
    static native long[] maps(String nativeLibraryDir);
    static native long[] readCpuInfo();
    static native String environmentValue();
    static native int jump();
    static native long[] cppRuntime(boolean chooseMarker);
    static native long[] dynamicLibraries();
    static native long[] tryFork();
    static native long[] asset(android.content.res.AssetManager manager);
    static native long[] looper();
    static native long[] openSl();
    static native void abortProcess();
}

package probe;
final class NativeSupport {
    private static native String runNative(String id);
    static void require(boolean b) {
        if (!b)
            throw new AssertionError("value mismatch");
    }
    static void check(String why) {
        if (why != null)
            throw new AssertionError(why);
    }
    static void checkNative(String id) { check(runNative(id)); }
}

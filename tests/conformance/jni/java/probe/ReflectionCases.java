/* SPDX-License-Identifier: Apache-2.0
 * Standalone Dalvik JNI probes. Oracles cite vm/Jni.cpp (Android 4.4).
 */
package probe;
import static probe.NativeSupport.*;
final class ReflectionCases implements CaseGroup {
    public static class Value {
        public int number;
        public static int shared;
        public Value(int value) { number = value; }
        public int add(int x) { return number + x; }
        public static int twice(int x) { return x * 2; }
    }
    private static native Object to(int mode, Class type);
    private static native Object fromConstructor(Object reflected, Class type);
    private static native String from(int mode, Object reflected, Object obj, Class type);

    public String[] ids() {
        return new String[] {"reflection.FromReflectedMethod.instance",
                             "reflection.FromReflectedMethod.static",
                             "reflection.FromReflectedMethod.constructor",
                             "reflection.ToReflectedMethod.instance",
                             "reflection.ToReflectedMethod.static",
                             "reflection.ToReflectedMethod.constructor",
                             "reflection.FromReflectedField.instance",
                             "reflection.ToReflectedField.instance",
                             "reflection.FromReflectedField.static",
                             "reflection.ToReflectedField.static"};
    }
    public void run(String id) throws Throwable {
        // Jni.cpp:1285-1288
        if (id.equals("reflection.FromReflectedMethod.instance")) {
            Value v = new Value(7);
            check(from(0, Value.class.getMethod("add", int.class), v, Value.class));
            return;
        }
        // Jni.cpp:1285-1288
        if (id.equals("reflection.FromReflectedMethod.static")) {
            Value v = new Value(7);
            check(from(1, Value.class.getMethod("twice", int.class), v, Value.class));
            return;
        }
        // Jni.cpp:1285-1288;1565-1578
        if (id.equals("reflection.FromReflectedMethod.constructor")) {
            Value v = (Value)fromConstructor(Value.class.getConstructor(int.class), Value.class);
            require(v.number == 73);
            return;
        }
        // Jni.cpp:1307-1312
        if (id.equals("reflection.ToReflectedMethod.instance")) {
            java.lang.reflect.Method m = (java.lang.reflect.Method)to(0, Value.class);
            require(java.lang.reflect.Modifier.isStatic(m.getModifiers()) == false);
            require(((Integer)m.invoke(new Value(7), 9)).intValue() == 16);
            return;
        }
        // Jni.cpp:1307-1312
        if (id.equals("reflection.ToReflectedMethod.static")) {
            java.lang.reflect.Method m = (java.lang.reflect.Method)to(1, Value.class);
            require(java.lang.reflect.Modifier.isStatic(m.getModifiers()) == true);
            require(((Integer)m.invoke(null, 9)).intValue() == 18);
            return;
        }
        // Jni.cpp:1307-1312
        if (id.equals("reflection.ToReflectedMethod.constructor")) {
            java.lang.reflect.Constructor c = (java.lang.reflect.Constructor)to(2, Value.class);
            require(((Value)c.newInstance(73)).number == 73 &&
                    !java.lang.reflect.Modifier.isStatic(c.getModifiers()));
            return;
        }
        // Jni.cpp:1294-1297
        if (id.equals("reflection.FromReflectedField.instance")) {
            Value v = new Value(7);
            Value.shared = 11;
            check(from(2, Value.class.getField("number"), v, Value.class));
            require(v.number == 41);
            return;
        }
        // Jni.cpp:1322-1327
        if (id.equals("reflection.ToReflectedField.instance")) {
            Value v = new Value(7);
            Value.shared = 11;
            java.lang.reflect.Field f = (java.lang.reflect.Field)to(3, Value.class);
            require(java.lang.reflect.Modifier.isStatic(f.getModifiers()) == false);
            require(f.getInt(v) == 7);
            f.setInt(v, 41);
            require(v.number == 41);
            return;
        }
        // Jni.cpp:1294-1297
        if (id.equals("reflection.FromReflectedField.static")) {
            Value v = new Value(7);
            Value.shared = 11;
            check(from(3, Value.class.getField("shared"), v, Value.class));
            require(Value.shared == 41);
            return;
        }
        // Jni.cpp:1322-1327
        if (id.equals("reflection.ToReflectedField.static")) {
            Value v = new Value(7);
            Value.shared = 11;
            java.lang.reflect.Field f = (java.lang.reflect.Field)to(4, Value.class);
            require(java.lang.reflect.Modifier.isStatic(f.getModifiers()) == true);
            require(f.getInt(v) == 11);
            f.setInt(v, 41);
            require(Value.shared == 41);
            return;
        }
        throw new IllegalArgumentException(id);
    }
}

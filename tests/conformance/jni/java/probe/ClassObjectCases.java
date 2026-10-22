/* SPDX-License-Identifier: Apache-2.0
 * Independent probes derived from Dalvik vm/Jni.cpp, Android 4.4.
 * Every oracle below cites the corresponding implementation, not HotSpot.
 */
package probe;
import static probe.NativeSupport.*;
final class ClassObjectCases implements CaseGroup {
    interface Tag {}
    static abstract class AbstractValue {}
    static class Value {
        static int calls;
        int mark = 99;
        long wide;
        double fraction;
        String text;
        public Value() { calls++; }
        public Value(long wide, double fraction, String text, int mark) {
            calls++;
            this.wide = wide;
            this.fraction = fraction;
            this.text = text;
            this.mark = mark;
        }
    }
    static class Sub extends Value implements Tag {}
    private static native Class superclass(Class type);
    private static native boolean assignable(Class a, Class b);
    private static native boolean instance(Object obj, Class type);
    private static native Object construct(Class type, int mode) throws InstantiationException;

    public String[] ids() {
        return new String[] {"class_object.GetSuperclass",
                             "class_object.GetSuperclass.root",
                             "class_object.IsAssignableFrom",
                             "class_object.IsInstanceOf",
                             "class_object.IsInstanceOf.dvm_null",
                             "class_object.AllocObject",
                             "class_object.AllocObject.dvm_abstract",
                             "class_object.AllocObject.dvm_interface",
                             "class_object.NewObject",
                             "class_object.NewObjectV"};
    }
    public void run(String id) throws Throwable {
        // Jni.cpp:1264-1267
        if (id.equals("class_object.GetSuperclass")) {
            require(superclass(Sub.class) == Value.class &&
                    superclass(Value.class) == Object.class);
            return;
        }
        // Jni.cpp:1264-1267
        if (id.equals("class_object.GetSuperclass.root")) {
            require(superclass(Object.class) == null);
            return;
        }
        // Jni.cpp:1275-1279
        if (id.equals("class_object.IsAssignableFrom")) {
            require(assignable(Sub.class, Value.class) && !assignable(Value.class, Sub.class) &&
                    assignable(Sub.class, Tag.class));
            return;
        }
        // Jni.cpp:1600-1610
        if (id.equals("class_object.IsInstanceOf")) {
            require(instance(new Sub(), Value.class) && instance(new Sub(), Tag.class) &&
                    !instance(new Value(), Sub.class));
            return;
        }
        // Jni.cpp:1603-1605
        if (id.equals("class_object.IsInstanceOf.dvm_null")) {
            require(instance(null, Value.class));
            return;
        }
        // Jni.cpp:1508-1520
        if (id.equals("class_object.AllocObject")) {
            Value.calls = 0;
            Value v = (Value)construct(Value.class, 0);
            require(v.getClass() == Value.class && v.mark == 0 && v.wide == 0 && v.text == null &&
                    Value.calls == 0);
            return;
        }
        // Jni.cpp:1022-1026;1512-1516
        if (id.equals("class_object.AllocObject.dvm_abstract")) {
            try {
                construct(AbstractValue.class, 0);
                throw new AssertionError("missing InstantiationException");
            } catch (InstantiationException expected) {
            };
            return;
        }
        // Jni.cpp:1022-1026;1512-1516
        if (id.equals("class_object.AllocObject.dvm_interface")) {
            try {
                construct(Tag.class, 0);
                throw new AssertionError("missing InstantiationException");
            } catch (InstantiationException expected) {
            };
            return;
        }
        // Jni.cpp:1526-1562
        if (id.equals("class_object.NewObject")) {
            Value.calls = 0;
            Value v = (Value)construct(Value.class, 1);
            require(v.mark == -73 && v.wide == -0x123456789abcdefL && v.fraction == -7.25 &&
                    "ctor".equals(v.text) && Value.calls == 1);
            return;
        }
        // Jni.cpp:1526-1562
        if (id.equals("class_object.NewObjectV")) {
            Value.calls = 0;
            Value v = (Value)construct(Value.class, 2);
            require(v.mark == -73 && v.wide == -0x123456789abcdefL && v.fraction == -7.25 &&
                    "ctor".equals(v.text) && Value.calls == 1);
            return;
        }
        throw new IllegalArgumentException(id);
    }
}

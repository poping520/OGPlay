#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/reflection_codec.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

struct ReflectionVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;

    explicit ReflectionVm(
        const std::string& fixture = "interp.dex",
        const InterpreterBackend backend = InterpreterBackend::switch_dispatch)
        : interpreter([this, &fixture]() -> DexClassLinker& {
              const auto catalog = CoreIntrinsicCatalog();
              linker.RegisterIntrinsics(catalog);
              linker.RegisterDex(ReadFixture(fixture));
              linker.Link();
              return linker;
          }(), model, nullptr, ledger, InterpreterConfig{.backend = backend}) {}

    [[nodiscard]] VmCallOutcome Virtual(
        const VmObjectRef receiver, const std::string_view name,
        const std::string_view descriptor,
        std::vector<VmValue> arguments = {}) {
        const auto actual = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(
            actual, std::string(name), std::string(descriptor));
        REQUIRE(index.has_value());
        const auto& linked = linker.Class(actual);
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return interpreter.Call(linked.vtable[*index], arguments);
    }

    [[nodiscard]] VmCallOutcome Static(
        const std::string_view owner, const std::string_view name,
        const std::string_view descriptor,
        std::vector<VmValue> arguments = {}) {
        const auto java_class = linker.ResolveDescriptor(owner);
        const auto method = linker.FindDirectMethod(
            java_class, std::string(name), std::string(descriptor));
        REQUIRE(method.has_value());
        return interpreter.Call(*method, arguments);
    }

    [[nodiscard]] VmObjectRef ClassArray(
        const std::vector<std::string_view>& descriptors) {
        const auto class_class = linker.ResolveDescriptor("Ljava/lang/Class;");
        const auto array_class =
            linker.ResolveDescriptor("[Ljava/lang/Class;");
        const auto array = model.NewObjectArray(
            array_class, class_class,
            static_cast<JniSize>(descriptors.size()));
        for (std::size_t index = 0; index < descriptors.size(); ++index) {
            model.SetObjectElement(
                array, static_cast<JniSize>(index),
                model.ClassObject(linker.ResolveDescriptor(descriptors[index])));
        }
        return array;
    }

    [[nodiscard]] VmObjectRef ObjectArray(
        const std::vector<VmObjectRef>& values) {
        const auto object_class = linker.ResolveDescriptor("Ljava/lang/Object;");
        const auto array_class =
            linker.ResolveDescriptor("[Ljava/lang/Object;");
        const auto array = model.NewObjectArray(
            array_class, object_class, static_cast<JniSize>(values.size()));
        for (std::size_t index = 0; index < values.size(); ++index) {
            model.SetObjectElement(array, static_cast<JniSize>(index),
                                   values[index]);
        }
        return array;
    }
};

VmObjectRef Ref(const VmCallOutcome& outcome) {
    REQUIRE_FALSE(outcome.exception.IsValid());
    REQUIRE(outcome.value.kind == VmValue::Kind::ref);
    return outcome.value.ref;
}

std::int32_t Int(const VmCallOutcome& outcome) {
    REQUIRE_FALSE(outcome.exception.IsValid());
    return outcome.value.AsInt();
}

void ExpectException(ReflectionVm& vm, const VmCallOutcome& outcome,
                     const std::string_view descriptor) {
    REQUIRE(outcome.exception.IsValid());
    REQUIRE(outcome.exception_class.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor == descriptor);
}

std::vector<std::string> MemberNames(ReflectionVm& vm,
                                     const VmObjectRef array) {
    std::vector<std::string> result;
    for (JniSize index = 0; index < vm.model.ArrayLength(array); ++index) {
        result.push_back(vm.interpreter.StringUtf8(Ref(vm.Virtual(
            vm.model.GetObjectElement(array, index), "getName",
            "()Ljava/lang/String;"))));
    }
    return result;
}

VmObjectRef MethodWrapper(ReflectionVm& vm, const std::string_view owner,
                          const std::string_view name,
                          const std::vector<std::string_view>& parameters = {}) {
    std::vector<DexClassId> parameter_types;
    for (const auto parameter : parameters) {
        parameter_types.push_back(vm.linker.ResolveDescriptor(parameter));
    }
    const auto meta = vm.interpreter.Reflection().FindDeclaredMethod(
        vm.linker.ResolveDescriptor(owner), name, parameter_types);
    REQUIRE_MESSAGE(meta.has_value(), "missing reflected method ", owner,
                    "->", name);
    return vm.interpreter.Reflection().MaterializeMethod(*meta);
}

VmObjectRef ConstructorWrapper(
    ReflectionVm& vm, const std::string_view owner,
    const std::vector<std::string_view>& parameters = {}) {
    std::vector<DexClassId> parameter_types;
    for (const auto parameter : parameters) {
        parameter_types.push_back(vm.linker.ResolveDescriptor(parameter));
    }
    const auto meta = vm.interpreter.Reflection().FindConstructor(
        vm.linker.ResolveDescriptor(owner), parameter_types, false);
    REQUIRE(meta.has_value());
    return vm.interpreter.Reflection().MaterializeConstructor(*meta);
}

VmObjectRef FieldWrapper(ReflectionVm& vm, const std::string_view owner,
                         const std::string_view name) {
    const auto meta = vm.interpreter.Reflection().FindDeclaredField(
        vm.linker.ResolveDescriptor(owner), name);
    REQUIRE_MESSAGE(meta.has_value(), "missing reflected field ", owner,
                    "->", name);
    return vm.interpreter.Reflection().MaterializeField(*meta);
}

VmObjectRef Box(ReflectionVm& vm, const std::string_view primitive,
                const VmValue value) {
    return vm.interpreter.Reflection().Codec().BoxReturn(
        vm.linker.ResolveDescriptor(primitive), value);
}

VmCallOutcome Invoke(ReflectionVm& vm, const VmObjectRef method,
                     const VmObjectRef receiver,
                     const std::vector<VmObjectRef>& arguments = {}) {
    const auto array = arguments.empty() ? VmObjectRef{}
                                         : vm.ObjectArray(arguments);
    return vm.Virtual(
        method, "invoke",
        "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;",
        {VmValue::Ref(receiver), VmValue::Ref(array)});
}

VmCallOutcome InvokeFrom(ReflectionVm& vm, const std::string_view caller,
                         const VmObjectRef method,
                         const VmObjectRef receiver) {
    const auto caller_class = vm.linker.ResolveDescriptor(caller);
    const auto driver = vm.linker.FindDirectMethod(
        caller_class, "invokeNoArgs",
        "(Ljava/lang/reflect/Method;Ljava/lang/Object;)Ljava/lang/Object;");
    REQUIRE(driver.has_value());
    const std::array arguments{VmValue::Ref(method), VmValue::Ref(receiver)};
    return vm.interpreter.Call(*driver, arguments);
}

VmCallOutcome Construct(ReflectionVm& vm, const VmObjectRef constructor,
                        const std::vector<VmObjectRef>& arguments = {}) {
    const auto array = arguments.empty() ? VmObjectRef{}
                                         : vm.ObjectArray(arguments);
    return vm.Virtual(
        constructor, "newInstance",
        "([Ljava/lang/Object;)Ljava/lang/Object;", {VmValue::Ref(array)});
}

}  // namespace

TEST_CASE("reflection metadata uses declared order and opaque local slots") {
    ReflectionVm vm;
    const auto counter = vm.linker.FindClass("LCounter;");
    REQUIRE(counter.has_value());

    // AOSP API19: .local/asop/dalvik/vm/reflect/Reflect.cpp ::
    // dvmCreateReflectMethodObject / createFieldObject
    const auto methods = vm.interpreter.Reflection().DeclaredMethods(*counter);
    REQUIRE(methods.size() == 3);
    CHECK(vm.linker.Method(methods[0].method).name == "describe");
    CHECK(vm.linker.Method(methods[1].method).name == "get");
    CHECK(vm.linker.Method(methods[2].method).name == "increment");
    CHECK(methods[0].slot == 0);
    CHECK(methods[1].slot == 1);
    CHECK(methods[2].slot == 2);
    CHECK(methods[0].parameter_types.empty());
    CHECK(vm.linker.Class(methods[1].return_type).descriptor == "I");
    CHECK(methods[0].slot != methods[0].method.Value());

    const auto constructors =
        vm.interpreter.Reflection().DeclaredConstructors(*counter);
    REQUIRE(constructors.size() == 1);
    CHECK(constructors[0].slot == 0);
    REQUIRE(constructors[0].parameter_types.size() == 1);
    CHECK(vm.linker.Class(constructors[0].parameter_types[0]).descriptor ==
          "I");

    const auto fields = vm.interpreter.Reflection().DeclaredFields(*counter);
    REQUIRE(fields.size() == 2);
    CHECK(vm.linker.Field(fields[0].field).name == "seed");
    CHECK(vm.linker.Field(fields[1].field).name == "value");
    CHECK(fields[0].slot == 0);
    CHECK(fields[1].slot == 1);
    CHECK(vm.linker.Class(fields[0].type).descriptor == "I");

    const auto method_class =
        vm.linker.FindClass("Ljava/lang/reflect/Method;");
    REQUIRE(method_class.has_value());
    const auto wrapper_methods =
        vm.interpreter.Reflection().DeclaredMethods(*method_class);
    const auto get_name = std::find_if(
        wrapper_methods.begin(), wrapper_methods.end(), [&](const auto& item) {
            return vm.linker.Method(item.method).name == "getName";
        });
    REQUIRE(get_name != wrapper_methods.end());
    CHECK(get_name->access_flags == 0x0001U);
}

TEST_CASE("reflection Method wrappers are fresh semantic copies") {
    ReflectionVm vm;
    const auto counter = vm.linker.FindClass("LCounter;");
    REQUIRE(counter.has_value());
    const auto class_object = vm.model.ClassObject(*counter);
    const auto query = [&] {
        return Ref(vm.Virtual(class_object, "getDeclaredMethods",
                              "()[Ljava/lang/reflect/Method;"));
    };

    // AOSP API19: .local/asop/libcore/libdvm/src/main/java/
    // java/lang/reflect/Method.java :: equals/getParameterTypes
    const auto first_array = query();
    const auto second_array = query();
    CHECK(first_array != second_array);
    const auto first = vm.model.GetObjectElement(first_array, 0);
    const auto second = vm.model.GetObjectElement(second_array, 0);
    CHECK(first != second);
    CHECK(vm.model.IdentityHashCode(first) !=
          vm.model.IdentityHashCode(second));
    CHECK(Int(vm.Virtual(first, "equals", "(Ljava/lang/Object;)Z",
                         {VmValue::Ref(second)})) == 1);
    CHECK(Int(vm.Virtual(first, "hashCode", "()I")) ==
          Int(vm.Virtual(second, "hashCode", "()I")));
    CHECK(vm.interpreter.StringUtf8(Ref(vm.Virtual(
              first, "getName", "()Ljava/lang/String;"))) == "describe");
    CHECK(Ref(vm.Virtual(first, "getDeclaringClass",
                         "()Ljava/lang/Class;")) == class_object);
    CHECK(Int(vm.Virtual(first, "getModifiers", "()I")) == 0x0001);

    const auto parameters_one = Ref(vm.Virtual(
        first, "getParameterTypes", "()[Ljava/lang/Class;"));
    const auto parameters_two = Ref(vm.Virtual(
        first, "getParameterTypes", "()[Ljava/lang/Class;"));
    CHECK(parameters_one != parameters_two);
    CHECK(vm.model.ArrayLength(parameters_one) == 0);
    const auto exceptions_one = Ref(vm.Virtual(
        first, "getExceptionTypes", "()[Ljava/lang/Class;"));
    const auto exceptions_two = Ref(vm.Virtual(
        first, "getExceptionTypes", "()[Ljava/lang/Class;"));
    CHECK(exceptions_one != exceptions_two);
    CHECK(vm.model.ArrayLength(exceptions_one) == 0);

    CHECK(Int(vm.Virtual(first, "isAccessible", "()Z")) == 0);
    const auto set = vm.Virtual(first, "setAccessible", "(Z)V",
                                {VmValue::Int(1)});
    REQUIRE_FALSE(set.exception.IsValid());
    CHECK(Int(vm.Virtual(first, "isAccessible", "()Z")) == 1);
    CHECK(Int(vm.Virtual(second, "isAccessible", "()Z")) == 0);
}

TEST_CASE("reflection factory materializes Constructor and Field wrappers") {
    ReflectionVm vm;
    const auto counter = vm.linker.FindClass("LCounter;");
    REQUIRE(counter.has_value());
    const auto constructors =
        vm.interpreter.Reflection().DeclaredConstructors(*counter);
    const auto fields = vm.interpreter.Reflection().DeclaredFields(*counter);
    REQUIRE(constructors.size() == 1);
    REQUIRE(fields.size() == 2);

    const auto constructor_one =
        vm.interpreter.Reflection().MaterializeConstructor(constructors[0]);
    const auto constructor_two =
        vm.interpreter.Reflection().MaterializeConstructor(constructors[0]);
    CHECK(constructor_one != constructor_two);
    CHECK(vm.interpreter.Reflection().SemanticallyEqual(
        constructor_one, constructor_two));
    CHECK(vm.interpreter.StringUtf8(Ref(vm.Virtual(
              constructor_one, "getName", "()Ljava/lang/String;"))) ==
          "Counter");
    CHECK(vm.model.ArrayLength(Ref(vm.Virtual(
              constructor_one, "getParameterTypes",
              "()[Ljava/lang/Class;"))) == 1);

    const auto field_one =
        vm.interpreter.Reflection().MaterializeField(fields[0]);
    const auto field_two =
        vm.interpreter.Reflection().MaterializeField(fields[0]);
    CHECK(field_one != field_two);
    CHECK(vm.interpreter.Reflection().SemanticallyEqual(field_one, field_two));
    CHECK(vm.interpreter.StringUtf8(Ref(vm.Virtual(
              field_one, "getName", "()Ljava/lang/String;"))) == "seed");
    CHECK(vm.linker.Class(vm.model.ClassOfClassObject(Ref(vm.Virtual(
              field_one, "getType", "()Ljava/lang/Class;")))).descriptor ==
          "I");
    CHECK(Int(vm.Virtual(field_one, "getModifiers", "()I")) == 0x0009);
}

TEST_CASE("reflection metadata cache does not retain guest wrappers") {
    ReflectionVm vm;
    const auto counter = vm.linker.FindClass("LCounter;");
    REQUIRE(counter.has_value());
    const auto methods = vm.interpreter.Reflection().DeclaredMethods(*counter);
    REQUIRE_FALSE(methods.empty());
    const auto wrapper =
        vm.interpreter.Reflection().MaterializeMethod(methods[0]);
    REQUIRE(vm.model.IsValidRef(wrapper));

    // AOSP API19 wrapper objects are ordinary heap objects. The immutable
    // host metadata cache must not become a hidden guest root.
    static_cast<void>(vm.interpreter.CollectGarbage("reflection_wrapper"));
    CHECK_FALSE(vm.model.IsValidRef(wrapper));

    const auto replacement =
        vm.interpreter.Reflection().MaterializeMethod(methods[0]);
    CHECK(vm.model.IsValidRef(replacement));
    CHECK(vm.interpreter.Reflection().MethodMetadata(replacement).method ==
          methods[0].method);
}

TEST_CASE("Class structural core follows API19 primitive array and hierarchy rules") {
    ReflectionVm vm("reflection.dex");
    const auto class_object = [&](const std::string_view descriptor) {
        return vm.model.ClassObject(vm.linker.ResolveDescriptor(descriptor));
    };
    const auto call = [&](const std::string_view descriptor,
                          const std::string_view name,
                          const std::string_view signature,
                          std::vector<VmValue> arguments = {}) {
        return vm.Virtual(class_object(descriptor), name, signature,
                          std::move(arguments));
    };

    // AOSP API19: .local/aosp/dalvik/vm/native/java_lang_Class.cpp ::
    // getComponentType/getInterfaces/getSuperclass/isAssignableFrom/isInstance
    CHECK(vm.interpreter.StringUtf8(Ref(call(
              "[[I", "getName", "()Ljava/lang/String;"))) == "[[I");
    CHECK(vm.interpreter.StringUtf8(Ref(call(
              "[[I", "getSimpleName", "()Ljava/lang/String;"))) ==
          "int[][]");
    CHECK(vm.interpreter.StringUtf8(Ref(call(
              "Lreflect/Derived;", "getSimpleName",
              "()Ljava/lang/String;"))) == "Derived");

    CHECK(Ref(call("[[I", "getComponentType", "()Ljava/lang/Class;")) ==
          class_object("[I"));
    CHECK_FALSE(Ref(call("I", "getComponentType",
                         "()Ljava/lang/Class;")).IsValid());
    CHECK(Ref(call("Lreflect/Derived;", "getSuperclass",
                   "()Ljava/lang/Class;")) == class_object("Lreflect/Base;"));
    CHECK(Ref(call("[I", "getSuperclass", "()Ljava/lang/Class;")) ==
          class_object("Ljava/lang/Object;"));
    CHECK_FALSE(Ref(call("Lreflect/ChildContract;", "getSuperclass",
                         "()Ljava/lang/Class;")).IsValid());
    CHECK_FALSE(Ref(call("V", "getSuperclass",
                         "()Ljava/lang/Class;")).IsValid());

    const auto direct = Ref(call("Lreflect/Derived;", "getInterfaces",
                                 "()[Ljava/lang/Class;"));
    REQUIRE(vm.model.ArrayLength(direct) == 1);
    CHECK(vm.model.GetObjectElement(direct, 0) ==
          class_object("Lreflect/ChildContract;"));
    const auto array_interfaces = Ref(call(
        "[I", "getInterfaces", "()[Ljava/lang/Class;"));
    REQUIRE(vm.model.ArrayLength(array_interfaces) == 2);
    CHECK(vm.model.GetObjectElement(array_interfaces, 0) ==
          class_object("Ljava/lang/Cloneable;"));
    CHECK(vm.model.GetObjectElement(array_interfaces, 1) ==
          class_object("Ljava/io/Serializable;"));

    CHECK(Int(call("I", "getModifiers", "()I")) == 0x0411);
    CHECK(Int(call("[I", "getModifiers", "()I")) == 0x0411);
    CHECK(Int(call("Lreflect/ChildContract;", "isInterface", "()Z")) == 1);
    CHECK(Int(call("[[I", "isArray", "()Z")) == 1);
    CHECK(Int(call("V", "isPrimitive", "()Z")) == 1);
    CHECK(Int(call("Lreflect/Derived;", "isSynthetic", "()Z")) == 0);

    const auto derived = vm.linker.ResolveDescriptor("Lreflect/Derived;");
    const auto instance = vm.model.NewInstance(
        derived, vm.linker.Class(derived).instance_slots);
    CHECK(Int(call("Lreflect/Base;", "isInstance", "(Ljava/lang/Object;)Z",
                   {VmValue::Ref(instance)})) == 1);
    CHECK(Int(call("Lreflect/Derived;", "isInstance", "(Ljava/lang/Object;)Z",
                   {VmValue::Ref(VmObjectRef{})})) == 0);
    CHECK(Int(call("Lreflect/Base;", "isAssignableFrom",
                   "(Ljava/lang/Class;)Z",
                   {VmValue::Ref(class_object("Lreflect/Derived;"))})) == 1);
    CHECK(Int(call("Lreflect/Derived;", "isAssignableFrom",
                   "(Ljava/lang/Class;)Z",
                   {VmValue::Ref(class_object("Lreflect/Base;"))})) == 0);
    ExpectException(vm, call("Lreflect/Base;", "isAssignableFrom",
                             "(Ljava/lang/Class;)Z",
                             {VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");

    CHECK(Ref(call("Lreflect/Base;", "cast",
                   "(Ljava/lang/Object;)Ljava/lang/Object;",
                   {VmValue::Ref(instance)})) == instance);
    ExpectException(vm, call("Lreflect/Derived;", "cast",
                             "(Ljava/lang/Object;)Ljava/lang/Object;",
                             {VmValue::Ref(vm.interpreter.NewIntrinsicInstance(
                                 "Ljava/lang/Object;"))}),
                    "Ljava/lang/ClassCastException;");
    CHECK(Ref(call("Lreflect/Derived;", "asSubclass",
                   "(Ljava/lang/Class;)Ljava/lang/Class;",
                   {VmValue::Ref(class_object("Lreflect/Base;"))})) ==
          class_object("Lreflect/Derived;"));
    CHECK(vm.interpreter.StringUtf8(Ref(call(
              "Lreflect/ChildContract;", "toString",
              "()Ljava/lang/String;"))) ==
          "interface reflect.ChildContract");
    CHECK(vm.interpreter.StringUtf8(Ref(call(
              "I", "toString", "()Ljava/lang/String;"))) == "int");
}

TEST_CASE("Class member queries separate declared and deterministic public aggregates") {
    ReflectionVm vm("reflection.dex");
    const auto derived = vm.linker.ResolveDescriptor("Lreflect/Derived;");
    const auto derived_class = vm.model.ClassObject(derived);
    const auto query = [&](const std::string_view name,
                           const std::string_view signature,
                           std::vector<VmValue> arguments = {}) {
        return vm.Virtual(derived_class, name, signature, std::move(arguments));
    };
    const auto string = [&](const std::string_view value) {
        return VmValue::Ref(vm.interpreter.NewStringUtf8(value));
    };

    // AOSP API19: .local/aosp/libcore/libdvm/src/main/java/java/lang/Class.java
    // :: getDeclaredMethods/getPublicMethodsRecursive/getPublicFieldsRecursive
    const auto declared_methods = MemberNames(
        vm, Ref(query("getDeclaredMethods", "()[Ljava/lang/reflect/Method;")));
    CHECK(declared_methods == std::vector<std::string>{
        "hiddenDerived", "acceptDouble", "acceptLong", "childContract",
        "chooseSecond", "common", "doNothing", "echo", "inheritedContract",
        "packageDerived", "protectedDerived", "throwTarget"});

    const auto public_methods = MemberNames(
        vm, Ref(query("getMethods", "()[Ljava/lang/reflect/Method;")));
    CHECK(std::count(public_methods.begin(), public_methods.end(), "common") == 1);
    CHECK(std::count(public_methods.begin(), public_methods.end(),
                     "inheritedContract") == 1);
    CHECK(std::find(public_methods.begin(), public_methods.end(),
                    "inheritedBase") != public_methods.end());
    CHECK(std::find(public_methods.begin(), public_methods.end(),
                    "hiddenDerived") == public_methods.end());

    CHECK(vm.model.ArrayLength(Ref(query(
              "getDeclaredConstructors",
              "()[Ljava/lang/reflect/Constructor;"))) == 2);
    CHECK(vm.model.ArrayLength(Ref(query(
              "getConstructors",
              "()[Ljava/lang/reflect/Constructor;"))) == 1);
    CHECK(MemberNames(vm, Ref(query(
              "getDeclaredFields", "()[Ljava/lang/reflect/Field;"))) ==
          std::vector<std::string>{
              "booleanField", "byteField", "charField", "derivedField",
              "doubleField", "finalField", "floatField", "hiddenDerived",
              "longField", "refField", "shortField", "volatileField"});
    CHECK(MemberNames(vm, Ref(query(
              "getFields", "()[Ljava/lang/reflect/Field;"))) ==
          std::vector<std::string>{
              "booleanField", "byteField", "charField", "derivedField",
              "doubleField", "finalField", "floatField", "longField",
              "refField", "shortField", "volatileField", "baseField",
              "contractField"});

    const auto hidden_method = Ref(query(
        "getDeclaredMethod",
        "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
        {string("hiddenDerived"), VmValue::Ref(VmObjectRef{})}));
    CHECK(vm.interpreter.StringUtf8(Ref(vm.Virtual(
              hidden_method, "getName", "()Ljava/lang/String;"))) ==
          "hiddenDerived");
    const auto inherited_method = Ref(query(
        "getMethod",
        "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
        {string("inheritedBase"), VmValue::Ref(VmObjectRef{})}));
    CHECK(Ref(vm.Virtual(inherited_method, "getDeclaringClass",
                         "()Ljava/lang/Class;")) ==
          vm.model.ClassObject(vm.linker.ResolveDescriptor("Lreflect/Base;")));
    ExpectException(vm, query(
        "getMethod",
        "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
        {string("hiddenDerived"), VmValue::Ref(VmObjectRef{})}),
        "Ljava/lang/NoSuchMethodException;");
    ExpectException(vm, query(
        "getDeclaredMethod",
        "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
        {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})}),
        "Ljava/lang/NullPointerException;");

    const auto int_parameters = vm.ClassArray({"I"});
    const auto private_constructor = Ref(query(
        "getDeclaredConstructor",
        "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;",
        {VmValue::Ref(int_parameters)}));
    CHECK(Int(vm.Virtual(private_constructor, "getModifiers", "()I")) ==
          0x0002);
    ExpectException(vm, query(
        "getConstructor",
        "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;",
        {VmValue::Ref(int_parameters)}), "Ljava/lang/NoSuchMethodException;");

    const auto hidden_field = Ref(query(
        "getDeclaredField",
        "(Ljava/lang/String;)Ljava/lang/reflect/Field;",
        {string("hiddenDerived")}));
    CHECK(Int(vm.Virtual(hidden_field, "getModifiers", "()I")) == 0x0002);
    const auto base_field = Ref(query(
        "getField", "(Ljava/lang/String;)Ljava/lang/reflect/Field;",
        {string("baseField")}));
    CHECK(Ref(vm.Virtual(base_field, "getDeclaringClass",
                         "()Ljava/lang/Class;")) ==
          vm.model.ClassObject(vm.linker.ResolveDescriptor("Lreflect/Base;")));
    ExpectException(vm, query(
        "getField", "(Ljava/lang/String;)Ljava/lang/reflect/Field;",
        {string("hiddenDerived")}), "Ljava/lang/NoSuchFieldException;");

    const auto int_class = vm.model.ClassObject(vm.linker.ResolveDescriptor("I"));
    CHECK(vm.model.ArrayLength(Ref(vm.Virtual(
              int_class, "getDeclaredMethods",
              "()[Ljava/lang/reflect/Method;"))) == 0);
    const auto array_class = vm.model.ClassObject(
        vm.linker.ResolveDescriptor("[Lreflect/Derived;"));
    CHECK(vm.model.ArrayLength(Ref(vm.Virtual(
              array_class, "getDeclaredFields",
              "()[Ljava/lang/reflect/Field;"))) == 0);
}

TEST_CASE("ReflectionCodec enforces the API19 primitive widening matrix") {
    ReflectionVm vm("reflection.dex");
    auto& codec = vm.interpreter.Reflection().Codec();
    struct Source final {
        std::string_view descriptor;
        VmValue value;
        std::string_view allowed_targets;
    };
    const std::vector<Source> sources{
        {"Z", VmValue::Int(1), "Z"},
        {"B", VmValue::Int(7), "BSIJFD"},
        {"S", VmValue::Int(7), "SIJFD"},
        {"C", VmValue::Int(7), "CIJFD"},
        {"I", VmValue::Int(7), "IJFD"},
        {"J", VmValue::Long(7), "JFD"},
        {"F", VmValue::Float(7.0F), "FD"},
        {"D", VmValue::Double(7.0), "D"},
    };
    constexpr std::string_view targets = "ZBCSIJFD";

    // AOSP API19: .local/aosp/dalvik/vm/reflect/Reflect.cpp ::
    // dvmConvertArgument/dvmConvertPrimitiveValue/dvmBoxPrimitive.
    for (const auto& source : sources) {
        const auto boxed = Box(vm, source.descriptor, source.value);
        for (const auto target : targets) {
            const auto target_text = std::string(1, target);
            if (source.allowed_targets.find(target) == std::string_view::npos) {
                try {
                    static_cast<void>(codec.ConvertArgument(
                        boxed, vm.linker.ResolveDescriptor(target_text)));
                    FAIL_CHECK("narrowing or boolean/numeric conversion passed");
                } catch (const VmJavaThrow& thrown) {
                    CHECK(thrown.descriptor ==
                          "Ljava/lang/IllegalArgumentException;");
                }
                continue;
            }
            const auto converted = codec.ConvertArgument(
                boxed, vm.linker.ResolveDescriptor(target_text));
            switch (target) {
                case 'J': CHECK(converted.AsLong() == 7); break;
                case 'F': CHECK(converted.AsFloat() == doctest::Approx(7.0F)); break;
                case 'D': CHECK(converted.AsDouble() == doctest::Approx(7.0)); break;
                default:
                    CHECK_UNARY(converted.AsInt() == 7 || target == 'Z');
                    break;
            }
        }
    }

    const auto derived = vm.linker.ResolveDescriptor("Lreflect/Derived;");
    const auto derived_object = vm.model.NewInstance(
        derived, vm.linker.Class(derived).instance_slots);
    CHECK(codec.ConvertArgument(
              derived_object,
              vm.linker.ResolveDescriptor("Lreflect/Base;")).ref ==
          derived_object);
    CHECK_FALSE(codec.ConvertArgument(
                    VmObjectRef{},
                    vm.linker.ResolveDescriptor("Ljava/lang/Object;"))
                    .ref.IsValid());
    CHECK(codec.BoxReturn(vm.linker.ResolveDescriptor("V"), VmValue::Void()) ==
          VmObjectRef{});
    CHECK(codec.BoxReturn(vm.linker.ResolveDescriptor("Ljava/lang/Object;"),
                          VmValue::Ref(derived_object)) == derived_object);
}

TEST_CASE("Method invoke dispatches and boxes identically on both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        ReflectionVm vm("reflection.dex", backend);
        const auto derived_class =
            vm.linker.ResolveDescriptor("Lreflect/Derived;");
        const auto derived = vm.model.NewInstance(
            derived_class, vm.linker.Class(derived_class).instance_slots);
        const auto integer = vm.linker.ResolveDescriptor("I");
        const auto long_type = vm.linker.ResolveDescriptor("J");
        const auto double_type = vm.linker.ResolveDescriptor("D");

        // Virtual and interface wrappers must dispatch through the receiver.
        const auto common = MethodWrapper(
            vm, "Lreflect/Base;", "common");
        CHECK(vm.interpreter.Reflection().Codec()
                  .ConvertArgument(Ref(Invoke(vm, common, derived)), integer)
                  .AsInt() == 7);
        const auto interface_method = MethodWrapper(
            vm, "Lreflect/ChildContract;", "childContract");
        CHECK(vm.interpreter.Reflection().Codec()
                  .ConvertArgument(
                      Ref(Invoke(vm, interface_method, derived)), integer)
                  .AsInt() == 8);

        const auto accept_long = MethodWrapper(
            vm, "Lreflect/Derived;", "acceptLong", {"J"});
        CHECK(vm.interpreter.Reflection().Codec()
                  .ConvertArgument(
                      Ref(Invoke(vm, accept_long, derived,
                                 {Box(vm, "B", VmValue::Int(11))})),
                      long_type)
                  .AsLong() == 11);
        const auto accept_double = MethodWrapper(
            vm, "Lreflect/Derived;", "acceptDouble", {"D"});
        CHECK(vm.interpreter.Reflection().Codec()
                  .ConvertArgument(
                      Ref(Invoke(vm, accept_double, derived,
                                 {Box(vm, "F", VmValue::Float(2.5F))})),
                      double_type)
                  .AsDouble() == doctest::Approx(2.5));
        const auto choose_second = MethodWrapper(
            vm, "Lreflect/Derived;", "chooseSecond", {"I", "J"});
        CHECK(vm.interpreter.Reflection().Codec()
                  .ConvertArgument(
                      Ref(Invoke(vm, choose_second, derived,
                                 {Box(vm, "B", VmValue::Int(3)),
                                  Box(vm, "I", VmValue::Int(19))})),
                      long_type)
                  .AsLong() == 19);

        const auto marker = vm.interpreter.NewIntrinsicInstance(
            "Ljava/lang/Object;");
        const auto echo = MethodWrapper(
            vm, "Lreflect/Derived;", "echo", {"Ljava/lang/Object;"});
        CHECK(Ref(Invoke(vm, echo, derived, {marker})) == marker);
        const auto do_nothing = MethodWrapper(
            vm, "Lreflect/Derived;", "doNothing");
        CHECK_FALSE(Ref(Invoke(vm, do_nothing, derived)).IsValid());

        const auto read = MethodWrapper(
            vm, "Lreflect/InvokeStatics;", "readInitialized");
        CHECK(vm.interpreter.Reflection().Codec()
                  .ConvertArgument(Ref(Invoke(vm, read, marker)), integer)
                  .AsInt() == 42);

        ExpectException(vm, Invoke(vm, common, VmObjectRef{}),
                        "Ljava/lang/NullPointerException;");
        ExpectException(vm, Invoke(vm, common, marker),
                        "Ljava/lang/IllegalArgumentException;");
        ExpectException(vm, Invoke(vm, accept_long, derived),
                        "Ljava/lang/IllegalArgumentException;");
        ExpectException(vm, Invoke(vm, accept_long, derived, {marker}),
                        "Ljava/lang/IllegalArgumentException;");

        const auto target = vm.interpreter.MakeThrowable(
            "Ljava/lang/RuntimeException;", "target");
        const auto throwing = MethodWrapper(
            vm, "Lreflect/Derived;", "throwTarget",
            {"Ljava/lang/Throwable;"});
        const auto thrown = Invoke(vm, throwing, derived, {target});
        ExpectException(vm, thrown,
                        "Ljava/lang/reflect/InvocationTargetException;");
        CHECK(Ref(vm.Virtual(thrown.exception, "getTargetException",
                             "()Ljava/lang/Throwable;")) == target);
    }
}

TEST_CASE("Method invoke honors caller access and preserves target throwable identity") {
    ReflectionVm vm("reflection.dex");
    const auto derived_class =
        vm.linker.ResolveDescriptor("Lreflect/Derived;");
    const auto derived = vm.model.NewInstance(
        derived_class, vm.linker.Class(derived_class).instance_slots);

    // AOSP API19: AccessCheck.cpp plus Stack.cpp::dvmInvokeMethod. The caller
    // is the interpreted frame outside Method.invoke, never Method itself.
    const auto package_method = MethodWrapper(
        vm, "Lreflect/Derived;", "packageDerived");
    ExpectException(vm, Invoke(vm, package_method, derived),
                    "Ljava/lang/IllegalAccessException;");
    CHECK(vm.interpreter.Reflection().Codec()
              .ConvertArgument(
                  Ref(InvokeFrom(vm, "Lreflect/Peer;", package_method,
                                 derived)),
                  vm.linker.ResolveDescriptor("I"))
              .AsInt() == 16);
    ExpectException(vm, InvokeFrom(vm, "Lother/Outsider;", package_method,
                                   derived),
                    "Ljava/lang/IllegalAccessException;");

    const auto protected_method = MethodWrapper(
        vm, "Lreflect/Derived;", "protectedDerived");
    ExpectException(vm, InvokeFrom(vm, "Lother/Sub;", protected_method,
                                   derived),
                    "Ljava/lang/IllegalAccessException;");
    const auto sub_class = vm.linker.ResolveDescriptor("Lother/Sub;");
    const auto sub = vm.model.NewInstance(
        sub_class, vm.linker.Class(sub_class).instance_slots);
    CHECK(vm.interpreter.Reflection().Codec()
              .ConvertArgument(
                  Ref(InvokeFrom(vm, "Lother/Sub;", protected_method, sub)),
                  vm.linker.ResolveDescriptor("I"))
              .AsInt() == 6);

    const auto package_owner_class =
        vm.linker.ResolveDescriptor("Lreflect/PackageOwner;");
    const auto package_owner = vm.model.NewInstance(
        package_owner_class,
        vm.linker.Class(package_owner_class).instance_slots);
    const auto package_owner_method = MethodWrapper(
        vm, "Lreflect/PackageOwner;", "value");
    CHECK(vm.interpreter.Reflection().Codec()
              .ConvertArgument(
                  Ref(InvokeFrom(vm, "Lreflect/Peer;",
                                 package_owner_method, package_owner)),
                  vm.linker.ResolveDescriptor("I"))
              .AsInt() == 23);
    ExpectException(vm, InvokeFrom(vm, "Lother/Outsider;",
                                   package_owner_method, package_owner),
                    "Ljava/lang/IllegalAccessException;");

    const auto hidden = MethodWrapper(
        vm, "Lreflect/Derived;", "hiddenDerived");
    ExpectException(vm, Invoke(vm, hidden, derived),
                    "Ljava/lang/IllegalAccessException;");
    vm.interpreter.Reflection().SetAccessible(hidden, true);
    CHECK(vm.interpreter.Reflection().Codec()
              .ConvertArgument(Ref(Invoke(vm, hidden, derived)),
                               vm.linker.ResolveDescriptor("I"))
              .AsInt() == 5);

    const auto target = vm.interpreter.MakeThrowable(
        "Ljava/lang/RuntimeException;", "target");
    const auto throwing = MethodWrapper(
        vm, "Lreflect/Derived;", "throwTarget", {"Ljava/lang/Throwable;"});
    const auto outcome = Invoke(vm, throwing, derived, {target});
    ExpectException(vm, outcome,
                    "Ljava/lang/reflect/InvocationTargetException;");
    CHECK(Ref(vm.Virtual(outcome.exception, "getTargetException",
                         "()Ljava/lang/Throwable;")) == target);
    CHECK(Ref(vm.Virtual(outcome.exception, "getCause",
                         "()Ljava/lang/Throwable;")) == target);
}

TEST_CASE("Constructor newInstance initializes converts and wraps target exceptions") {
    ReflectionVm vm("reflection.dex");

    // AOSP API19: .local/aosp/dalvik/vm/native/
    // java_lang_reflect_Constructor.cpp :: constructNative
    const auto constructor = ConstructorWrapper(
        vm, "Lreflect/ConstructTarget;", {"J"});
    const auto instance = Ref(Construct(vm, constructor,
                                        {Box(vm, "I", VmValue::Int(27))}));
    const auto value_field = vm.linker.FindFieldRecursive(
        vm.model.ObjectClass(instance), "value", "I");
    REQUIRE(value_field.has_value());
    CHECK(vm.model.InstanceSlots(instance)
              [vm.linker.Field(*value_field).slot].bits == 27U);
    const auto initialized = vm.linker.FindFieldRecursive(
        vm.linker.ResolveDescriptor("Lreflect/ConstructTarget;"),
        "initialized", "I");
    REQUIRE(initialized.has_value());
    CHECK(vm.linker.Class(vm.linker.Field(*initialized).owner)
              .static_storage[vm.linker.Field(*initialized).slot] == 41U);

    const auto private_constructor = ConstructorWrapper(
        vm, "Lreflect/ConstructTarget;");
    ExpectException(vm, Construct(vm, private_constructor),
                    "Ljava/lang/IllegalAccessException;");
    vm.interpreter.Reflection().SetAccessible(private_constructor, true);
    CHECK(Ref(Construct(vm, private_constructor)).IsValid());

    const auto target = vm.interpreter.MakeThrowable(
        "Ljava/lang/RuntimeException;", "constructor target");
    const auto throwing = ConstructorWrapper(
        vm, "Lreflect/ThrowingConstructor;", {"Ljava/lang/Throwable;"});
    const auto outcome = Construct(vm, throwing, {target});
    ExpectException(vm, outcome,
                    "Ljava/lang/reflect/InvocationTargetException;");
    CHECK(Ref(vm.Virtual(outcome.exception, "getTargetException",
                         "()Ljava/lang/Throwable;")) == target);

    for (const auto descriptor : {"Lreflect/AbstractTarget;",
                                  "Lreflect/BaseContract;", "[I", "I", "V"}) {
        const auto represented = vm.linker.ResolveDescriptor(descriptor);
        try {
            static_cast<void>(vm.interpreter.Reflection().NewInstance(
                represented, std::nullopt));
            FAIL_CHECK("non-instantiable class was allocated");
        } catch (const VmJavaThrow& thrown) {
            CHECK(thrown.descriptor == "Ljava/lang/InstantiationException;");
        }
    }
}

TEST_CASE("Class newInstance uses the nullary constructor without wrapping") {
    ReflectionVm vm("reflection.dex");
    const auto call = [&](const std::string_view descriptor) {
        const auto java_class = vm.linker.ResolveDescriptor(descriptor);
        return vm.Virtual(vm.model.ClassObject(java_class), "newInstance",
                          "()Ljava/lang/Object;");
    };

    // AOSP API19: .local/aosp/dalvik/vm/native/java_lang_Class.cpp ::
    // Dalvik_java_lang_Class_newInstance
    const auto instance = Ref(call("Lreflect/DefaultTarget;"));
    const auto field = vm.linker.FindFieldRecursive(
        vm.model.ObjectClass(instance), "value", "I");
    REQUIRE(field.has_value());
    CHECK(vm.model.InstanceSlots(instance)[vm.linker.Field(*field).slot].bits ==
          31U);
    ExpectException(vm, call("Lreflect/NoDefault;"),
                    "Ljava/lang/InstantiationException;");

    const auto throwing =
        vm.linker.ResolveDescriptor("Lreflect/ThrowingDefault;");
    const auto initialized = vm.interpreter.EnsureClassInitialized(throwing);
    REQUIRE_FALSE(initialized.exception.IsValid());
    const auto target = vm.interpreter.MakeThrowable(
        "Ljava/lang/RuntimeException;", "class target");
    vm.interpreter.SetStaticFieldBits(
        "Lreflect/ThrowingDefault;", "target", "Ljava/lang/Throwable;",
        target.Value());
    const auto outcome = call("Lreflect/ThrowingDefault;");
    ExpectException(vm, outcome, "Ljava/lang/RuntimeException;");
    CHECK(outcome.exception == target);
}

TEST_CASE("Field get and set share access initialization and widening rules") {
    ReflectionVm vm("reflection.dex");
    const auto derived_class =
        vm.linker.ResolveDescriptor("Lreflect/Derived;");
    const auto derived = vm.model.NewInstance(
        derived_class, vm.linker.Class(derived_class).instance_slots);

    // AOSP API19: .local/aosp/dalvik/vm/native/
    // java_lang_reflect_Field.cpp :: validateFieldAccess/getFieldValue
    const auto byte_field = FieldWrapper(
        vm, "Lreflect/Derived;", "byteField");
    REQUIRE_FALSE(vm.Virtual(
        byte_field, "setByte", "(Ljava/lang/Object;B)V",
        {VmValue::Ref(derived), VmValue::Int(-7)}).exception.IsValid());
    CHECK(vm.Virtual(byte_field, "getLong", "(Ljava/lang/Object;)J",
                     {VmValue::Ref(derived)}).value.AsLong() == -7);
    CHECK(vm.interpreter.Reflection().Codec().ConvertArgument(
              Ref(vm.Virtual(byte_field, "get",
                             "(Ljava/lang/Object;)Ljava/lang/Object;",
                             {VmValue::Ref(derived)})),
              vm.linker.ResolveDescriptor("B")).AsInt() == -7);

    const auto long_field = FieldWrapper(
        vm, "Lreflect/Derived;", "longField");
    REQUIRE_FALSE(vm.Virtual(
        long_field, "set", "(Ljava/lang/Object;Ljava/lang/Object;)V",
        {VmValue::Ref(derived),
         VmValue::Ref(Box(vm, "I", VmValue::Int(91)))}).exception.IsValid());
    CHECK(vm.Virtual(long_field, "getDouble", "(Ljava/lang/Object;)D",
                     {VmValue::Ref(derived)}).value.AsDouble() ==
          doctest::Approx(91.0));

    const auto ref_field = FieldWrapper(
        vm, "Lreflect/Derived;", "refField");
    REQUIRE_FALSE(vm.Virtual(
        ref_field, "set", "(Ljava/lang/Object;Ljava/lang/Object;)V",
        {VmValue::Ref(derived), VmValue::Ref(derived)}).exception.IsValid());
    CHECK(Ref(vm.Virtual(ref_field, "get",
                         "(Ljava/lang/Object;)Ljava/lang/Object;",
                         {VmValue::Ref(derived)})) == derived);
    ExpectException(vm, vm.Virtual(
        ref_field, "set", "(Ljava/lang/Object;Ljava/lang/Object;)V",
        {VmValue::Ref(derived), VmValue::Ref(
            vm.interpreter.NewIntrinsicInstance("Ljava/lang/Object;"))}),
        "Ljava/lang/IllegalArgumentException;");

    const auto static_field = FieldWrapper(
        vm, "Lreflect/InvokeStatics;", "staticField");
    REQUIRE_FALSE(vm.Virtual(
        static_field, "setInt", "(Ljava/lang/Object;I)V",
        {VmValue::Ref(VmObjectRef{}), VmValue::Int(17)}).exception.IsValid());
    CHECK(vm.Virtual(static_field, "getLong", "(Ljava/lang/Object;)J",
                     {VmValue::Ref(VmObjectRef{})}).value.AsLong() == 17);
    const auto initialized = vm.linker.FindFieldRecursive(
        vm.linker.ResolveDescriptor("Lreflect/InvokeStatics;"),
        "fieldInitialized", "I");
    REQUIRE(initialized.has_value());
    CHECK(vm.linker.Class(vm.linker.Field(*initialized).owner)
              .static_storage[vm.linker.Field(*initialized).slot] == 55U);

    const auto hidden = FieldWrapper(
        vm, "Lreflect/Derived;", "hiddenDerived");
    ExpectException(vm, vm.Virtual(
        hidden, "getInt", "(Ljava/lang/Object;)I",
        {VmValue::Ref(derived)}), "Ljava/lang/IllegalAccessException;");
    vm.interpreter.Reflection().SetAccessible(hidden, true);
    REQUIRE_FALSE(vm.Virtual(
        hidden, "setInt", "(Ljava/lang/Object;I)V",
        {VmValue::Ref(derived), VmValue::Int(8)}).exception.IsValid());
    CHECK(Int(vm.Virtual(hidden, "getInt", "(Ljava/lang/Object;)I",
                         {VmValue::Ref(derived)})) == 8);

    const auto final_field = FieldWrapper(
        vm, "Lreflect/Derived;", "finalField");
    ExpectException(vm, vm.Virtual(
        final_field, "setInt", "(Ljava/lang/Object;I)V",
        {VmValue::Ref(derived), VmValue::Int(4)}),
        "Ljava/lang/IllegalAccessException;");
    vm.interpreter.Reflection().SetAccessible(final_field, true);
    REQUIRE_FALSE(vm.Virtual(
        final_field, "setInt", "(Ljava/lang/Object;I)V",
        {VmValue::Ref(derived), VmValue::Int(4)}).exception.IsValid());
    const auto volatile_field = FieldWrapper(
        vm, "Lreflect/Derived;", "volatileField");
    CHECK((Int(vm.Virtual(volatile_field, "getModifiers", "()I")) &
           0x0040) != 0);
}

TEST_CASE("reflect Array creates and accesses typed primitive and object arrays") {
    ReflectionVm vm("reflection.dex");
    const auto class_object = [&](const std::string_view descriptor) {
        return vm.model.ClassObject(vm.linker.ResolveDescriptor(descriptor));
    };
    const auto array_call = [&](const std::string_view name,
                                const std::string_view descriptor,
                                std::vector<VmValue> arguments) {
        return vm.Static("Ljava/lang/reflect/Array;", name, descriptor,
                         std::move(arguments));
    };

    // AOSP API19: .local/aosp/libcore/luni/src/main/java/
    // java/lang/reflect/Array.java :: get/set/newInstance
    const auto ints = Ref(array_call(
        "newInstance", "(Ljava/lang/Class;I)Ljava/lang/Object;",
        {VmValue::Ref(class_object("I")), VmValue::Int(2)}));
    CHECK(vm.linker.Class(vm.model.ObjectClass(ints)).descriptor == "[I");
    REQUIRE_FALSE(array_call(
        "setByte", "(Ljava/lang/Object;IB)V",
        {VmValue::Ref(ints), VmValue::Int(0), VmValue::Int(-5)})
                      .exception.IsValid());
    CHECK(array_call("getLong", "(Ljava/lang/Object;I)J",
                     {VmValue::Ref(ints), VmValue::Int(0)})
              .value.AsLong() == -5);
    CHECK(vm.interpreter.Reflection().Codec().ConvertArgument(
              Ref(array_call("get", "(Ljava/lang/Object;I)Ljava/lang/Object;",
                             {VmValue::Ref(ints), VmValue::Int(0)})),
              vm.linker.ResolveDescriptor("I")).AsInt() == -5);
    ExpectException(vm, array_call(
        "setLong", "(Ljava/lang/Object;IJ)V",
        {VmValue::Ref(ints), VmValue::Int(0), VmValue::Long(2)}),
        "Ljava/lang/IllegalArgumentException;");

    const auto bases = Ref(array_call(
        "newInstance", "(Ljava/lang/Class;I)Ljava/lang/Object;",
        {VmValue::Ref(class_object("Lreflect/Base;")), VmValue::Int(1)}));
    const auto derived_class =
        vm.linker.ResolveDescriptor("Lreflect/Derived;");
    const auto derived = vm.model.NewInstance(
        derived_class, vm.linker.Class(derived_class).instance_slots);
    REQUIRE_FALSE(array_call(
        "set", "(Ljava/lang/Object;ILjava/lang/Object;)V",
        {VmValue::Ref(bases), VmValue::Int(0), VmValue::Ref(derived)})
                      .exception.IsValid());
    CHECK(Ref(array_call("get", "(Ljava/lang/Object;I)Ljava/lang/Object;",
                         {VmValue::Ref(bases), VmValue::Int(0)})) == derived);
    ExpectException(vm, array_call(
        "set", "(Ljava/lang/Object;ILjava/lang/Object;)V",
        {VmValue::Ref(bases), VmValue::Int(0), VmValue::Ref(
            vm.interpreter.NewIntrinsicInstance("Ljava/lang/Object;"))}),
        "Ljava/lang/IllegalArgumentException;");

    const auto dims_class = vm.linker.ResolveDescriptor("[I");
    const auto dims = vm.model.NewPrimitiveArray(
        dims_class, JniPrimitiveKind::integer, 2);
    vm.model.SetPrimitiveElement(dims, 0, 2);
    vm.model.SetPrimitiveElement(dims, 1, 3);
    const auto matrix = Ref(array_call(
        "newInstance", "(Ljava/lang/Class;[I)Ljava/lang/Object;",
        {VmValue::Ref(class_object("I")), VmValue::Ref(dims)}));
    CHECK(vm.linker.Class(vm.model.ObjectClass(matrix)).descriptor == "[[I");
    CHECK(vm.model.ArrayLength(matrix) == 2);
    CHECK(vm.model.ArrayLength(vm.model.GetObjectElement(matrix, 0)) == 3);

    CHECK(Int(array_call("getLength", "(Ljava/lang/Object;)I",
                         {VmValue::Ref(matrix)})) == 2);
    ExpectException(vm, array_call(
        "get", "(Ljava/lang/Object;I)Ljava/lang/Object;",
        {VmValue::Ref(ints), VmValue::Int(2)}),
        "Ljava/lang/ArrayIndexOutOfBoundsException;");
    ExpectException(vm, array_call(
        "getLength", "(Ljava/lang/Object;)I",
        {VmValue::Ref(vm.interpreter.NewIntrinsicInstance(
            "Ljava/lang/Object;"))}), "Ljava/lang/IllegalArgumentException;");
    ExpectException(vm, array_call(
        "newInstance", "(Ljava/lang/Class;I)Ljava/lang/Object;",
        {VmValue::Ref(class_object("V")), VmValue::Int(1)}),
        "Ljava/lang/IllegalArgumentException;");
    ExpectException(vm, array_call(
        "newInstance", "(Ljava/lang/Class;I)Ljava/lang/Object;",
        {VmValue::Ref(class_object("I")), VmValue::Int(-1)}),
        "Ljava/lang/NegativeArraySizeException;");
}

TEST_CASE("Dalvik system metadata closes nested enclosing and throws reflection") {
    ReflectionVm vm("reflection.dex");
    const auto class_object = [&](const std::string_view descriptor) {
        return vm.model.ClassObject(vm.linker.ResolveDescriptor(descriptor));
    };
    const auto class_call = [&](const std::string_view descriptor,
                                const std::string_view name,
                                const std::string_view signature) {
        return vm.Virtual(class_object(descriptor), name, signature);
    };

    // AOSP API19: dalvik/annotation/{InnerClass,EnclosingClass,
    // EnclosingMethod,MemberClasses,Throws}.java and Class.java nested APIs.
    CHECK(vm.interpreter.StringUtf8(Ref(class_call(
              "Lreflect/Outer$Member;", "getSimpleName",
              "()Ljava/lang/String;"))) == "Member");
    CHECK(vm.interpreter.StringUtf8(Ref(class_call(
              "Lreflect/Outer$Member;", "getCanonicalName",
              "()Ljava/lang/String;"))) == "reflect.Outer.Member");
    CHECK(Ref(class_call("Lreflect/Outer$Member;", "getDeclaringClass",
                         "()Ljava/lang/Class;")) ==
          class_object("Lreflect/Outer;"));
    CHECK(Ref(class_call("Lreflect/Outer$Member;", "getEnclosingClass",
                         "()Ljava/lang/Class;")) ==
          class_object("Lreflect/Outer;"));
    CHECK(Int(class_call("Lreflect/Outer$Member;", "isMemberClass",
                         "()Z")) == 1);
    CHECK(Int(class_call("Lreflect/Outer$Member;", "getModifiers",
                         "()I")) == 0x0009);
    const auto members = Ref(class_call(
        "Lreflect/Outer;", "getDeclaredClasses", "()[Ljava/lang/Class;"));
    REQUIRE(vm.model.ArrayLength(members) == 1);
    CHECK(vm.model.GetObjectElement(members, 0) ==
          class_object("Lreflect/Outer$Member;"));

    CHECK(vm.interpreter.StringUtf8(Ref(class_call(
              "Lreflect/Outer$1Local;", "getSimpleName",
              "()Ljava/lang/String;"))) == "Local");
    CHECK_FALSE(Ref(class_call("Lreflect/Outer$1Local;", "getCanonicalName",
                               "()Ljava/lang/String;")).IsValid());
    CHECK_FALSE(Ref(class_call("Lreflect/Outer$1Local;", "getDeclaringClass",
                               "()Ljava/lang/Class;")).IsValid());
    CHECK(Ref(class_call("Lreflect/Outer$1Local;", "getEnclosingClass",
                         "()Ljava/lang/Class;")) ==
          class_object("Lreflect/Outer;"));
    CHECK(Int(class_call("Lreflect/Outer$1Local;", "isLocalClass",
                         "()Z")) == 1);
    const auto enclosing_method = Ref(class_call(
        "Lreflect/Outer$1Local;", "getEnclosingMethod",
        "()Ljava/lang/reflect/Method;"));
    CHECK(vm.interpreter.StringUtf8(Ref(vm.Virtual(
              enclosing_method, "getName", "()Ljava/lang/String;"))) ==
          "make");

    CHECK(vm.interpreter.StringUtf8(Ref(class_call(
              "Lreflect/Outer$1;", "getSimpleName",
              "()Ljava/lang/String;"))).empty());
    CHECK(Int(class_call("Lreflect/Outer$1;", "isAnonymousClass", "()Z")) ==
          1);
    CHECK_FALSE(Ref(class_call("Lreflect/Outer$1;", "getCanonicalName",
                               "()Ljava/lang/String;")).IsValid());

    const auto enclosing_constructor = Ref(class_call(
        "Lreflect/Outer$CtorLocal;", "getEnclosingConstructor",
        "()Ljava/lang/reflect/Constructor;"));
    CHECK(vm.interpreter.StringUtf8(Ref(vm.Virtual(
              enclosing_constructor, "getName", "()Ljava/lang/String;"))) ==
          "reflect.Outer");

    const auto risky = MethodWrapper(vm, "Lreflect/Outer;", "risky");
    const auto exceptions_one = Ref(vm.Virtual(
        risky, "getExceptionTypes", "()[Ljava/lang/Class;"));
    const auto exceptions_two = Ref(vm.Virtual(
        risky, "getExceptionTypes", "()[Ljava/lang/Class;"));
    CHECK(exceptions_one != exceptions_two);
    REQUIRE(vm.model.ArrayLength(exceptions_one) == 2);
    CHECK(vm.model.GetObjectElement(exceptions_one, 0) ==
          class_object("Ljava/io/IOException;"));
    CHECK(vm.model.GetObjectElement(exceptions_one, 1) ==
          class_object("Ljava/lang/RuntimeException;"));
}

#include <algorithm>
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

    explicit ReflectionVm(const std::string& fixture = "interp.dex")
        : interpreter([this, &fixture]() -> DexClassLinker& {
              const auto catalog = CoreIntrinsicCatalog();
              linker.RegisterIntrinsics(catalog);
              linker.RegisterDex(ReadFixture(fixture));
              linker.Link();
              return linker;
          }(), model, nullptr, ledger) {}

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
        "hiddenDerived", "childContract", "common", "inheritedContract",
        "protectedDerived"});

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
          std::vector<std::string>{"derivedField", "hiddenDerived"});
    CHECK(MemberNames(vm, Ref(query(
              "getFields", "()[Ljava/lang/reflect/Field;"))) ==
          std::vector<std::string>{"derivedField", "baseField",
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

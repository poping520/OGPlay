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

    ReflectionVm()
        : interpreter([this]() -> DexClassLinker& {
              const auto catalog = CoreIntrinsicCatalog();
              linker.RegisterIntrinsics(catalog);
              linker.RegisterDex(ReadFixture("interp.dex"));
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

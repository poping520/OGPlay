#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"

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

struct LoaderVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;

    explicit LoaderVm(
        const InterpreterBackend backend = InterpreterBackend::switch_dispatch)
        : interpreter([this]() -> DexClassLinker& {
              const auto catalog = CoreIntrinsicCatalog();
              linker.RegisterIntrinsics(catalog);
              linker.RegisterDex(ReadFixture("interp.dex"));
              linker.Link();
              return linker;
          }(), model, nullptr, ledger, InterpreterConfig{.backend = backend}) {}

    [[nodiscard]] VmObjectRef String(const std::string_view value) {
        return interpreter.NewStringUtf8(value);
    }

    [[nodiscard]] VmCallOutcome Static(
        const std::string_view owner, const std::string_view name,
        const std::string_view descriptor,
        std::vector<VmValue> arguments = {}) {
        const auto java_class = linker.FindClass(owner);
        REQUIRE(java_class.has_value());
        const auto method = linker.FindDirectMethod(
            *java_class, std::string(name), std::string(descriptor));
        REQUIRE(method.has_value());
        return interpreter.Call(*method, arguments);
    }

    [[nodiscard]] VmCallOutcome Virtual(
        const VmObjectRef receiver, const std::string_view name,
        const std::string_view descriptor,
        std::vector<VmValue> arguments = {}) {
        const auto actual = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(
            actual, std::string(name), std::string(descriptor));
        REQUIRE(index.has_value());
        const auto& linked = linker.Class(actual);
        REQUIRE(*index < linked.vtable.size());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return interpreter.Call(linked.vtable[*index], arguments);
    }

    [[nodiscard]] VmCallOutcome Direct(
        const VmObjectRef receiver, const std::string_view name,
        const std::string_view descriptor,
        std::vector<VmValue> arguments = {}) {
        const auto actual = model.ObjectClass(receiver);
        const auto method = linker.FindDirectMethod(
            actual, std::string(name), std::string(descriptor));
        REQUIRE(method.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return interpreter.Call(*method, arguments);
    }
};

VmObjectRef Ref(const VmCallOutcome& outcome) {
    REQUIRE_FALSE(outcome.exception.IsValid());
    REQUIRE(outcome.value.kind == VmValue::Kind::ref);
    return outcome.value.ref;
}

void ExpectException(LoaderVm& vm, const VmCallOutcome& outcome,
                     const std::string_view descriptor) {
    REQUIRE(outcome.exception.IsValid());
    CHECK(outcome.exception_class.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor == descriptor);
}

void ExpectClassNotFound(LoaderVm& vm, const VmCallOutcome& outcome) {
    ExpectException(vm, outcome, "Ljava/lang/ClassNotFoundException;");
    CHECK(outcome.exception_message.size() > 0);
}

}  // namespace

TEST_CASE("ClassLoader system and bootstrap facades have stable API19 identity") {
    LoaderVm vm;

    // AOSP API19: libcore ClassLoader.java :: createSystemClassLoader/getParent
    const auto system_one = Ref(vm.Static(
        "Ljava/lang/ClassLoader;", "getSystemClassLoader",
        "()Ljava/lang/ClassLoader;"));
    const auto system_two = Ref(vm.Static(
        "Ljava/lang/ClassLoader;", "getSystemClassLoader",
        "()Ljava/lang/ClassLoader;"));
    CHECK(system_one == system_two);
    CHECK(vm.linker.Class(vm.model.ObjectClass(system_one)).descriptor ==
          "Ldalvik/system/PathClassLoader;");

    const auto boot = Ref(vm.Virtual(
        system_one, "getParent", "()Ljava/lang/ClassLoader;"));
    CHECK(vm.linker.Class(vm.model.ObjectClass(boot)).descriptor ==
          "Ljava/lang/BootClassLoader;");
    CHECK_FALSE(Ref(vm.Virtual(
        boot, "getParent", "()Ljava/lang/ClassLoader;")).IsValid());

    static_cast<void>(vm.interpreter.CollectGarbage("class_loader_roots"));
    CHECK(Ref(vm.Static("Ljava/lang/ClassLoader;", "getSystemClassLoader",
                        "()Ljava/lang/ClassLoader;")) == system_one);
    CHECK(Ref(vm.Virtual(system_one, "getParent",
                         "()Ljava/lang/ClassLoader;")) == boot);
}

TEST_CASE("Class getClassLoader follows defining loader and primitive rules") {
    LoaderVm vm;
    const auto class_loader = [&](const std::string_view descriptor) {
        const auto represented = vm.linker.ResolveDescriptor(descriptor);
        return Ref(vm.Virtual(vm.model.ClassObject(represented),
                              "getClassLoader",
                              "()Ljava/lang/ClassLoader;"));
    };

    // AOSP API19: libcore Class.java :: getClassLoader/getClassLoaderImpl
    const auto boot = class_loader("Ljava/lang/String;");
    const auto application = class_loader("LCounter;");
    CHECK(boot == vm.interpreter.ClassLoaders().BootstrapLoader());
    CHECK(application == vm.interpreter.ClassLoaders().ApplicationLoader());
    CHECK_FALSE(class_loader("I").IsValid());
    CHECK_FALSE(class_loader("V").IsValid());
    CHECK(class_loader("[Ljava/lang/String;") == boot);
    CHECK(class_loader("[[LCounter;") == application);
}

TEST_CASE("ClassLoader separates known classes from initiating loader state") {
    LoaderVm vm;
    const auto system = vm.interpreter.ClassLoaders().ApplicationLoader();
    const auto boot = vm.interpreter.ClassLoaders().BootstrapLoader();
    const auto counter = vm.linker.FindClass("LCounter;");
    REQUIRE(counter.has_value());
    CHECK_FALSE(vm.linker.IsInitiatedBy(*counter, kApplicationLoader));

    const auto find_loaded = [&](const VmObjectRef loader,
                                 const std::string_view name) {
        return Ref(vm.Virtual(
            loader, "findLoadedClass",
            "(Ljava/lang/String;)Ljava/lang/Class;",
            {VmValue::Ref(vm.String(name))}));
    };
    CHECK_FALSE(find_loaded(system, "Counter").IsValid());
    CHECK(find_loaded(boot, "java.lang.String").IsValid());
    CHECK_FALSE(find_loaded(system, "[broken").IsValid());

    const auto loaded = Ref(vm.Virtual(
        system, "loadClass", "(Ljava/lang/String;Z)Ljava/lang/Class;",
        {VmValue::Ref(vm.String("Counter")), VmValue::Int(1)}));
    CHECK(loaded == vm.model.ClassObject(*counter));
    CHECK(vm.linker.IsInitiatedBy(*counter, kApplicationLoader));
    CHECK(find_loaded(system, "Counter") == loaded);

    const auto string_class = vm.linker.FindClass("Ljava/lang/String;");
    REQUIRE(string_class.has_value());
    CHECK_FALSE(vm.linker.IsInitiatedBy(*string_class, kApplicationLoader));
    const auto delegated = Ref(vm.Virtual(
        system, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
        {VmValue::Ref(vm.String("java.lang.String"))}));
    CHECK(delegated == vm.model.ClassObject(*string_class));
    CHECK(vm.linker.IsInitiatedBy(*string_class, kApplicationLoader));
    CHECK(vm.linker.Class(*string_class).defining_loader == kBootstrapLoader);

    const auto app_array = Ref(vm.Virtual(
        system, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
        {VmValue::Ref(vm.String("[LCounter;"))}));
    CHECK(vm.interpreter.ClassLoaders().LoaderForClass(
              vm.model.ClassOfClassObject(app_array)) == system);

    const auto clinit_user = vm.linker.FindClass("LClinitUser;");
    REQUIRE(clinit_user.has_value());
    CHECK(vm.linker.Class(*clinit_user).clinit_state ==
          ClinitState::uninitialized);
    static_cast<void>(Ref(vm.Virtual(
        system, "loadClass", "(Ljava/lang/String;Z)Ljava/lang/Class;",
        {VmValue::Ref(vm.String("ClinitUser")), VmValue::Int(1)})));
    CHECK(vm.linker.Class(*clinit_user).clinit_state ==
          ClinitState::uninitialized);
}

TEST_CASE("ClassLoader refuses dynamic namespaces and bootstrap app lookup") {
    LoaderVm vm;
    const auto system = vm.interpreter.ClassLoaders().ApplicationLoader();
    const auto boot = vm.interpreter.ClassLoaders().BootstrapLoader();

    ExpectClassNotFound(vm, vm.Virtual(
        boot, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
        {VmValue::Ref(vm.String("Counter"))}));
    ExpectClassNotFound(vm, vm.Virtual(
        system, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
        {VmValue::Ref(vm.String("missing.Type"))}));
    ExpectClassNotFound(vm, vm.Virtual(
        system, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
        {VmValue::Ref(vm.String("int"))}));
    ExpectException(vm, vm.Virtual(
        system, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
        {VmValue::Ref(VmObjectRef(0))}), "Ljava/lang/NullPointerException;");
    ExpectException(vm, vm.Virtual(
        system, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
        {VmValue::Ref(vm.String("DormantOptional"))}),
        "Ljava/lang/LinkageError;");

    const auto custom =
        vm.interpreter.NewIntrinsicInstance("Ljava/lang/ClassLoader;");
    const auto constructed = vm.Direct(custom, "<init>", "()V");
    CHECK_FALSE(constructed.exception.IsValid());
    CHECK(Ref(vm.Virtual(custom, "getParent",
                         "()Ljava/lang/ClassLoader;")) == system);
    const auto loaded = Ref(vm.Virtual(
        custom, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
        {VmValue::Ref(vm.String("Counter"))}));
    CHECK(loaded.IsValid());

    ExpectClassNotFound(vm, vm.Virtual(
        custom, "findClass", "(Ljava/lang/String;)Ljava/lang/Class;",
        {VmValue::Ref(vm.String("dynamic.Type"))}));
}

TEST_CASE("Class forName follows API19 caller loader initialization and errors") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        LoaderVm vm(backend);
        const auto find = [&](const std::string_view name) {
            return vm.Static(
                "LForNameCaller;", "find",
                "(Ljava/lang/String;)Ljava/lang/Class;",
                {VmValue::Ref(vm.String(name))});
        };
        const auto find_with_loader =
            [&](const std::string_view name, const bool initialize,
                const VmObjectRef loader) {
                return vm.Static(
                    "LForNameCaller;", "findWithLoader",
                    "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;",
                    {VmValue::Ref(vm.String(name)),
                     VmValue::Int(initialize ? 1 : 0), VmValue::Ref(loader)});
            };

        // AOSP API19: libcore Class.java :: forName and Dalvik
        // java_lang_Class.cpp :: Dalvik_java_lang_Class_classForName.
        CHECK(Ref(find("Counter")) == vm.model.ClassObject(
                  vm.linker.ResolveDescriptor("LCounter;")));
        CHECK(Ref(find("java.lang.String")) == vm.model.ClassObject(
                  vm.linker.ResolveDescriptor("Ljava/lang/String;")));
        CHECK(Ref(find("[LCounter;")) == vm.model.ClassObject(
                  vm.linker.ResolveDescriptor("[LCounter;")));
        ExpectClassNotFound(vm, find("int"));
        ExpectClassNotFound(vm, find("missing.Type"));
        const auto linkage = find("DormantOptional");
        ExpectClassNotFound(vm, linkage);
        const auto linkage_cause = Ref(vm.Virtual(
            linkage.exception, "getCause", "()Ljava/lang/Throwable;"));
        CHECK(vm.linker.Class(vm.model.ObjectClass(linkage_cause)).descriptor ==
              "Ljava/lang/LinkageError;");
        CHECK(Ref(vm.Virtual(linkage.exception, "getException",
                             "()Ljava/lang/Throwable;")) == linkage_cause);
        CHECK(Ref(find("[I")) == vm.model.ClassObject(
                  vm.linker.ResolveDescriptor("[I")));

        const auto system = vm.interpreter.ClassLoaders().ApplicationLoader();
        const auto boot = vm.interpreter.ClassLoaders().BootstrapLoader();
        CHECK(Ref(find_with_loader("Counter", false, VmObjectRef{})) ==
              vm.model.ClassObject(vm.linker.ResolveDescriptor("LCounter;")));
        CHECK(Ref(find_with_loader("java.lang.String", false, boot)) ==
              vm.model.ClassObject(
                  vm.linker.ResolveDescriptor("Ljava/lang/String;")));
        ExpectClassNotFound(vm,
                            find_with_loader("Counter", false, boot));

        const auto custom =
            vm.interpreter.NewIntrinsicInstance("Ljava/lang/ClassLoader;");
        REQUIRE_FALSE(vm.Direct(custom, "<init>", "()V").exception.IsValid());
        CHECK(Ref(find_with_loader("Counter", false, custom)) ==
              vm.model.ClassObject(vm.linker.ResolveDescriptor("LCounter;")));

        const auto clinit = vm.linker.ResolveDescriptor("LClinitUser;");
        CHECK(vm.linker.Class(clinit).clinit_state ==
              ClinitState::uninitialized);
        CHECK(Ref(find_with_loader("ClinitUser", false, system)) ==
              vm.model.ClassObject(clinit));
        CHECK(vm.linker.Class(clinit).clinit_state ==
              ClinitState::uninitialized);
        CHECK(Ref(find_with_loader("ClinitUser", true, system)) ==
              vm.model.ClassObject(clinit));
        CHECK(vm.linker.Class(clinit).clinit_state == ClinitState::initialized);

        const auto null_name = vm.Static(
            "LForNameCaller;", "find",
            "(Ljava/lang/String;)Ljava/lang/Class;",
            {VmValue::Ref(VmObjectRef{})});
        ExpectException(vm, null_name, "Ljava/lang/NullPointerException;");

        const auto failing =
            vm.linker.ResolveDescriptor("LForNameInitFailure;");
        const auto failed = find_with_loader("ForNameInitFailure", true, system);
        REQUIRE(failed.exception.IsValid());
        CHECK(vm.linker.Class(failed.exception_class).descriptor ==
              "Ljava/lang/ExceptionInInitializerError;");
        const auto target_field = vm.linker.FindFieldRecursive(
            failing, "target", "Ljava/lang/Throwable;");
        REQUIRE(target_field.has_value());
        const auto& field = vm.linker.Field(*target_field);
        CHECK(failed.exception == VmObjectRef(
                  vm.linker.Class(failing).static_storage[field.slot]));
    }
}

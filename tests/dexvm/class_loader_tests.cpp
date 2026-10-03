#include "boot_dex.h"
#include <cstddef>
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/loader/dex.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/reflection.h"
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
        const InterpreterBackend backend = InterpreterBackend::switch_dispatch,
        CoreIntrinsicServices services = {})
        : interpreter([this, &services]() -> DexClassLinker& {
              const auto catalog = CoreIntrinsicCatalog(std::move(services));
              linker.RegisterIntrinsics(catalog);
              linker.RegisterDex(ReadFixture("interp.dex"));
              ogplay::test::RegisterBootDex(linker);
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

TEST_CASE("Class and ClassLoader resource streams preserve loader and package semantics") {
    CoreIntrinsicServices services;
    services.find_classpath_resource = [](
        const CoreIntrinsicServices::ClasspathLoader loader, const std::string_view name)
        -> std::optional<CoreIntrinsicServices::ClasspathResource> {
        if (name == "java/lang/boot.txt") return {{"/system/framework/bootdex.jar", std::string(name)}};
        if (loader == CoreIntrinsicServices::ClasspathLoader::application && name == "app.txt")
            return {{"/data/app/test-1.apk", std::string(name)}};
        return std::nullopt;
    };
    services.read_classpath_resource = [](const CoreIntrinsicServices::ClasspathResource& resource)
        -> std::optional<std::vector<std::byte>> {
        return std::vector<std::byte>{resource.entry_name == "app.txt" ? std::byte{'a'} : std::byte{'b'}};
    };
    LoaderVm vm(InterpreterBackend::switch_dispatch, std::move(services));
    const auto read = [&](const VmObjectRef stream) {
        REQUIRE(stream.IsValid());
        return vm.Virtual(stream, "read", "()I").value.AsInt();
    };

    const auto string_class = vm.model.ClassObject(
        vm.linker.ResolveDescriptor("Ljava/lang/String;"));
    const auto boot_stream = Ref(vm.Virtual(
        string_class, "getResourceAsStream",
        "(Ljava/lang/String;)Ljava/io/InputStream;",
        {VmValue::Ref(vm.String("boot.txt"))}));
    CHECK(read(boot_stream) == 'b');
    CHECK(read(boot_stream) == -1);

    const auto counter_class = vm.model.ClassObject(
        vm.linker.ResolveDescriptor("LCounter;"));
    const auto app_stream = Ref(vm.Virtual(
        counter_class, "getResourceAsStream",
        "(Ljava/lang/String;)Ljava/io/InputStream;",
        {VmValue::Ref(vm.String("/app.txt"))}));
    CHECK(read(app_stream) == 'a');

    const auto boot_loader = vm.interpreter.ClassLoaders().BootstrapLoader();
    CHECK_FALSE(Ref(vm.Virtual(
        boot_loader, "getResourceAsStream",
        "(Ljava/lang/String;)Ljava/io/InputStream;",
        {VmValue::Ref(vm.String("app.txt"))})).IsValid());
}

TEST_CASE("Class resource URLs preserve API19 names and open actual streams") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        using Role = CoreIntrinsicServices::ClasspathLoader;
        using Resource = CoreIntrinsicServices::ClasspathResource;
        CoreIntrinsicServices services;
        services.find_classpath_resource = [](Role role, std::string_view name)
            -> std::optional<Resource> {
            if (name == "java/lang/boot.txt") return {{"/system/framework/bootdex.jar", std::string(name)}};
            if (role == Role::application && name == "root %.txt")
                return {{"/data/app/test-1.apk", std::string(name)}};
            return std::nullopt;
        };
        services.read_classpath_resource = [](const Resource& resource)
            -> std::optional<std::vector<std::byte>> {
            if (resource.archive_path != "/system/framework/bootdex.jar" &&
                resource.archive_path != "/data/app/test-1.apk") {
                throw std::invalid_argument("unregistered archive");
            }
            if (resource.entry_name == "corrupt") throw std::runtime_error("CRC mismatch");
            if (resource.entry_name != "root %.txt" && resource.entry_name != "java/lang/boot.txt")
                return std::nullopt;
            return std::vector<std::byte>{std::byte{'x'}, std::byte{'y'}};
        };
        LoaderVm vm(backend, services);
        const auto app = vm.interpreter.ClassLoaders().ApplicationLoader();
        const auto boot = vm.interpreter.ClassLoaders().BootstrapLoader();
        const auto lookup = [&](VmObjectRef owner, std::string_view name) {
            return Ref(vm.Virtual(owner, "getResource", "(Ljava/lang/String;)Ljava/net/URL;",
                                  {VmValue::Ref(vm.String(name))}));
        };
        const auto text = [&](VmObjectRef owner, const char* method) {
            return vm.interpreter.StringUtf8(Ref(vm.Virtual(owner, method, "()Ljava/lang/String;")));
        };
        const auto url = lookup(app, "root %.txt");
        REQUIRE(url.IsValid());
        CHECK(text(url, "toExternalForm") == "jar:file:/data/app/test-1.apk!/root%20%25.txt");
        CHECK(text(url, "getProtocol") == "jar");
        const auto connection = Ref(vm.Virtual(url, "openConnection", "()Ljava/net/URLConnection;"));
        CHECK(Ref(vm.Virtual(connection, "getURL", "()Ljava/net/URL;")) == url);
        const auto stream = Ref(vm.Virtual(connection, "getInputStream", "()Ljava/io/InputStream;"));
        CHECK(vm.linker.Class(vm.model.ObjectClass(stream)).descriptor == "Ljava/io/ByteArrayInputStream;");
        CHECK(vm.Virtual(stream, "read", "()I").value.AsInt() == 'x');
        CHECK(vm.Virtual(stream, "read", "()I").value.AsInt() == 'y');
        CHECK(vm.Virtual(stream, "read", "()I").value.AsInt() == -1);
        const auto fresh = Ref(vm.Virtual(url, "openStream", "()Ljava/io/InputStream;"));
        CHECK(fresh != stream);
        CHECK(vm.Virtual(fresh, "read", "()I").value.AsInt() == 'x');
        CHECK_FALSE(lookup(boot, "root %.txt").IsValid());
        CHECK_FALSE(lookup(app, "missing").IsValid());
        CHECK_FALSE(lookup(app, "/root %.txt").IsValid());
        const auto string = vm.model.ClassObject(vm.linker.ResolveDescriptor("Ljava/lang/String;"));
        CHECK(text(lookup(string, "boot.txt"), "toExternalForm") ==
              "jar:file:/system/framework/bootdex.jar!/java/lang/boot.txt");
        CHECK_FALSE(lookup(string, "/root %.txt").IsValid());
        const auto counter = vm.model.ClassObject(vm.linker.ResolveDescriptor("LCounter;"));
        CHECK(lookup(counter, "/root %.txt").IsValid());
        CHECK_FALSE(lookup(counter, "root %.txt").IsValid()); // API19 default package prepends '/'.
        CHECK_FALSE(lookup(counter, "//root %.txt").IsValid()); // strip exactly one slash.
        const auto primitive = vm.model.ClassObject(vm.linker.ResolveDescriptor("I"));
        CHECK(lookup(primitive, "/root %.txt").IsValid()); // null loader -> system loader.
        CHECK(Ref(vm.Static("Ljava/lang/ClassLoader;", "getSystemResource",
                           "(Ljava/lang/String;)Ljava/net/URL;",
                           {VmValue::Ref(vm.String("root %.txt"))})).IsValid());
        for (const auto owner : {app, string}) {
            ExpectException(vm, vm.Virtual(owner, "getResource", "(Ljava/lang/String;)Ljava/net/URL;",
                                          {VmValue::Ref(VmObjectRef{})}), "Ljava/lang/NullPointerException;");
        }
        const auto from_spec = [&](std::string_view spec) {
            const auto result = vm.interpreter.NewIntrinsicInstance("Ljava/net/URL;");
            REQUIRE_FALSE(vm.Direct(result, "<init>", "(Ljava/lang/String;)V",
                                    {VmValue::Ref(vm.String(spec))}).exception.IsValid());
            return result;
        };
        const auto restored = from_spec(text(url, "toExternalForm"));
        CHECK(vm.Virtual(Ref(vm.Virtual(restored, "openStream", "()Ljava/io/InputStream;")),
                         "read", "()I").value.AsInt() == 'x');
        for (const auto spec : {"jar:file:/tmp/host.zip!/root%20%25.txt",
                                "jar:https://example.com/a.jar!/x",
                                "jar:file:/data/app/test-1.apk!/corrupt"}) {
            ExpectException(vm, vm.Virtual(from_spec(spec), "openStream", "()Ljava/io/InputStream;"),
                            "Ljava/io/IOException;");
        }
        ExpectException(vm, vm.Virtual(from_spec("jar:file:/data/app/test-1.apk!/missing"),
                                      "openStream", "()Ljava/io/InputStream;"),
                        "Ljava/io/FileNotFoundException;");
        const auto rejected = vm.interpreter.NewIntrinsicInstance("Ljava/net/URL;");
        ExpectException(vm, vm.Direct(rejected, "<init>", "(Ljava/lang/String;)V",
            {VmValue::Ref(vm.String("jar:file:/data/app/test-1.apk!/bad%zz"))}),
            "Ljava/net/MalformedURLException;");
        const auto no_input = Ref(vm.Virtual(url, "openConnection", "()Ljava/net/URLConnection;"));
        REQUIRE_FALSE(vm.Virtual(no_input, "setDoInput", "(Z)V", {VmValue::Int(0)}).exception.IsValid());
        ExpectException(vm, vm.Virtual(no_input, "getInputStream", "()Ljava/io/InputStream;"),
                        "Ljava/net/ProtocolException;");
        const auto hits = vm.ledger.Unimplemented();
        CHECK(std::any_of(hits.begin(), hits.end(),
                          [](const auto& hit) { return hit.id == "dexvm.classloader.resource_url"; }));
        const auto gc_connection = Ref(vm.Virtual(url, "openConnection", "()Ljava/net/URLConnection;"));
        const auto gc_stream = Ref(vm.Virtual(gc_connection, "getInputStream", "()Ljava/io/InputStream;"));
        CHECK(vm.Virtual(gc_stream, "read", "()I").value.AsInt() == 'x');
        const auto roots = vm.interpreter.ProtectReferences(std::array{gc_connection});
        static_cast<void>(vm.interpreter.CollectGarbage("classpath_connection_roots"));
        CHECK(Ref(vm.Virtual(gc_connection, "getURL", "()Ljava/net/URL;")) == url);
        CHECK(Ref(vm.Virtual(gc_connection, "getInputStream", "()Ljava/io/InputStream;")) == gc_stream);
        CHECK(vm.Virtual(gc_stream, "read", "()I").value.AsInt() == 'y');
    }
    LoaderVm no_provider;
    ExpectException(no_provider, no_provider.Virtual(
        no_provider.interpreter.ClassLoaders().ApplicationLoader(), "getResource",
        "(Ljava/lang/String;)Ljava/net/URL;", {VmValue::Ref(no_provider.String("x"))}),
        "Ljava/lang/UnsupportedOperationException;");
}

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


namespace {
std::vector<IntrinsicClassDecl> ParentFirstCatalog() {
    auto catalog = CoreIntrinsicCatalog();
    std::erase_if(catalog, [](const auto& declaration) {
        const auto& name = declaration.descriptor;
        return name != "Ljava/lang/Object;" && name != "Ljava/lang/Class;" &&
               name != "Ljava/lang/ClassLoader;" && name != "Ljava/lang/BootClassLoader;" &&
               name != "Ldalvik/system/PathClassLoader;";
    });
    return catalog;
}
}  // namespace

TEST_CASE("ClassLoader parent-first registration preserves bootstrap identity and app-only classes") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        DexClassLinker linker;
        auto catalog = ParentFirstCatalog();
        auto intrinsic = IntrinsicClassBuilder::Class("Lshared/IntrinsicOwned;");
        intrinsic.StaticMethod("answer", "()I", [](IntrinsicContext&) { return VmValue::Int(71); });
        catalog.push_back(std::move(intrinsic).Build());
        linker.RegisterIntrinsics(catalog);
        const auto boot = linker.RegisterBootDex(ReadFixture("parent_first_boot.dex"));
        const auto original = linker.FindClass("Lshared/ParentOwned;");
        REQUIRE(original.has_value());
        const auto app_bytes = ReadFixture("parent_first_app.dex");
        const auto image = ogplay::loader::ParseDex(app_bytes);
        const auto app = linker.RegisterDex(app_bytes);
        linker.Link();
        CHECK(linker.FindClass("Lshared/ParentOwned;") == original);
        CHECK(linker.Class(*original).dex_unit == boot);
        CHECK(linker.Class(*original).defining_loader == kBootstrapLoader);
        CHECK(linker.Class(*original).own_static_fields.size() == 1);
        CHECK_FALSE(linker.FindFieldRecursive(*original, "appOnly", "I").has_value());
        CHECK_FALSE(linker.FindDirectMethod(*original, "appOnly", "()I").has_value());
        const auto child = linker.ResolveDescriptor("Lshared/AppOnly;");
        CHECK(linker.Class(child).defining_loader == kApplicationLoader);
        CHECK(linker.Class(child).super == original);
        CHECK(linker.IsAssignable(linker.ResolveDescriptor("Lshared/Contract;"), child));
        for (std::uint32_t i = 0; i < image.types.size(); ++i) {
            if (image.types[i].descriptor == "Lshared/ParentOwned;")
                CHECK(linker.ResolveTypeIndex(app, i) == *original);
        }
        for (std::uint32_t i = 0; i < image.methods.size(); ++i) {
            const auto& entry = image.methods[i];
            if (image.types[entry.class_type_index].descriptor != "Lshared/ParentOwned;") continue;
            const auto& name = image.strings[entry.name_string_index].value;
            if (name == u"answer") {
                const auto call = linker.ResolveMethodIndex(app, i, InvokeKind::static_call);
                CHECK(linker.Method(call.method).dex_unit == boot);
            } else if (name == u"appOnly") {
                CHECK_THROWS_AS(static_cast<void>(linker.ResolveMethodIndex(app, i, InvokeKind::static_call)), DexVmError);
            }
        }
        JniStringStore strings;
        JniPrimitiveArrayStore arrays;
        JavaObjectModel model(strings, arrays);
        ogplay::core::CapabilityLedger ledger;
        Interpreter vm(linker, model, nullptr, ledger, {.backend = backend});
        auto& loaders = vm.ClassLoaders();
        CHECK_FALSE(linker.IsInitiatedBy(*original, kApplicationLoader));
        CHECK(loaders.LoadClass(kApplicationLoader, "shared.ParentOwned") == *original);
        CHECK(loaders.LoadClass(kBootstrapLoader, "shared.ParentOwned") == *original);
        CHECK(linker.IsInitiatedBy(*original, kApplicationLoader));
        CHECK(loaders.LoaderForClass(*original) == loaders.BootstrapLoader());
        CHECK(loaders.LoaderForClass(child) == loaders.ApplicationLoader());
        CHECK(model.ClassObject(linker.ResolveDescriptor("Lshared/ParentOwned;")) == model.ClassObject(*original));
        CHECK(linker.Class(*original).clinit_state == ClinitState::uninitialized);
        for (const auto& [name, expected] : std::array<std::pair<const char*, int>, 5>{{
            {"readParent", 42}, {"readField", 42}, {"readObserver", 0},
            {"readIntrinsic", 71}, {"useInterface", 7}}}) {
            const auto method = linker.FindDirectMethod(linker.ResolveDescriptor("Lshared/Caller;"), name, "()I");
            REQUIRE(method.has_value());
            const auto result = vm.Call(*method, {});
            REQUIRE_FALSE(result.exception.IsValid());
            CHECK(result.value.AsInt() == expected);
        }
        CHECK(linker.Class(*original).clinit_state == ClinitState::initialized);
        const auto fields = vm.Reflection().DeclaredFields(*original);
        REQUIRE(fields.size() == 1);
        CHECK(linker.Field(fields[0].field).name == "seed");
        const auto methods = vm.Reflection().DeclaredMethods(*original);
        REQUIRE(methods.size() == 1);
        CHECK(linker.Method(methods[0].method).name == "answer");
    }
}

TEST_CASE("ClassLoader parent-first registration also accepts identical boot and app definitions") {
    DexClassLinker linker;
    linker.RegisterIntrinsics(ParentFirstCatalog());
    const auto bytes = ReadFixture("parent_first_boot.dex");
    const auto boot = linker.RegisterBootDex(bytes);
    const auto original = linker.FindClass("Lshared/ParentOwned;");
    REQUIRE(original.has_value());
    linker.RegisterDex(bytes);
    linker.Link();
    CHECK(linker.FindClass("Lshared/ParentOwned;") == original);
    CHECK(linker.Class(*original).dex_unit == boot);
    CHECK(linker.Class(*original).defining_loader == kBootstrapLoader);
}

TEST_CASE("ClassLoader parent-first registration still rejects malformed duplicate class definitions") {
    const auto bytes = ReadFixture("parent_first_boot.dex");
    const auto image = ogplay::loader::ParseDex(bytes);
    REQUIRE(image.classes.size() >= 2);
    auto invalid = bytes;
    const auto offset = image.header.class_defs_offset;
    std::copy_n(invalid.begin() + offset, 4, invalid.begin() + offset + 32);
    for (const bool boot : {false, true}) {
        DexClassLinker linker;
        linker.RegisterIntrinsics(ParentFirstCatalog());
        if (!boot) linker.RegisterBootDex(bytes);
        try {
            if (boot) linker.RegisterBootDex(invalid);
            else linker.RegisterDex(invalid);
            FAIL("duplicate class_def must fail even if a parent definition exists");
        } catch (const ogplay::loader::DexError& error) {
            CHECK(error.Reason() == ogplay::loader::DexErrorReason::invalid_class_def);
            CHECK(error.Offset() == offset + 32);
        }
    }
}

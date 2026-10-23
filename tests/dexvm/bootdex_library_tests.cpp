#include <bit>
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <cstdint>
#include <algorithm>
#include <memory>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "tls_fixtures.h"

#include "bootdex_library_fixture.h"

using namespace ogplay::test::bootdex_library;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-87 Arrays primitive algorithms are deterministic") {
    Dvm87Vm fixture;
    const auto array = fixture.model.NewPrimitiveArray(
        fixture.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 4);
    for (std::int32_t index = 0; index < 4; ++index) {
        constexpr std::int32_t values[]{7, -2, 7, 3};
        fixture.model.SetPrimitiveElement(array, index, static_cast<std::uint64_t>(values[index]));
    }

    Dvm87Vm::RequireOk(fixture.Static(
        "Ljava/util/Arrays;", "sort", "([I)V", {VmValue::Ref(array)}));
    CHECK(static_cast<std::int32_t>(
              fixture.model.GetPrimitiveElement(array, 0)) == -2);
    CHECK(fixture.model.GetPrimitiveElement(array, 1) == 3);
    CHECK(fixture.model.GetPrimitiveElement(array, 2) == 7);
    CHECK(fixture.Static(
        "Ljava/util/Arrays;", "binarySearch", "([II)I",
        {VmValue::Ref(array), VmValue::Int(3)}).value.AsInt() == 1);

    const auto object_array = fixture.model.NewObjectArray(
        fixture.linker.ResolveDescriptor("[Ljava/lang/Object;"),
        fixture.linker.ResolveDescriptor("Ljava/lang/Object;"), 3);
    const auto first = fixture.vm.NewStringUtf8("first");
    const auto second = fixture.vm.NewStringUtf8("second");
    fixture.model.SetObjectElement(object_array, 0, first);
    fixture.model.SetObjectElement(object_array, 1, second);
    fixture.model.SetObjectElement(object_array, 2, second);
    const auto list = fixture.Static(
        "Ljava/util/Arrays;", "asList",
        "([Ljava/lang/Object;)Ljava/util/List;",
        {VmValue::Ref(object_array)}).value.ref;
    Dvm87Vm::RequireOk(fixture.Static(
        "Ljava/util/Collections;", "reverse", "(Ljava/util/List;)V",
        {VmValue::Ref(list)}));
    CHECK(fixture.model.GetObjectElement(object_array, 0) == second);
    CHECK(fixture.model.GetObjectElement(object_array, 1) == second);
    CHECK(fixture.model.GetObjectElement(object_array, 2) == first);
    CHECK(fixture.Static(
        "Ljava/util/Collections;", "frequency",
        "(Ljava/util/Collection;Ljava/lang/Object;)I",
        {VmValue::Ref(list), VmValue::Ref(second)}).value.AsInt() == 2);
}

TEST_CASE("API19 regex requires the guest ICU bridge") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm fixture(backend);
        CHECK_THROWS_AS(static_cast<void>(fixture.Static("Ljava/util/regex/Pattern;", "compile",
            "(Ljava/lang/String;)Ljava/util/regex/Pattern;", {VmValue::Ref(fixture.vm.NewStringUtf8("a+"))})), DexVmError);
    }
}

TEST_CASE("API19 Build family executes from BootDex without class intrinsics") {
  for (const auto backend : {InterpreterBackend::switch_dispatch,
                             InterpreterBackend::threaded}) {
    Dvm87Vm f(backend);
    for (const auto descriptor : {
             "Landroid/os/Build;", "Landroid/os/Build$VERSION;",
             "Landroid/os/Build$VERSION_CODES;"}) {
      const auto type = f.linker.ResolveDescriptor(descriptor);
      CAPTURE(std::string(descriptor));
      CHECK(f.linker.Class(type).is_boot_dex);
      for (const auto method : f.linker.Class(type).own_direct_methods)
        CHECK(f.linker.Method(method).kind != MethodKind::intrinsic);
      for (const auto method : f.linker.Class(type).own_virtual_methods)
        CHECK(f.linker.Method(method).kind != MethodKind::intrinsic);
      Dvm87Vm::RequireOk(f.vm.EnsureClassInitialized(type));
    }
    const auto string_field = [&](const std::string_view descriptor,
                                  const std::string_view name) {
      const auto type = f.linker.ResolveDescriptor(descriptor);
      const auto field = f.linker.FindFieldRecursive(
          type, std::string(name), "Ljava/lang/String;");
      REQUIRE(field.has_value());
      return f.vm.StringUtf8(VmObjectRef{static_cast<std::uint32_t>(
          f.linker.Class(type).static_storage[f.linker.Field(*field).slot])});
    };
    const auto int_field = [&](const std::string_view descriptor,
                               const std::string_view name) {
      const auto type = f.linker.ResolveDescriptor(descriptor);
      const auto field = f.linker.FindFieldRecursive(
          type, std::string(name), "I");
      REQUIRE(field.has_value());
      return static_cast<std::int32_t>(
          f.linker.Class(type).static_storage[f.linker.Field(*field).slot]);
    };
    CHECK(string_field("Landroid/os/Build;", "BRAND") == "unknown");
    CHECK(string_field("Landroid/os/Build;", "CPU_ABI") == "armeabi");
    CHECK(string_field("Landroid/os/Build;", "TAGS") == "release-keys");
    CHECK(string_field("Landroid/os/Build$VERSION;", "RELEASE") == "4.4.4");
    CHECK(string_field("Landroid/os/Build$VERSION;", "SDK") == "19");
    CHECK(int_field("Landroid/os/Build$VERSION;", "SDK_INT") == 19);
    CHECK(int_field("Landroid/os/Build$VERSION_CODES;", "KITKAT") == 19);
  }
}

TEST_CASE("DVM-103 all BootDex classes link and collection methods have no intrinsic overlay") {
    Dvm87Vm f;
    const auto all = f.linker.AllClasses();
    auto catalog = CoreIntrinsicCatalog();
    const auto android = AndroidIntrinsicCatalog(f.context);
    catalog.insert(catalog.end(), android.begin(), android.end());
    std::size_t count{};
    for (const auto type : all) {
        if (!f.linker.Class(type).is_boot_dex) continue;
        const auto descriptor = f.linker.Class(type).descriptor;
        CAPTURE(std::string(descriptor));
        f.linker.EnsureClassLinked(type);
        ++count;
        // Handwritten overlays remain authoritative. Compare every own method
        // with the production catalogs rather than a growing class whitelist.
        const auto check = [&](VmMethodId method) {
            const auto& linked = f.linker.Method(method);
            CAPTURE(linked.name);
            CAPTURE(linked.descriptor);
            bool overlay = false;
            for (const auto& declaration : catalog) {
                if (declaration.descriptor != descriptor) continue;
                for (const auto& candidate : declaration.methods)
                    if (candidate.name == linked.name && candidate.descriptor == linked.descriptor)
                        overlay = true;
            }
            if (overlay && !(linked.access_flags & (kAccNative | kAccAbstract)))
                CHECK(linked.kind == MethodKind::intrinsic);
            else if (!overlay && !linked.is_skeleton && !(linked.access_flags & (kAccNative | kAccAbstract))) {
                CHECK(linked.kind == MethodKind::interpreted);
                CHECK(linked.code.has_value());
            }
        };
        for (const auto method : f.linker.Class(type).own_virtual_methods) check(method);
        for (const auto method : f.linker.Class(type).own_direct_methods) check(method);
        if (!descriptor.starts_with("Ljava/util/")) continue;
        // All newly selected collection implementations are ordinary DEX.
        const auto collection = f.linker.ResolveDescriptor("Ljava/util/Collection;");
        const auto map = f.linker.ResolveDescriptor("Ljava/util/Map;");
        if (!f.linker.IsAssignable(collection, type) && !f.linker.IsAssignable(map, type)) continue;
        for (const auto method : f.linker.Class(type).own_virtual_methods)
            CHECK(f.linker.Method(method).kind != MethodKind::intrinsic);
        for (const auto method : f.linker.Class(type).own_direct_methods)
            CHECK(f.linker.Method(method).kind != MethodKind::intrinsic);
    }
    CHECK(count == ogplay::loader::ParseDex(Dvm102BootDex()).classes.size());
}

TEST_CASE("DVM-103 deferred intrinsic interfaces preserve declaration order") {
    DexClassLinker linker;
    auto catalog = CoreIntrinsicCatalog();
    auto probe = IntrinsicClassBuilder::Class("Ltest/InterfaceOrder;", "Ljava/lang/Object;",
        {"Ljava/io/Serializable;", "Ljava/lang/Cloneable;"});
    catalog.push_back(std::move(probe).Build());
    linker.RegisterIntrinsics(catalog);
    ogplay::test::BindBootDexPlatformNatives(linker);
              linker.RegisterBootDex(Dvm102BootDex());
    linker.Link();
    const auto owner = linker.ResolveDescriptor("Ltest/InterfaceOrder;");
    const auto& interfaces = linker.Class(owner).direct_interfaces;
    REQUIRE(interfaces.size() == 2);
    CHECK(linker.Class(interfaces[0]).descriptor == "Ljava/io/Serializable;");
    CHECK(linker.Class(interfaces[1]).descriptor == "Ljava/lang/Cloneable;");
}

TEST_CASE("DVM-104 tools events atomics and X500 execute API19 bytecode") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        const auto text = f.vm.NewStringUtf8("one,two,,three");
        const auto tokenizer = f.vm.NewIntrinsicInstance("Ljava/util/StringTokenizer;");
        f.Construct(tokenizer, "Ljava/util/StringTokenizer;", "(Ljava/lang/String;Ljava/lang/String;)V",
            {VmValue::Ref(text), VmValue::Ref(f.vm.NewStringUtf8(","))});
        auto result = f.Virtual(tokenizer, "countTokens", "()I"); f.RequireOk(result); CHECK(result.value.AsInt() == 3);
        result = f.Virtual(tokenizer, "nextToken", "()Ljava/lang/String;"); f.RequireOk(result); CHECK(f.vm.StringUtf8(result.value.ref) == "one");
        const auto bitset = f.vm.NewIntrinsicInstance("Ljava/util/BitSet;"); f.Construct(bitset, "Ljava/util/BitSet;", "()V");
        f.RequireOk(f.Virtual(bitset, "set", "(I)V", {VmValue::Int(130)}));
        result = f.Virtual(bitset, "nextSetBit", "(I)I", {VmValue::Int(0)}); f.RequireOk(result); CHECK(result.value.AsInt() == 130);
        result = f.Static("Ljava/util/Objects;", "equals", "(Ljava/lang/Object;Ljava/lang/Object;)Z", {VmValue::Ref(text), VmValue::Ref(text)}); f.RequireOk(result); CHECK(result.value.AsInt() == 1);
        const auto math = f.vm.NewIntrinsicInstance("Ljava/math/MathContext;"); f.Construct(math, "Ljava/math/MathContext;", "(I)V", {VmValue::Int(7)});
        result = f.Virtual(math, "getPrecision", "()I"); f.RequireOk(result); CHECK(result.value.AsInt() == 7);
        const auto atomic = f.vm.NewIntrinsicInstance("Ljava/util/concurrent/atomic/AtomicLong;");
        f.Construct(atomic, "Ljava/util/concurrent/atomic/AtomicLong;", "(J)V", {VmValue::Long(INT64_MAX)});
        result = f.Virtual(atomic, "incrementAndGet", "()J"); f.RequireOk(result); CHECK(result.value.AsLong() == INT64_MIN);
        const auto principal = f.vm.NewIntrinsicInstance("Ljavax/security/auth/x500/X500Principal;");
        f.Construct(principal, "Ljavax/security/auth/x500/X500Principal;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("CN=Alice,O=Example,C=US"))});
        result = f.Virtual(principal, "getName", "(Ljava/lang/String;)Ljava/lang/String;", {VmValue::Ref(f.vm.NewStringUtf8("CANONICAL"))});
        f.RequireOk(result); CHECK(f.vm.StringUtf8(result.value.ref) == "cn=alice,o=example,c=us");
        const auto encoded = f.Virtual(principal, "getEncoded", "()[B"); f.RequireOk(encoded);
        const auto decoded = f.vm.NewIntrinsicInstance("Ljavax/security/auth/x500/X500Principal;");
        f.Construct(decoded, "Ljavax/security/auth/x500/X500Principal;", "([B)V", {VmValue::Ref(encoded.value.ref)});
        result = f.Virtual(principal, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(decoded)}); f.RequireOk(result); CHECK(result.value.AsInt() == 1);
    }
}

TEST_CASE("DVM-104 formatters events tokenizer and key parameters use guest state") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto calls=std::make_shared<int>();
        auto listener=IntrinsicClassBuilder::Class("Ltest/PropertyListener;","Ljava/lang/Object;",{"Ljava/beans/PropertyChangeListener;"});
        listener.VirtualMethod("propertyChange","(Ljava/beans/PropertyChangeEvent;)V",[calls](IntrinsicContext&){++*calls;return VmValue::Void();});
        const std::vector<IntrinsicClassDecl> extras{std::move(listener).Build()};
        Dvm87Vm f(backend,"en","eng","USA","GMT",extras);
        const auto events=f.vm.NewIntrinsicInstance("Ljava/beans/PropertyChangeSupport;");
        const auto source=f.vm.NewIntrinsicInstance("Ljava/lang/Object;");
        f.Construct(events,"Ljava/beans/PropertyChangeSupport;","(Ljava/lang/Object;)V",{VmValue::Ref(source)});
        const auto observer=f.vm.NewIntrinsicInstance("Ltest/PropertyListener;");
        f.RequireOk(f.Virtual(events,"addPropertyChangeListener","(Ljava/beans/PropertyChangeListener;)V",{VmValue::Ref(observer)}));
        const auto fire=[&](int old,int next){return f.Virtual(events,"firePropertyChange","(Ljava/lang/String;II)V",{VmValue::Ref(f.vm.NewStringUtf8("value")),VmValue::Int(old),VmValue::Int(next)});};
        f.RequireOk(fire(1,1));CHECK(*calls==0);f.RequireOk(fire(1,2));CHECK(*calls==1);
        f.RequireOk(f.Virtual(events,"removePropertyChangeListener","(Ljava/beans/PropertyChangeListener;)V",{VmValue::Ref(observer)}));f.RequireOk(fire(2,3));CHECK(*calls==1);
        const auto choice=f.vm.NewIntrinsicInstance("Ljava/text/ChoiceFormat;");
        const auto limits = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[D"), JniPrimitiveKind::double_value, 3);
        const std::array<double, 3> bounds{0.0, 1.0, 2.0};
        for (int i = 0; i < 3; ++i) f.model.SetPrimitiveElement(limits, i, std::bit_cast<std::uint64_t>(bounds[static_cast<std::size_t>(i)]));
        const auto labels = f.model.NewObjectArray(f.linker.ResolveDescriptor("[Ljava/lang/String;"), f.linker.ResolveDescriptor("Ljava/lang/String;"), 3);
        for (int i = 0; i < 3; ++i) f.model.SetObjectElement(labels, i, f.vm.NewStringUtf8(std::array{"none", "one", "many"}[static_cast<std::size_t>(i)]));
        f.Construct(choice,"Ljava/text/ChoiceFormat;","([D[Ljava/lang/String;)V",{VmValue::Ref(limits), VmValue::Ref(labels)});
        const auto rendered=f.Virtual(choice,"format","(J)Ljava/lang/String;",{VmValue::Long(2)});f.RequireOk(rendered);CHECK(f.vm.StringUtf8(rendered.value.ref)=="many");
        const auto format=f.vm.NewIntrinsicInstance("Ljava/text/MessageFormat;");
        f.Construct(format,"Ljava/text/MessageFormat;","(Ljava/lang/String;)V",{VmValue::Ref(f.vm.NewStringUtf8("Hello {0}"))});
        const auto args=f.model.NewObjectArray(f.linker.ResolveDescriptor("[Ljava/lang/Object;"),f.linker.ResolveDescriptor("Ljava/lang/Object;"),1);
        f.model.SetObjectElement(args,0,f.vm.NewStringUtf8("world"));
        const auto message=f.Virtual(format,"format","(Ljava/lang/Object;)Ljava/lang/String;",{VmValue::Ref(args)});f.RequireOk(message);CHECK(f.vm.StringUtf8(message.value.ref)=="Hello world");
        const auto reader=f.vm.NewIntrinsicInstance("Ljava/io/StringReader;");f.Construct(reader,"Ljava/io/StringReader;","(Ljava/lang/String;)V",{VmValue::Ref(f.vm.NewStringUtf8("word 42"))});
        const auto tokenizer=f.vm.NewIntrinsicInstance("Ljava/io/StreamTokenizer;");f.Construct(tokenizer,"Ljava/io/StreamTokenizer;","(Ljava/io/Reader;)V",{VmValue::Ref(reader)});
        const auto token=f.Virtual(tokenizer,"nextToken","()I");f.RequireOk(token);CHECK(token.value.AsInt()==-3);
        const auto keybytes=f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"),JniPrimitiveKind::byte,3);
        f.model.WriteByteRegion(keybytes,0,std::array{std::byte{1},std::byte{2},std::byte{3}});
        for(const auto* owner:{"Ljavax/crypto/spec/IvParameterSpec;","Ljavax/crypto/spec/SecretKeySpec;","Ljava/security/spec/PKCS8EncodedKeySpec;","Ljava/security/spec/X509EncodedKeySpec;"}) {
            CAPTURE(owner);const auto key=f.vm.NewIntrinsicInstance(owner);
            const bool secret=std::string_view(owner).find("SecretKey")!=std::string_view::npos;
            if(secret)f.Construct(key,owner,"([BLjava/lang/String;)V",{VmValue::Ref(keybytes),VmValue::Ref(f.vm.NewStringUtf8("AES"))});
            else f.Construct(key,owner,"([B)V",{VmValue::Ref(keybytes)});
            const auto copied=f.Virtual(key,std::string_view(owner).find("IvParameter")!=std::string_view::npos?"getIV":"getEncoded","()[B");f.RequireOk(copied);CHECK(copied.value.ref!=keybytes);CHECK(f.model.ReadByteRegion(copied.value.ref,0,3)==f.model.ReadByteRegion(keybytes,0,3));
        }
    }
}

TEST_CASE("DVM-276 BootDex exact member sources execute on both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        DexClassLinker linker;
        linker.RegisterIntrinsics(CoreIntrinsicCatalog());
        linker.RegisterIntrinsics(AndroidIntrinsicCatalog(std::make_shared<DexVmAndroidContext>()));
        auto builder = IntrinsicClassBuilder::Class("Ltest/MemberComposition;");
        builder.StaticMethod("host", "()I", [](IntrinsicContext&) { return VmValue::Int(29); });
        // This legacy implementation must lose to the explicit ROM source.
        builder.StaticMethod("rom", "()I", [](IntrinsicContext&) { return VmValue::Int(-100); });
        builder.StaticMethod("missing", "()I", [](IntrinsicContext&) { return VmValue::Int(-200); });
        auto declaration = std::move(builder).Build();
        int legacy_clinit_calls = 0;
        declaration.clinit_implementation = [&](IntrinsicContext&) {
            ++legacy_clinit_calls;
            return VmValue::Void();
        };
        linker.RegisterIntrinsics(std::array{declaration});
        const auto read = [](const std::string& name) {
            std::ifstream input(std::filesystem::path(OGPLAY_DEXVM_FIXTURE_DIR) / name, std::ios::binary);
            REQUIRE(input.good());
            return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(input), {}};
        };
        const auto jar = read("member-sources.jar");
        const auto archive = std::span<const std::byte>{reinterpret_cast<const std::byte*>(jar.data()), jar.size()};
        const auto dex_entry =
            ogplay::loader::ReadApkEntry(archive, ogplay::loader::ParseApkArchive(archive), "classes.dex");
        std::vector<std::uint8_t> dex(dex_entry.size());
        std::memcpy(dex.data(), dex_entry.data(), dex.size());
        linker.RegisterBootDex(std::move(dex), DexClassLinker::BootDexMemberSources(archive));
        linker.Link();
        ogplay::runtime::HostCopyJavaPayloadProvider string_payload;
        JniStringStore strings{string_payload};
        ogplay::runtime::HostCopyJavaPayloadProvider array_payload;
        JniPrimitiveArrayStore arrays{array_payload};
        JavaObjectModel model{strings, arrays};
        ogplay::core::CapabilityLedger ledger;
        Interpreter vm{linker, model, nullptr, ledger, InterpreterConfig{.backend = backend}};
        const auto owner = linker.ResolveDescriptor("Ltest/MemberComposition;");
        const auto call = [&](const std::string& name) {
            const auto id = linker.FindDirectMethod(owner, name, "()I");
            REQUIRE(id.has_value());
            return vm.Call(*id, {});
        };
        const auto rom = call("rom");
        REQUIRE_FALSE(rom.exception.IsValid());
        CHECK(legacy_clinit_calls == 0);
        CHECK(rom.value.AsInt() == 17);  // encoded 5 is overwritten by the ROM clinit.
        CHECK(call("host").value.AsInt() == 29);
        CHECK(call("combined").value.AsInt() == 46);
        const auto missing = call("missing");
        REQUIRE(missing.exception.IsValid());
        CHECK(linker.Class(missing.exception_class).descriptor == "Ljava/lang/UnsatisfiedLinkError;");
        CHECK(call("unavailable").exception.IsValid());
        const auto hits = ledger.Unimplemented();
        CHECK(std::any_of(hits.begin(), hits.end(), [](const auto& hit) {
            return hit.id.find("MemberComposition;") != std::string::npos &&
                   hit.id.find("missing") != std::string::npos;
        }));
        CHECK(linker.Class(owner).own_static_fields.size() == 1);
        const auto id = linker.FindDirectMethod(owner, "rom", "()I");
        CHECK(linker.Method(*id).kind == MethodKind::interpreted);
        CHECK(linker.Method(*linker.FindDirectMethod(owner, "host", "()I")).kind == MethodKind::intrinsic);
        CHECK(linker.Method(*linker.FindDirectMethod(owner, "missing", "()I")).is_skeleton);
    }
}

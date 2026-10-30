#include "ogplay/runtime/integration/java_payload_provider.h"
#include "ogplay/runtime/jni/host_copy_payload_provider.h"
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include "ogplay/hal/clock.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include <set>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/owned_state_table.h"
#include "ogplay/runtime/dexvm/zip_runtime.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni/jni_class_registry.h"

DOCTEST_REGISTER_EXCEPTION_TRANSLATOR(const ogplay::runtime::dexvm::VmJavaThrow& error) {
    return doctest::String((error.descriptor + ": " + error.message).c_str());
}

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-214 native ZIP admission placeholders do not require BootDex metadata") {
    ogplay::runtime::HostCopyJavaPayloadProvider strings_payload;
    JniStringStore strings{strings_payload};
    ogplay::runtime::HostCopyJavaPayloadProvider arrays_payload;
    JniPrimitiveArrayStore arrays{arrays_payload};
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(IntrinsicClassBuilder::RootClass("Ljava/lang/Object;")).Build());
    for (const auto owner : {"Lorg/ogplay/zip/NativeZip;", "Ljava/util/zip/Deflater;", "Ljava/util/zip/Inflater;"}) {
        auto builder = IntrinsicClassBuilder::Class(owner);
        builder.AdmitBootNativeMethods();
        catalog.push_back(std::move(builder).Build());
    }
    linker.RegisterIntrinsics(catalog);
    linker.Link();
    ogplay::core::CapabilityLedger ledger;
    Interpreter vm(linker, model, nullptr, ledger);
    CHECK(vm.GuestNativeResourceCount() == 0);
}

struct GcVm final {
    ogplay::memory::AddressSpace memory;
    ogplay::core::CapabilityLedger payload_ledger;
    GuestJavaPayloadProvider arrays_payload{memory, payload_ledger};
    JniStringStore strings{arrays_payload};
    JniPrimitiveArrayStore arrays{arrays_payload};
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    std::shared_ptr<std::uint32_t> destructor_calls = std::make_shared<std::uint32_t>(0);
    Interpreter vm;

    GcVm(const std::vector<IntrinsicClassDecl>& extras = {},
         const InterpreterBackend backend = InterpreterBackend::switch_dispatch,
         NativeMethodBridge* native_bridge = nullptr)
        : vm(
              [this, &extras]() -> DexClassLinker& {
                  auto catalog = CoreIntrinsicCatalog();
                  auto builder = IntrinsicClassBuilder::Class("Lgc/RootBox;");
                  builder.InstanceField("child", "Ljava/lang/Object;");
                  builder.StaticField("root", "Ljava/lang/Object;");
                  catalog.push_back(std::move(builder).Build());
                  auto host = IntrinsicClassBuilder::Class("Lgc/HostBox;");
                  host.HostStateDestructor([calls = destructor_calls](std::uint64_t state) {
                      CHECK(state == 99);
                      ++*calls;
                  });
                  catalog.push_back(std::move(host).Build());
                  linker.RegisterIntrinsics(catalog);
                  linker.RegisterIntrinsics(extras);
                  ogplay::test::RegisterBootDex(linker);
                  linker.Link();
                  return linker;
              }(),
              model, native_bridge, ledger, InterpreterConfig{.backend = backend}) {}
};

TEST_CASE("DexVM GC root surface enumerates permanent static JNI and session roots") {
    GcVm fixture;
    const auto static_root = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.vm.SetIntrinsicStaticRef("Lgc/RootBox;", "root", "Ljava/lang/Object;", static_root);
    const auto intern_root = fixture.model.InternString(u"permanent");
    const auto jni_root = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto session_root = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.vm.SetGcIntegration(
        {[identity = fixture.model.ToIdentity(jni_root)](const auto& visit) { visit(identity); },
         {},
         [session_root](const VmRootVisitor& visit) { visit(session_root); }});

    std::set<std::uint32_t> roots;
    fixture.vm.VisitRoots([&](const VmObjectRef ref) {
        if (ref.IsValid()) roots.insert(ref.Value());
    });

    CHECK(roots.contains(static_root.Value()));
    CHECK(roots.contains(intern_root.Value()));
    CHECK(roots.contains(jni_root.Value()));
    CHECK(roots.contains(session_root.Value()));
    const auto root_box = fixture.linker.FindClass("Lgc/RootBox;");
    REQUIRE(root_box.has_value());
    CHECK(fixture.linker.Class(*root_box).static_ref_slots == std::vector<std::uint16_t>{0});
    CHECK(fixture.vm.RegisteredIntrinsicSideTableCount() > 0);
}

TEST_CASE("JNI GC roots include local and global but exclude weak global") {
    JniReferenceTable references;
    references.AttachThread(7);
    const JniObjectIdentity local{JniObjectDomain::dex_vm, 11};
    const JniObjectIdentity global{JniObjectDomain::dex_vm, 12};
    const JniObjectIdentity weak{JniObjectDomain::dex_vm, 13};
    static_cast<void>(references.NewLocal(7, local));
    static_cast<void>(references.NewGlobal(global));
    static_cast<void>(references.NewWeakGlobal(weak));

    std::vector<JniObjectIdentity> roots;
    references.VisitRoots([&](const JniObjectIdentity value) { roots.push_back(value); });
    CHECK(std::ranges::find(roots, local) != roots.end());
    CHECK(std::ranges::find(roots, global) != roots.end());
    CHECK(std::ranges::find(roots, weak) == roots.end());
}

TEST_CASE("DexVM marker traces exact object edges and intrinsic side tables") {
    GcVm fixture;
    const auto root = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto field_child = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.model.InstanceSlots(root)[0] = Slot{field_child.Value(), SlotTag::ref};
    const auto array_class = fixture.linker.ResolveDescriptor("[Ljava/lang/Object;");
    const auto object_class = fixture.linker.FindClass("Ljava/lang/Object;");
    REQUIRE(object_class.has_value());
    const auto array = fixture.model.NewObjectArray(array_class, *object_class, 1);
    fixture.model.SetObjectElement(array, 0, root);
    const auto side_child = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.vm.RegisterIntrinsicStateTable({"marker-edge",
                                            [root, side_child](VmObjectRef owner, const VmRootVisitor& visit) {
                                                if (owner == root) visit(side_child);
                                            },
                                            [](VmObjectRef) {},
                                            {}});
    const auto garbage = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.vm.SetGcIntegration({{}, {}, [array](const VmRootVisitor& visit) { visit(array); }});

    const auto marked = fixture.vm.MarkReachable();
    CHECK(marked.IsMarked(array));
    CHECK(marked.IsMarked(root));
    CHECK(marked.IsMarked(field_child));
    CHECK(marked.IsMarked(side_child));
    CHECK_FALSE(marked.IsMarked(garbage));
    CHECK(marked.garbage_bytes == 40);
    CHECK(marked.garbage_objects == 1);
}

TEST_CASE("DexVM intrinsic state tables register trace sweep and clone hooks") {
    GcVm fixture;
    const auto initial_tables = fixture.vm.RegisteredIntrinsicSideTableCount();
    auto state = std::make_shared<std::unordered_map<std::uint32_t, VmObjectRef>>();
    fixture.vm.RegisterIntrinsicStateTable({"gc-test-state",
                                            [state](const VmObjectRef owner, const VmRootVisitor& visit) {
                                                const auto found = state->find(owner.Value());
                                                if (found != state->end()) visit(found->second);
                                            },
                                            [state](const VmObjectRef owner) { state->erase(owner.Value()); },
                                            [state](const VmObjectRef source, const VmObjectRef clone) {
                                                const auto found = state->find(source.Value());
                                                if (found != state->end()) {
                                                    (*state)[clone.Value()] = found->second;
                                                }
                                            }});
    CHECK(fixture.vm.RegisteredIntrinsicSideTableCount() == initial_tables + 1);

    const auto owner = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto child = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto garbage_io = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto garbage_zip = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.vm.IO().SetInput(garbage_io, {{std::byte{0x2a}}, 0, false});
    fixture.vm.ZIP().Open(
        garbage_zip, {std::byte{0x50}, std::byte{0x4b}, std::byte{0x05}, std::byte{0x06}, std::byte{0}, std::byte{0},
                      std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0}, std::byte{0},
                      std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0}, std::byte{0},
                      std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0}});
    (*state)[owner.Value()] = child;
    const auto source_hash = fixture.model.IdentityHashCode(owner);
    const auto clone = fixture.vm.CloneObject(owner);
    CHECK(fixture.model.IdentityHashCode(clone) != source_hash);
    CHECK(fixture.model.IdentityHashCode(owner) == source_hash);
    REQUIRE(state->contains(clone.Value()));
    CHECK(state->at(clone.Value()) == child);

    fixture.vm.SetGcIntegration({{}, {}, [owner](const VmRootVisitor& visit) { visit(owner); }});
    const auto marked = fixture.vm.MarkReachable();
    CHECK(marked.IsMarked(owner));
    CHECK(marked.IsMarked(child));
    CHECK_FALSE(marked.IsMarked(clone));
    CHECK_FALSE(marked.IsMarked(garbage_io));
    CHECK_FALSE(marked.IsMarked(garbage_zip));
    static_cast<void>(fixture.vm.SweepGarbage(marked));
    CHECK_FALSE(state->contains(clone.Value()));
    CHECK(fixture.vm.IO().FindInput(garbage_io) == nullptr);
    CHECK_FALSE(fixture.vm.ZIP().Contains(garbage_zip));
}

TEST_CASE("OwnedStateTable traces children sweeps owners and refuses stale reuse") {
    GcVm fixture;
    OwnedStateTable<VmObjectRef> states("gc-owned-state", OwnedStateTracePolicy::trace_guest_references,
                                        OwnedStateClonePolicy::do_not_clone,
                                        [](const VmObjectRef& child, const VmRootVisitor& visit) {
                                            if (child.IsValid()) visit(child);
                                        });
    fixture.vm.RegisterIntrinsicStateTable(states.Hooks());

    const auto owner = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto child = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    states.InsertOrAssign(owner, child);
    fixture.vm.SetGcIntegration({{}, {}, [owner](const VmRootVisitor& visit) { visit(owner); }});
    const auto alive = fixture.vm.MarkReachable();
    CHECK(alive.IsMarked(owner));
    CHECK(alive.IsMarked(child));

    fixture.vm.SetGcIntegration({});
    const auto swept = fixture.vm.CollectGarbage();
    CHECK(swept.freed_objects == 2);
    CHECK(states.Size() == 0);

    const auto reused = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const bool reused_old_handle = reused.Value() == owner.Value() || reused.Value() == child.Value();
    CHECK(reused_old_handle);
    CHECK_FALSE(states.Contains(reused));

    CHECK_THROWS_AS((OwnedStateTable<VmObjectRef>("missing-trace", OwnedStateTracePolicy::trace_guest_references)),
                    DexVmError);
}

TEST_CASE("DexVM and JNI share one object array identity and element store") {
    ogplay::runtime::HostCopyJavaPayloadProvider strings_payload;
    JniStringStore strings{strings_payload};
    ogplay::runtime::HostCopyJavaPayloadProvider primitives_payload;
    JniPrimitiveArrayStore primitives{primitives_payload};
    JniClassRegistry jni_classes;
    const auto jni_object = jni_classes.RegisterClass({"java/lang/Object"});
    const auto jni_box = jni_classes.RegisterClass({"gc/Box", std::string{"java/lang/Object"}});
    JniGuestObjectRegistry objects(jni_classes);
    auto& object_arrays = objects.ObjectArrays();

    DexClassLinker linker;
    auto catalog = CoreIntrinsicCatalog();
    catalog.push_back(std::move(IntrinsicClassBuilder::Class("Lgc/Box;")).Build());
    linker.RegisterIntrinsics(catalog);
    ogplay::test::RegisterBootDex(linker);
    linker.Link();
    const auto dex_object = linker.FindClass("Ljava/lang/Object;");
    const auto dex_box = linker.FindClass("Lgc/Box;");
    REQUIRE(dex_object.has_value());
    REQUIRE(dex_box.has_value());
    const auto dex_array = linker.ResolveDescriptor("[Lgc/Box;");

    const auto publish_class = [&](const DexClassId java_class) {
        return java_class == *dex_box ? jni_box : jni_object;
    };
    const auto resolve_class = [&](const JniObjectIdentity java_class) {
        return java_class == jni_box ? *dex_box : *dex_object;
    };
    JavaObjectModel model(
        strings, primitives, {},
        {&object_arrays, publish_class, resolve_class, {}, [&](const JniObjectIdentity element_class) {
             return std::pair{dex_array, resolve_class(element_class)};
         }});

    const auto element = model.NewInstance(*dex_box, 0);
    objects.Register(model.ToIdentity(element), jni_box);
    CHECK(objects.ClassOf(model.ToIdentity(element)) == jni_box);
    const auto array = model.NewObjectArray(dex_array, *dex_box, 1);
    model.SetObjectElement(array, 0, element);
    const auto identity = model.ToIdentity(array);
    REQUIRE(object_arrays.Contains(identity));
    REQUIRE(object_arrays.Get(identity, 0).has_value());
    CHECK(object_arrays.Get(identity, 0)->object == model.ToIdentity(element));

    const auto imported_identity = object_arrays.New(jni_box, 1);
    object_arrays.Set(imported_identity, 0, JniObjectValue{model.ToIdentity(element), jni_box});
    const auto imported = model.FromIdentity(imported_identity);
    CHECK(model.Kind(imported) == VmObjectKind::object_array);
    CHECK(model.ObjectClass(imported) == dex_array);
    CHECK(model.GetObjectElement(imported, 0) == element);
}

TEST_CASE("DexVM sweeper releases stores reuses handles and is idempotent") {
    GcVm fixture;
    const auto survivor = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto dead = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.model.InstanceSlots(dead)[0] = {survivor.Value(), SlotTag::ref};
    const auto dead_string = fixture.model.NewString(u"dead");
    const auto dead_string_hash = fixture.model.IdentityHashCode(dead_string);
    const auto string_identity = fixture.model.ToIdentity(dead_string);
    const auto before = fixture.model.AllocatedBytes();
    std::vector<JniObjectIdentity> weak_clears;
    fixture.vm.SetGcIntegration({{},
                                 [&](const JniObjectIdentity identity) { weak_clears.push_back(identity); },
                                 [survivor](const VmRootVisitor& visit) { visit(survivor); }});

    const auto first = fixture.vm.CollectGarbage();
    CHECK(first.freed_objects == 2);
    CHECK(first.freed_bytes == 80);
    CHECK(fixture.model.AllocatedBytes() == before - 80);
    CHECK_FALSE(fixture.model.IsValidRef(dead));
    CHECK_THROWS_AS(static_cast<void>(fixture.model.Kind(dead)), DexVmError);
    CHECK_THROWS_AS(static_cast<void>(fixture.strings.Length(string_identity)), JniStringError);
    CHECK(std::ranges::find(weak_clears, string_identity) != weak_clears.end());

    const auto reused = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    CHECK(reused == dead_string);
    CHECK(fixture.model.IdentityHashCode(reused) != dead_string_hash);
    CHECK(fixture.model.InstanceSlots(reused)[0].bits == 0);
    fixture.vm.SetGcIntegration({{}, {}, [survivor, reused](const VmRootVisitor& visit) {
                                     visit(survivor);
                                     visit(reused);
                                 }});
    const auto second = fixture.vm.CollectGarbage();
    CHECK(second.freed_objects == 0);
    CHECK(second.freed_bytes == 0);
}

TEST_CASE("DexVM host state destructor runs exactly once only after death") {
    GcVm fixture;
    const auto host_class = fixture.linker.FindClass("Lgc/HostBox;");
    REQUIRE(host_class.has_value());
    const auto live = fixture.model.NewHostBacked(*host_class, 99);
    static_cast<void>(fixture.model.NewHostBacked(*host_class, 99));
    fixture.vm.SetGcIntegration({{}, {}, [live](const VmRootVisitor& visit) { visit(live); }});

    const auto first = fixture.vm.CollectGarbage();
    CHECK(first.freed_objects == 1);
    CHECK(*fixture.destructor_calls == 1);
    const auto second = fixture.vm.CollectGarbage();
    CHECK(second.freed_objects == 0);
    CHECK(*fixture.destructor_calls == 1);

    fixture.vm.SetGcIntegration({});
    const auto third = fixture.vm.CollectGarbage();
    CHECK(third.freed_objects == 1);
    CHECK(*fixture.destructor_calls == 2);
}

}  // namespace

TEST_CASE("DVM-108 field owned native resources follow mutation aliases and retry") {
    std::vector<std::int64_t> released;
    bool reject = false;
    auto owner = IntrinsicClassBuilder::Class("Lgc/NativeOwner;");
    owner.InstanceField("ctx", "J");
    owner.InstanceField("other", "J");
    owner.InstanceField("bad", "I");
    owner.StaticField("global", "J");
    owner.StaticMethod("cleanup", "(J)V", [&](IntrinsicContext& c) {
        if (reject) throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "retry cleanup"};
        released.push_back(c.arguments[0].AsLong());
        return VmValue::Void();
    });
    owner.StaticMethod("different", "(J)V", [](IntrinsicContext&) { return VmValue::Void(); });
    owner.StaticMethod("invalid", "()V", [](IntrinsicContext&) { return VmValue::Void(); });
    GcVm f({std::move(owner).Build()});
    const auto type = f.linker.ResolveDescriptor("Lgc/NativeOwner;");
    const auto field = *f.linker.FindFieldRecursive(type, "ctx", "J");
    const auto other = *f.linker.FindFieldRecursive(type, "other", "J");
    const auto cleanup = *f.linker.FindDirectMethod(type, "cleanup", "(J)V");
    const auto different = *f.linker.FindDirectMethod(type, "different", "(J)V");
    const auto invalid = *f.linker.FindDirectMethod(type, "invalid", "()V");
    CHECK_THROWS_AS(f.vm.TrackGuestNativeResourceField(*f.linker.FindFieldRecursive(type, "bad", "I"), cleanup),
                    DexVmError);
    CHECK_THROWS_AS(f.vm.TrackGuestNativeResourceField(*f.linker.FindFieldRecursive(type, "global", "J"), cleanup),
                    DexVmError);
    CHECK_THROWS_AS(f.vm.TrackGuestNativeResourceField(field, invalid), DexVmError);
    f.vm.TrackGuestNativeResourceField(field, cleanup);
    f.vm.TrackGuestNativeResourceField(field, cleanup);
    CHECK_THROWS_AS(f.vm.TrackGuestNativeResourceField(field, different), DexVmError);
    f.vm.TrackGuestNativeResourceField(other, cleanup);
    const auto a = f.vm.NewIntrinsicInstance("Lgc/NativeOwner;");
    const auto a_root = f.vm.ProtectReferences(std::array{a});
    const auto write = [&](VmObjectRef object, const char* name, std::int64_t value) {
        const auto slot = f.linker.Field(*f.linker.FindFieldRecursive(type, name, "J")).slot;
        auto slots = f.model.InstanceSlots(object);
        const auto bits = static_cast<std::uint64_t>(value);
        slots[slot].bits = static_cast<std::uint32_t>(bits);
        slots[slot + 1].bits = static_cast<std::uint32_t>(bits >> 32U);
    };
    write(a, "ctx", INT64_C(0x123456789));
    const auto b = f.vm.NewIntrinsicInstance("Lgc/NativeOwner;");
    write(b, "other", INT64_C(0x123456789));
    CHECK(f.vm.GuestNativeResourceCount() == 1);
    static_cast<void>(f.vm.CollectGarbage());
    CHECK(released.empty());
    // Java destroyed the old token and wrote a new one: GC must see only the field.
    write(a, "ctx", 42);
    f.vm.ReleaseGuestNativeResources(true);
    CHECK(released == std::vector<std::int64_t>{42});
    CHECK(f.vm.GuestNativeResourceCount() == 0);
    f.vm.ReleaseGuestNativeResources(true);
    CHECK(released.size() == 1);
    const auto c = f.vm.NewIntrinsicInstance("Lgc/NativeOwner;");
    write(c, "ctx", 77);
    static_cast<void>(f.vm.CloneObject(c));
    reject = true;
    CHECK_THROWS_AS(static_cast<void>(f.vm.CollectGarbage()), VmJavaThrow);
    CHECK(f.vm.GuestNativeResourceCount() == 1);
    reject = false;
    f.vm.ReleaseGuestNativeResources();
    CHECK(released == std::vector<std::int64_t>{42, 77});
    CHECK(f.vm.GuestNativeResourceCount() == 0);
}

TEST_CASE("DVM-109 soft references retain caches until pressure and interned strings stay canonical") {
    GcVm f;
    const auto construct = [&](const char* type, const char* descriptor, std::vector<VmValue> args) {
        const auto object = f.vm.NewIntrinsicInstance(type);
        args.insert(args.begin(), VmValue::Ref(object));
        const auto method = f.linker.FindDirectMethod(f.linker.ResolveDescriptor(type), "<init>", descriptor);
        REQUIRE(method);
        const auto result = f.vm.Call(*method, args);
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return object;
    };
    const auto queue = construct("Ljava/lang/ref/ReferenceQueue;", "()V", {});
    const auto value = f.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto soft = construct("Ljava/lang/ref/SoftReference;", "(Ljava/lang/Object;Ljava/lang/ref/ReferenceQueue;)V",
                                {VmValue::Ref(value), VmValue::Ref(queue)});
    const auto roots = f.vm.ProtectReferences(std::array{soft, queue});
    static_cast<void>(f.vm.CollectGarbage("ordinary"));
    CHECK(f.model.IsValidRef(value));
    const auto referent = f.linker.FindFieldRecursive(f.linker.ResolveDescriptor("Ljava/lang/ref/Reference;"),
                                                      "referent", "Ljava/lang/Object;");
    REQUIRE(referent);
    CHECK(f.model.InstanceSlots(soft)[f.linker.Field(*referent).slot].bits == value.Value());
    static_cast<void>(f.vm.CollectGarbage("pressure", true));
    CHECK_FALSE(f.model.IsValidRef(value));
    CHECK(f.model.InstanceSlots(soft)[f.linker.Field(*referent).slot].bits == 0);
    const auto poll_slot = f.linker.FindVtableIndex(f.model.ObjectClass(queue), "poll", "()Ljava/lang/ref/Reference;");
    REQUIRE(poll_slot);
    const auto polled =
        f.vm.Call(f.linker.Class(f.model.ObjectClass(queue)).vtable[*poll_slot], std::array{VmValue::Ref(queue)});
    REQUIRE(!polled.exception.IsValid());
    CHECK(polled.value.ref == soft);
    const auto one = f.model.NewString(u"dvm109-weak-intern-😀");
    const auto two = f.model.NewString(u"dvm109-weak-intern-😀");
    const auto string_type = f.linker.ResolveDescriptor("Ljava/lang/String;");
    const auto intern_slot = f.linker.FindVtableIndex(string_type, "intern", "()Ljava/lang/String;");
    REQUIRE(intern_slot);
    const auto intern = f.linker.Class(string_type).vtable[*intern_slot];
    CHECK(f.vm.Call(intern, std::array{VmValue::Ref(one)}).value.ref == one);
    CHECK(f.vm.Call(intern, std::array{VmValue::Ref(two)}).value.ref == one);
    static_cast<void>(f.vm.CollectGarbage());
    CHECK_FALSE(f.model.IsValidRef(one));
    CHECK_FALSE(f.model.IsValidRef(two));
    const auto fresh = f.model.NewString(u"dvm109-weak-intern-😀");
    CHECK(f.model.InternString(fresh) == fresh);
    CHECK(f.model.InternString(u"dvm109-weak-intern-😀") == fresh);  // Literal promotion.
    static_cast<void>(f.vm.CollectGarbage());
    CHECK(f.model.IsValidRef(fresh));
}

TEST_CASE("WU7-3 System.gc clears weak interns while retaining literals in both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        GcVm f({}, backend);
        const auto literal = f.model.InternString(u"wu7-3-constant");
        const auto value = f.model.NewString(u"wu7-3-dynamic");
        CHECK(f.model.InternString(value) == value);
        const auto weak = f.vm.NewIntrinsicInstance("Ljava/lang/ref/WeakReference;");
        const auto root = f.vm.ProtectReferences(std::array{weak});
        const auto ctor = f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Ljava/lang/ref/WeakReference;"),
                                                    "<init>", "(Ljava/lang/Object;)V");
        REQUIRE(ctor.has_value());
        REQUIRE_FALSE(f.vm.Call(*ctor, std::array{VmValue::Ref(weak), VmValue::Ref(value)}).exception.IsValid());
        const auto gc = f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Ljava/lang/System;"), "gc", "()V");
        REQUIRE(gc.has_value());
        REQUIRE_FALSE(f.vm.Call(*gc, {}).exception.IsValid());
        CHECK_FALSE(f.model.IsValidRef(value));
        CHECK(f.model.IsValidRef(literal));
        const auto type = f.model.ObjectClass(weak);
        const auto get = f.linker.FindVtableIndex(type, "get", "()Ljava/lang/Object;");
        REQUIRE(get.has_value());
        const auto result = f.vm.Call(f.linker.Class(type).vtable[*get], std::array{VmValue::Ref(weak)});
        REQUIRE_FALSE(result.exception.IsValid());
        CHECK_FALSE(result.value.ref.IsValid());
    }
}

TEST_CASE("WU7-4 heap queries and VMRuntime share live heap configuration on both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        GcVm f({}, backend);
        VmThreadRuntime threads(f.vm);
        const auto stat = [&](const char* owner, const char* name, const char* signature,
                              std::vector<VmValue> args = {}) {
            const auto method = f.linker.FindDirectMethod(f.linker.ResolveDescriptor(owner), name, signature);
            REQUIRE(method);
            const auto result = f.vm.Call(*method, args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto on = [&](VmObjectRef receiver, const char* name, const char* signature,
                            std::vector<VmValue> args = {}) {
            const auto type = f.model.ObjectClass(receiver);
            const auto slot = f.linker.FindVtableIndex(type, name, signature);
            REQUIRE(slot);
            args.insert(args.begin(), VmValue::Ref(receiver));
            const auto result = f.vm.Call(f.linker.Class(type).vtable[*slot], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto runtime = stat("Ljava/lang/Runtime;", "getRuntime", "()Ljava/lang/Runtime;").ref;
        const auto root = f.vm.ProtectReferences(std::array{runtime});
        CHECK(on(runtime, "maxMemory", "()J").AsLong() == static_cast<std::int64_t>(f.model.HeapGrowthLimitBytes()));
        CHECK(on(runtime, "totalMemory", "()J").AsLong() - on(runtime, "freeMemory", "()J").AsLong() ==
              static_cast<std::int64_t>(f.model.AllocatedBytes()));
        const auto before = on(runtime, "freeMemory", "()J").AsLong();
        const auto bytes = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte, 4096);
        CHECK(on(runtime, "freeMemory", "()J").AsLong() < before);
        CHECK(f.model.IsValidRef(bytes));
        const auto specific = stat("Ldalvik/system/VMRuntime;", "getRuntime", "()Ldalvik/system/VMRuntime;").ref;
        CHECK(stat("Ldalvik/system/VMRuntime;", "getRuntime", "()Ldalvik/system/VMRuntime;").ref == specific);
        CHECK(on(specific, "getTargetHeapUtilization", "()F").AsFloat() == doctest::Approx(0.75));
        CHECK(on(specific, "setTargetHeapUtilization", "(F)F", {VmValue::Float(0.6F)}).AsFloat() ==
              doctest::Approx(0.75));
        CHECK(on(specific, "getTargetHeapUtilization", "()F").AsFloat() == doctest::Approx(614.0 / 1024.0));
        CHECK(f.model.TargetHeapUtilization() == doctest::Approx(614.0 / 1024.0));
        const auto setter = f.linker.FindVtableIndex(f.model.ObjectClass(specific), "setTargetHeapUtilization", "(F)F");
        REQUIRE(setter);
        const auto invalid = f.vm.Call(f.linker.Class(f.model.ObjectClass(specific)).vtable[*setter],
                                       std::array{VmValue::Ref(specific), VmValue::Float(0.0F)});
        REQUIRE(invalid.exception.IsValid());
        CHECK(f.linker.Class(invalid.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        on(specific, "registerNativeAllocation", "(I)V", {VmValue::Int(4096)});
        CHECK(f.model.NativeAllocatedBytes() == 4096);
        on(specific, "registerNativeFree", "(I)V", {VmValue::Int(1024)});
        CHECK(f.model.NativeAllocatedBytes() == 3072);
        // API19's deprecated external-allocation protocol was removed in 3.0.
        CHECK(on(specific, "trackExternalAllocation", "(J)Z", {VmValue::Long(8192)}).AsInt() == 1);
        CHECK(f.model.NativeAllocatedBytes() == 3072);
        on(specific, "clearGrowthLimit", "()V");
        CHECK(on(runtime, "maxMemory", "()J").AsLong() == static_cast<std::int64_t>(f.model.MaximumHeapBytes()));
    }
}

TEST_CASE("WU7-4 finalizer daemon preserves resurrection swallows exceptions and finalizes once") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto calls = std::make_shared<std::atomic<int>>(0);
        auto worker = std::make_shared<std::atomic<bool>>(false);
        auto declaration = IntrinsicClassBuilder::Class("Lgc/Finalizable;");
        declaration.StaticField("resurrected", "Ljava/lang/Object;");
        declaration.OverrideMethod(
            "finalize", "()V",
            [calls, worker](IntrinsicContext& c) -> VmValue {
                ++*calls;
                *worker = c.vm.Threads().CurrentThreadObject().IsValid() &&
                          c.vm.Threads().ThreadId(c.vm.Threads().CurrentThreadObject()) != 1;
                const auto type = c.vm.Model().ObjectClass(c.receiver);
                const auto field = c.vm.Linker().FindFieldRecursive(type, "resurrected", "Ljava/lang/Object;");
                REQUIRE(field);
                c.vm.Linker().MutableClass(type).static_storage[c.vm.Linker().Field(*field).slot] = c.receiver.Value();
                throw VmJavaThrow{"Ljava/lang/RuntimeException;", "finalizer test exception"};
            },
            kAccProtected);
        GcVm f({std::move(declaration).Build()}, backend);
        VmThreadRuntime threads(f.vm);
        f.vm.Monitors().SetTimeSource(
            [] { return static_cast<std::int64_t>(ogplay::hal::Clock::SteadyTimestampNs() / 1000000U); });
        const auto object = f.vm.NewIntrinsicInstance("Lgc/Finalizable;");
        const auto identity = f.model.ToIdentity(object);
        static_cast<void>(f.vm.CollectGarbage("finalizable-unreachable"));
        f.vm.RunFinalization();
        CHECK(calls->load() == 1);
        CHECK(worker->load());
        CHECK(f.model.FindIdentity(identity).IsValid());
        const auto type = f.linker.ResolveDescriptor("Lgc/Finalizable;");
        const auto field = f.linker.FindFieldRecursive(type, "resurrected", "Ljava/lang/Object;");
        REQUIRE(field);
        f.linker.MutableClass(type).static_storage[f.linker.Field(*field).slot] = 0;
        static_cast<void>(f.vm.CollectGarbage("resurrected-cleared"));
        f.vm.RunFinalization();
        static_cast<void>(f.vm.CollectGarbage("sentinel-cleared"));
        CHECK(calls->load() == 1);
        CHECK_FALSE(f.model.FindIdentity(identity).IsValid());
        CHECK_FALSE(threads.TakeFailure().has_value());
        threads.Shutdown();
    }
}

TEST_CASE("WU7-4 weak soft and phantom queues distinguish pressure and finalizer reachability") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        GcVm f({}, backend);
        const auto make = [&](const char* owner, const char* signature, std::vector<VmValue> args = {}) {
            const auto instance = f.vm.NewIntrinsicInstance(owner);
            const auto root = f.vm.ProtectReferences(std::array{instance});
            const auto ctor = f.linker.FindDirectMethod(f.model.ObjectClass(instance), "<init>", signature);
            REQUIRE(ctor);
            args.insert(args.begin(), VmValue::Ref(instance));
            const auto result = f.vm.Call(*ctor, args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return instance;
        };
        const auto invoke = [&](VmObjectRef receiver, const char* name, const char* signature) {
            const auto type = f.model.ObjectClass(receiver);
            const auto slot = f.linker.FindVtableIndex(type, name, signature);
            REQUIRE(slot);
            const auto result = f.vm.Call(f.linker.Class(type).vtable[*slot], std::array{VmValue::Ref(receiver)});
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        for (const auto* owner :
             {"Ljava/lang/ref/WeakReference;", "Ljava/lang/ref/SoftReference;", "Ljava/lang/ref/PhantomReference;"}) {
            const auto queue = make("Ljava/lang/ref/ReferenceQueue;", "()V");
            const auto qr = f.vm.ProtectReferences(std::array{queue});
            const auto value = f.vm.NewIntrinsicInstance("Lgc/RootBox;");
            const auto reference = make(owner, "(Ljava/lang/Object;Ljava/lang/ref/ReferenceQueue;)V",
                                        {VmValue::Ref(value), VmValue::Ref(queue)});
            const auto root = f.vm.ProtectReferences(std::array{reference});
            if (std::string_view(owner).find("Phantom") != std::string_view::npos)
                CHECK_FALSE(invoke(reference, "get", "()Ljava/lang/Object;").ref.IsValid());
            static_cast<void>(f.vm.CollectGarbage("ordinary-reference"));
            if (std::string_view(owner).find("Soft") != std::string_view::npos) {
                CHECK(f.model.IsValidRef(value));
                CHECK_FALSE(invoke(queue, "poll", "()Ljava/lang/ref/Reference;").ref.IsValid());
                static_cast<void>(f.vm.CollectGarbage("reference-pressure", true));
            }
            CHECK_FALSE(f.model.IsValidRef(value));
            CHECK(invoke(queue, "poll", "()Ljava/lang/ref/Reference;").ref == reference);
            CHECK_FALSE(invoke(queue, "poll", "()Ljava/lang/ref/Reference;").ref.IsValid());
            CHECK_FALSE(invoke(reference, "get", "()Ljava/lang/Object;").ref.IsValid());
        }
    }
}

TEST_CASE("WU7-4 a reclaimed Thread handle can start again with a new Java identity") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto declaration = IntrinsicClassBuilder::Class("Lgc/Worker;", "Ljava/lang/Thread;");
        declaration.OverrideMethod("run", "()V", [](IntrinsicContext&) { return VmValue::Void(); });
        GcVm f({std::move(declaration).Build()}, backend);
        VmThreadRuntime threads(f.vm);
        const auto old = f.vm.NewIntrinsicInstance("Lgc/Worker;");
        const auto identity = f.model.ToIdentity(old);
        const auto old_id = threads.AllocateThreadId();
        threads.Start(old, "old-worker", old_id);
        threads.Join(old);
        static_cast<void>(f.vm.CollectGarbage("dead-thread"));
        CHECK_FALSE(f.model.FindIdentity(identity).IsValid());
        VmObjectRef recycled;
        for (std::size_t i = 0; i < 1024 && !recycled.IsValid(); ++i) {
            const auto object = f.vm.NewIntrinsicInstance("Lgc/Worker;");
            if (object == old) recycled = object;
        }
        REQUIRE(recycled.IsValid());
        const auto root = f.vm.ProtectReferences(std::array{recycled});
        CHECK(f.model.ToIdentity(recycled) != identity);
        CHECK_FALSE(threads.IsAlive(recycled));
        CHECK(threads.ThreadId(recycled) == 0);
        const auto new_id = threads.AllocateThreadId();
        CHECK(new_id != old_id);
        threads.Start(recycled, "new-worker", new_id);
        threads.Join(recycled);
        CHECK(threads.ThreadId(recycled) == new_id);
        CHECK_FALSE(threads.TakeFailure().has_value());
        threads.Shutdown();
    }
}

TEST_CASE("WU7-4 native close claims its owner before reentrant collection and restores on failure") {
    struct Bridge final : NativeMethodBridge {
        Interpreter* vm{};
        int calls{};
        bool nested_cleanup{true};
        bool reject{};

        VmValue Invoke(const LinkedMethod&, VmObjectRef, std::span<const VmValue>) override {
            ++calls;
            if (reject) throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "close rejected"};
            if (nested_cleanup) {
                nested_cleanup = false;
                static_cast<void>(vm->CollectGarbage("while-native-close-is-active"));
                vm->ReleaseGuestNativeResources(true);
            }
            return VmValue::Void();
        }
    };

    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Bridge bridge;
        auto declaration = IntrinsicClassBuilder::Class("Lgc/CloseOwner;");
        declaration.InstanceField("peer", "J");
        declaration.GuestNativeStatic("release", "(J)V", kAccPublic | kAccStatic | kAccNative);
        GcVm f({std::move(declaration).Build()}, backend, &bridge);
        bridge.vm = &f.vm;
        const auto type = f.linker.ResolveDescriptor("Lgc/CloseOwner;");
        const auto field = f.linker.FindFieldRecursive(type, "peer", "J");
        const auto cleanup = f.linker.FindDirectMethod(type, "release", "(J)V");
        REQUIRE(field);
        REQUIRE(cleanup);
        f.vm.TrackGuestNativeResourceField(*field, *cleanup);
        const auto owner = f.vm.NewIntrinsicInstance("Lgc/CloseOwner;");
        const auto root = f.vm.ProtectReferences(std::array{owner});
        const auto slot = f.linker.Field(*field).slot;
        f.model.InstanceSlots(owner)[slot] = {123, SlotTag::wide_lo};
        f.model.InstanceSlots(owner)[slot + 1] = {0, SlotTag::wide_hi};
        const auto closed = f.vm.Call(*cleanup, std::array{VmValue::Long(123)});
        REQUIRE_FALSE(closed.exception.IsValid());
        CHECK(bridge.calls == 1);
        CHECK(f.model.InstanceSlots(owner)[slot].bits == 0);
        CHECK(f.vm.GuestNativeResourceCount() == 0);
        bridge.reject = true;
        f.model.InstanceSlots(owner)[slot].bits = 456;
        const auto failed = f.vm.Call(*cleanup, std::array{VmValue::Long(456)});
        REQUIRE(failed.exception.IsValid());
        CHECK(f.model.InstanceSlots(owner)[slot].bits == 456);
        CHECK(f.vm.GuestNativeResourceCount() == 1);
        bridge.reject = false;
        f.vm.ReleaseGuestNativeResources(true);
        CHECK(bridge.calls == 3);
        CHECK(f.vm.GuestNativeResourceCount() == 0);
    }
}

TEST_CASE("DVM-260 primitive aget aput Release microbenchmark") {
    if (!std::getenv("OGPLAY_RUN_ARRAY_BENCHMARK")) return;
    using namespace ogplay::runtime;
    using namespace ogplay::runtime::dexvm;
    ogplay::runtime::HostCopyJavaPayloadProvider strings_payload;
    JniStringStore strings{strings_payload};
    ogplay::memory::AddressSpace memory;
    ogplay::core::CapabilityLedger ledger;
    GuestJavaPayloadProvider arrays_payload{memory, ledger};
    JniPrimitiveArrayStore arrays{arrays_payload};
    JavaObjectModel model{strings, arrays};
    const auto array = model.NewPrimitiveArray(DexClassId{1}, JniPrimitiveKind::integer, 65536);
    std::vector<std::uint64_t> times;
    std::uint64_t checksum{};
    for (int round = 0; round < 7; ++round) {
        const auto start = ogplay::hal::Clock::SteadyTimestampNs();
        for (std::uint32_t i = 0; i < 1000000; ++i) {
            const auto index = static_cast<JniSize>(i & 65535U);
            const auto value = model.GetPrimitiveElement(array, (index + 1) & 65535);
            model.SetPrimitiveElement(array, index, value + i);
        }
        times.push_back(ogplay::hal::Clock::SteadyTimestampNs() - start);
        checksum += model.GetPrimitiveElement(array, round);
    }
    std::sort(times.begin(), times.end());
    MESSAGE("ARRAY_BENCHMARK median_ns=" << times[3] << " checksum=" << checksum);
    CHECK(checksum != 0);
}

TEST_CASE("DVM-260 direct pin is a strong root until final release") {
    GcVm f;
    const auto array = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 3);
    const auto identity = f.model.ToIdentity(array);
    auto access = f.arrays.Acquire(identity, JniArrayAccessKind::elements);
    REQUIRE_FALSE(access.is_copy);
    std::thread writer([&] { f.memory.Write32(access.address, 123); });
    writer.join();
    CHECK(f.model.GetPrimitiveElement(array, 0) == 123);
    f.vm.SetGcIntegration({[&](const auto& visit) { f.arrays.VisitAccessRoots(visit); }, {}, {}});
    const auto weak = f.vm.NewIntrinsicInstance("Ljava/lang/ref/WeakReference;");
    const auto weak_root = f.vm.ProtectReferences(std::array{weak});
    const auto ctor = f.linker.FindDirectMethod(f.model.ObjectClass(weak), "<init>", "(Ljava/lang/Object;)V");
    REQUIRE(ctor);
    const auto constructed = f.vm.Call(*ctor, std::array{VmValue::Ref(weak), VmValue::Ref(array)});
    REQUIRE_FALSE(constructed.exception.IsValid());
    const auto referent = f.linker.FindFieldRecursive(f.model.ObjectClass(weak), "referent", "Ljava/lang/Object;");
    REQUIRE(referent);
    const auto referent_slot = f.linker.Field(*referent).slot;
    const auto before = f.vm.MarkReachable();
    CHECK(before.IsMarked(array));
    static_cast<void>(f.vm.CollectGarbage());
    CHECK(f.model.ArrayLength(array) == 3);
    CHECK(f.arrays.Acquire(identity, JniArrayAccessKind::critical).address == access.address);
    CHECK(f.arrays.PinCount(identity) == 2);
    f.arrays.Release(identity, JniArrayReleaseMode::abort);
    CHECK(f.model.InstanceSlots(weak)[referent_slot].bits == array.Value());
    f.arrays.Release(identity, JniArrayReleaseMode::commit);
    CHECK(f.vm.MarkReachable().IsMarked(array));
    f.arrays.Release(identity, JniArrayReleaseMode::abort);
    CHECK_FALSE(f.vm.MarkReachable().IsMarked(array));
    static_cast<void>(f.vm.CollectGarbage());
    CHECK_THROWS_AS(static_cast<void>(f.arrays.Length(identity)), JniArrayError);
    CHECK(f.model.InstanceSlots(weak)[referent_slot].bits == 0);
}

TEST_CASE("DVM-260 direct String pins survive GC until final cross-kind release") {
    GcVm f;
    const auto text = f.model.NewString(u"A\0\U0001f600");
    const auto id = f.model.ToIdentity(text);
    const auto pin = f.strings.Acquire(id, JniStringAccessKind::chars);
    f.vm.SetGcIntegration({[&](const auto& visit) { f.strings.VisitAccessRoots(visit); }, {}, {}});
    for (int round = 0; round < 3; ++round) {
        static_cast<void>(f.vm.CollectGarbage());
        REQUIRE(f.strings.Contains(id));
        auto second = f.strings.Acquire(id, JniStringAccessKind::critical);
        CHECK(second.address == pin.address);
        CHECK(f.strings.PinCount(id) == 2);
        f.strings.Release(id, 0, JniStringAccessKind::chars);
    }
    CHECK_FALSE(pin.is_copy);
    f.strings.Release(id, 0, JniStringAccessKind::critical);
    static_cast<void>(f.vm.CollectGarbage());
    CHECK_FALSE(f.strings.Contains(id));
}

TEST_CASE("DVM-353 ROM finalizer watchdog measures injected real time during a driver stall") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const bool timeout : {false, true}) {
            std::atomic<bool> entered{}, release{}, completed{};
            auto declaration = IntrinsicClassBuilder::Class("Lgc/PacedFinalizable;");
            declaration.OverrideMethod(
                "finalize", "()V",
                [&](IntrinsicContext& c) {
                    const auto depth = c.vm.ExecutionLock().ReleaseForBlocking();
                    entered = true;
                    while (!release.load())
                        std::this_thread::yield();
                    c.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    completed = true;
                    return VmValue::Void();
                },
                kAccProtected);
            GcVm f({std::move(declaration).Build()}, backend);
            VmThreadRuntime threads(f.vm);
            std::atomic<std::int64_t> real{}, uptime{};
            ogplay::hal::RealtimeClock clock(
                1000, [&] { return ogplay::hal::RealtimeClock::TimePoint{} + std::chrono::milliseconds(real.load()); });
            auto& monitors = f.vm.Monitors();
            monitors.SetTimeSource([&] { return uptime.load(); });
            monitors.SetRealtimeSource([&] { return static_cast<std::int64_t>(clock.Ticks()); });
            monitors.SetClockAdvance([&](std::int64_t delta) { uptime += delta; });
            monitors.ObserveClockDriverBlocking(true);
            static_cast<void>(f.vm.NewIntrinsicInstance("Lgc/PacedFinalizable;"));
            static_cast<void>(f.vm.CollectGarbage("paced-finalizer"));
            const auto await = [](auto predicate) {
                for (int i = 0; i < 3000; ++i) {
                    if (predicate()) return true;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return false;
            };
            CHECK(await([&] { return entered.load(); }));
            CHECK(await([&] {
                const auto snapshot = threads.Snapshot();
                return std::ranges::any_of(snapshot, [](const auto& t) {
                    return t.name == "FinalizerWatchdogDaemon" && t.wait_state == VmThreadWaitState::sleeping;
                });
            }));
            real = 3000;
            CHECK(await([&] { return uptime.load() == 3000; }));
            CHECK_FALSE(f.vm.ExitCode().has_value());
            CHECK_FALSE(completed.load());
            if (timeout) {
                real = 10001;
                CHECK(await([&] { return f.vm.ExitCode().has_value(); }));
                CHECK(f.vm.ExitCode() == 2);
            }
            release = true;
            if (!timeout) {
                CHECK(await([&] { return completed.load(); }));
                CHECK_FALSE(f.vm.ExitCode().has_value());
            }
            threads.Shutdown();
        }
    }
}

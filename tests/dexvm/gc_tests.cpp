#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/collection_runtime.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni/jni_class_registry.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

struct GcVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    std::shared_ptr<std::uint32_t> destructor_calls =
        std::make_shared<std::uint32_t>(0);
    Interpreter vm;

    GcVm()
        : vm([this]() -> DexClassLinker& {
                 auto catalog = CoreIntrinsicCatalog();
                 auto builder =
                     IntrinsicClassBuilder::Class("Lgc/RootBox;");
                 builder.InstanceField("child", "Ljava/lang/Object;");
                 builder.StaticField("root", "Ljava/lang/Object;");
                 catalog.push_back(std::move(builder).Build());
                 auto host =
                     IntrinsicClassBuilder::Class("Lgc/HostBox;");
                 host.HostStateDestructor(
                     [calls = destructor_calls](std::uint64_t state) {
                         CHECK(state == 99);
                         ++*calls;
                     });
                 catalog.push_back(std::move(host).Build());
                 linker.RegisterIntrinsics(catalog);
                 linker.Link();
                 return linker;
             }(),
             model, nullptr, ledger) {}
};

TEST_CASE("DexVM GC root surface enumerates permanent static JNI and session roots") {
    GcVm fixture;
    const auto static_root = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.vm.SetIntrinsicStaticRef("Lgc/RootBox;", "root",
                                    "Ljava/lang/Object;", static_root);
    const auto intern_root = fixture.model.InternString(u"permanent");
    const auto jni_root = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto session_root = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.vm.SetGcIntegration({
        [identity = fixture.model.ToIdentity(jni_root)](const auto& visit) {
            visit(identity);
        },
        {},
        [session_root](const VmRootVisitor& visit) { visit(session_root); }});

    std::set<std::uint32_t> roots;
    fixture.vm.VisitRoots(
        [&](const VmObjectRef ref) {
            if (ref.IsValid()) roots.insert(ref.Value());
        });

    CHECK(roots.contains(static_root.Value()));
    CHECK(roots.contains(intern_root.Value()));
    CHECK(roots.contains(jni_root.Value()));
    CHECK(roots.contains(session_root.Value()));
    const auto root_box = fixture.linker.FindClass("Lgc/RootBox;");
    REQUIRE(root_box.has_value());
    CHECK(fixture.linker.Class(*root_box).static_ref_slots ==
          std::vector<std::uint16_t>{0});
    CHECK(fixture.vm.RegisteredIntrinsicSideTableCount() == 4);
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
    references.VisitRoots([&](const JniObjectIdentity value) {
        roots.push_back(value);
    });
    CHECK(std::ranges::find(roots, local) != roots.end());
    CHECK(std::ranges::find(roots, global) != roots.end());
    CHECK(std::ranges::find(roots, weak) == roots.end());
}

TEST_CASE("DexVM marker traces exact object edges and intrinsic side tables") {
    GcVm fixture;
    const auto root = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto field_child = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.model.InstanceSlots(root)[0] =
        Slot{field_child.Value(), SlotTag::ref};
    const auto array_class = fixture.linker.ResolveDescriptor("[Ljava/lang/Object;");
    const auto object_class = fixture.linker.FindClass("Ljava/lang/Object;");
    REQUIRE(object_class.has_value());
    const auto array = fixture.model.NewObjectArray(array_class, *object_class, 1);
    fixture.model.SetObjectElement(array, 0, root);
    const auto side_child = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.vm.ListStorage(root).push_back(side_child);
    const auto garbage = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.vm.SetGcIntegration(
        {{}, {}, [array](const VmRootVisitor& visit) { visit(array); }});

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
    auto state = std::make_shared<
        std::unordered_map<std::uint32_t, VmObjectRef>>();
    fixture.vm.RegisterIntrinsicStateTable({
        "gc-test-state",
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
    CHECK(fixture.vm.RegisteredIntrinsicSideTableCount() == 5);

    const auto owner = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto child = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto garbage_io = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.vm.IO().SetInput(garbage_io, {{std::byte{0x2a}}, 0, false});
    (*state)[owner.Value()] = child;
    fixture.vm.ListStorage(owner).push_back(child);
    auto& source_map = fixture.vm.Collections().EnsureMap(owner);
    source_map.entries.push_back({1, child, owner, 7});
    const auto source_hash = fixture.model.IdentityHashCode(owner);
    const auto clone = fixture.vm.CloneObject(owner);
    CHECK(fixture.model.IdentityHashCode(clone) != source_hash);
    CHECK(fixture.model.IdentityHashCode(owner) == source_hash);
    REQUIRE(state->contains(clone.Value()));
    CHECK(state->at(clone.Value()) == child);
    REQUIRE(fixture.vm.ListStorage(clone).size() == 1);
    CHECK(fixture.vm.ListStorage(clone).front() == child);
    const auto* cloned_map = fixture.vm.Collections().FindMap(clone);
    REQUIRE(cloned_map != nullptr);
    REQUIRE(cloned_map->entries.size() == 1);
    CHECK(cloned_map->entries.front().key == child);
    CHECK(cloned_map->entries.front().value == owner);

    fixture.vm.SetGcIntegration(
        {{}, {}, [owner](const VmRootVisitor& visit) { visit(owner); }});
    const auto marked = fixture.vm.MarkReachable();
    CHECK(marked.IsMarked(owner));
    CHECK(marked.IsMarked(child));
    CHECK_FALSE(marked.IsMarked(clone));
    CHECK_FALSE(marked.IsMarked(garbage_io));
    static_cast<void>(fixture.vm.SweepGarbage(marked));
    CHECK_FALSE(state->contains(clone.Value()));
    CHECK(fixture.vm.IO().FindInput(garbage_io) == nullptr);
}

TEST_CASE("DexVM and JNI share one object array identity and element store") {
    JniStringStore strings;
    JniPrimitiveArrayStore primitives;
    JniClassRegistry jni_classes;
    const auto jni_object = jni_classes.RegisterClass({"java/lang/Object"});
    const auto jni_box = jni_classes.RegisterClass(
        {"gc/Box", std::string{"java/lang/Object"}});
    JniGuestObjectRegistry objects(jni_classes);
    auto& object_arrays = objects.ObjectArrays();

    DexClassLinker linker;
    auto catalog = CoreIntrinsicCatalog();
    catalog.push_back(
        std::move(IntrinsicClassBuilder::Class("Lgc/Box;")).Build());
    linker.RegisterIntrinsics(catalog);
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
        {&object_arrays, publish_class, resolve_class, {},
         [&](const JniObjectIdentity element_class) {
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
    object_arrays.Set(imported_identity, 0,
                      JniObjectValue{model.ToIdentity(element), jni_box});
    const auto imported = model.FromIdentity(imported_identity);
    CHECK(model.Kind(imported) == VmObjectKind::object_array);
    CHECK(model.ObjectClass(imported) == dex_array);
    CHECK(model.GetObjectElement(imported, 0) == element);
}

TEST_CASE("DexVM sweeper releases stores reuses handles and is idempotent") {
    GcVm fixture;
    const auto survivor = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto dead = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.vm.ListStorage(dead).push_back(survivor);
    const auto dead_string = fixture.model.NewString(u"dead");
    const auto dead_string_hash =
        fixture.model.IdentityHashCode(dead_string);
    const auto string_identity = fixture.model.ToIdentity(dead_string);
    const auto before = fixture.model.AllocatedBytes();
    std::vector<JniObjectIdentity> weak_clears;
    fixture.vm.SetGcIntegration({
        {},
        [&](const JniObjectIdentity identity) { weak_clears.push_back(identity); },
        [survivor](const VmRootVisitor& visit) { visit(survivor); }});

    const auto first = fixture.vm.CollectGarbage();
    CHECK(first.freed_objects == 2);
    CHECK(first.freed_bytes == 80);
    CHECK(fixture.model.AllocatedBytes() == before - 80);
    CHECK_FALSE(fixture.model.IsValidRef(dead));
    CHECK_THROWS_AS(static_cast<void>(fixture.model.Kind(dead)), DexVmError);
    CHECK_THROWS_AS(static_cast<void>(fixture.strings.Length(string_identity)),
                    JniStringError);
    CHECK(std::ranges::find(weak_clears, string_identity) != weak_clears.end());

    const auto reused = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    CHECK(reused == dead_string);
    CHECK(fixture.model.IdentityHashCode(reused) != dead_string_hash);
    CHECK(fixture.vm.ListStorage(reused).empty());
    fixture.vm.SetGcIntegration(
        {{}, {}, [survivor, reused](const VmRootVisitor& visit) {
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
    fixture.vm.SetGcIntegration(
        {{}, {}, [live](const VmRootVisitor& visit) { visit(live); }});

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

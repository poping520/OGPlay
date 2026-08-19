#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <memory>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"

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

TEST_CASE("DexVM sweeper releases stores reuses handles and is idempotent") {
    GcVm fixture;
    const auto survivor = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    const auto dead = fixture.vm.NewIntrinsicInstance("Lgc/RootBox;");
    fixture.vm.ListStorage(dead).push_back(survivor);
    const auto dead_string = fixture.model.NewString(u"dead");
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

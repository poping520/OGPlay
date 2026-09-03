#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

struct ObserverEvent final {
    VmObjectRef observer;
    VmObjectRef observable;
    VmObjectRef data;
    bool observable_monitor_held{};
};

struct ObserverControl final {
    std::vector<ObserverEvent> events;
    std::unordered_set<std::uint32_t> delete_all_and_collect;
    std::unordered_set<std::uint32_t> throw_on_update;
};

struct ObservableVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    std::shared_ptr<ObserverControl> control =
        std::make_shared<ObserverControl>();
    Interpreter vm;

    ObservableVm()
        : vm([this]() -> DexClassLinker& {
              linker.RegisterIntrinsics(CoreIntrinsicCatalog());
              auto observer = IntrinsicClassBuilder::Class(
                  "Ltest/RecordingObserver;", "Ljava/lang/Object;",
                  {"Ljava/util/Observer;"});
              observer.VirtualMethod(
                  "update",
                  "(Ljava/util/Observable;Ljava/lang/Object;)V",
                  [control = control](IntrinsicContext& context) {
                      const auto observable = context.arguments[0].ref;
                      control->events.push_back(
                          {context.receiver, observable,
                           context.arguments[1].ref,
                           context.vm.Monitors().IsOwner(
                               observable,
                               context.vm.CurrentContextToken())});
                      if (control->delete_all_and_collect.contains(
                              context.receiver.Value())) {
                          const auto owner =
                              context.vm.Model().ObjectClass(observable);
                          const auto index = context.vm.Linker().FindVtableIndex(
                              owner, "deleteObservers", "()V");
                          if (!index.has_value()) {
                              throw DexVmError(
                                  DexVmErrorReason::internal_invariant,
                                  "Observable.deleteObservers is missing");
                          }
                          const std::vector<VmValue> arguments{
                              VmValue::Ref(observable)};
                          const auto outcome = context.vm.Call(
                              context.vm.Linker().Class(owner).vtable[*index],
                              arguments);
                          if (outcome.exception.IsValid()) {
                              context.vm.SetPendingException(outcome.exception);
                              return VmValue::Void();
                          }
                          static_cast<void>(context.vm.CollectGarbage(
                              "observable_snapshot_test"));
                      }
                      if (control->throw_on_update.contains(
                              context.receiver.Value())) {
                          throw VmJavaThrow{
                              "Ljava/lang/IllegalStateException;",
                              "observer update failed"};
                      }
                      return VmValue::Void();
                  });
              std::vector<IntrinsicClassDecl> test_catalog;
              test_catalog.push_back(std::move(observer).Build());
              linker.RegisterIntrinsics(test_catalog);
              linker.Link();
              return linker;
          }(), model, nullptr, ledger, {}) {}

    [[nodiscard]] VmCallOutcome Virtual(
        const VmObjectRef receiver, const std::string_view name,
        const std::string_view descriptor,
        std::vector<VmValue> arguments = {}) {
        const auto owner = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(
            owner, std::string(name), std::string(descriptor));
        REQUIRE(index.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return vm.Call(linker.Class(owner).vtable[*index], arguments);
    }

    void Construct(const VmObjectRef object, const std::string_view owner) {
        const auto method = linker.FindDirectMethod(
            linker.ResolveDescriptor(owner), "<init>", "()V");
        REQUIRE(method.has_value());
        const std::array arguments{VmValue::Ref(object)};
        RequireOk(vm.Call(*method, arguments));
    }

    [[nodiscard]] VmObjectRef NewObservable() {
        const auto result =
            vm.NewIntrinsicInstance("Ljava/util/Observable;");
        Construct(result, "Ljava/util/Observable;");
        return result;
    }

    [[nodiscard]] VmObjectRef NewObserver() {
        return vm.NewIntrinsicInstance("Ltest/RecordingObserver;");
    }

    static void RequireOk(const VmCallOutcome& outcome) {
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    }

    void RequireException(const VmCallOutcome& outcome,
                          const std::string_view descriptor) const {
        REQUIRE(outcome.exception.IsValid());
        CHECK(linker.Class(outcome.exception_class).descriptor == descriptor);
    }
};

}  // namespace

TEST_CASE("java.util Observer and Observable expose the API 19 hierarchy") {
    ObservableVm fixture;
    const auto observer =
        fixture.linker.ResolveDescriptor("Ljava/util/Observer;");
    const auto observable =
        fixture.linker.ResolveDescriptor("Ljava/util/Observable;");

    const auto& observer_class = fixture.linker.Class(observer);
    CHECK(observer_class.is_interface);
    CHECK(observer_class.access_flags == 0x0601U);
    REQUIRE(observer_class.own_virtual_methods.size() == 1U);
    const auto& update =
        fixture.linker.Method(observer_class.own_virtual_methods.front());
    CHECK(update.name == "update");
    CHECK(update.descriptor ==
          "(Ljava/util/Observable;Ljava/lang/Object;)V");
    CHECK(update.access_flags == 0x0401U);

    const auto& observable_class = fixture.linker.Class(observable);
    CHECK_FALSE(observable_class.is_interface);
    REQUIRE(observable_class.super.has_value());
    CHECK(*observable_class.super ==
          fixture.linker.ResolveDescriptor("Ljava/lang/Object;"));
    CHECK(observable_class.own_instance_fields.size() == 2U);
    CHECK(observable_class.own_direct_methods.size() == 1U);
    CHECK(observable_class.own_virtual_methods.size() == 9U);

    const auto delete_observer = fixture.linker.FindVtableIndex(
        observable, "deleteObserver", "(Ljava/util/Observer;)V");
    const auto delete_observers = fixture.linker.FindVtableIndex(
        observable, "deleteObservers", "()V");
    REQUIRE(delete_observer.has_value());
    REQUIRE(delete_observers.has_value());
    CHECK((fixture.linker.Method(
               observable_class.vtable[*delete_observer]).access_flags &
           0x0020U) != 0U);
    CHECK((fixture.linker.Method(
               observable_class.vtable[*delete_observers]).access_flags &
           0x0020U) != 0U);
}

TEST_CASE("java.util Observable gates notifications and manages registration") {
    ObservableVm fixture;
    const auto observable = fixture.NewObservable();
    const auto observer = fixture.NewObserver();
    const auto data = fixture.vm.NewStringUtf8("event");

    CHECK(fixture.Virtual(observable, "countObservers", "()I")
              .value.AsInt() == 0);
    CHECK(fixture.Virtual(observable, "hasChanged", "()Z").value.AsInt() ==
          0);
    fixture.RequireException(
        fixture.Virtual(observable, "addObserver",
                        "(Ljava/util/Observer;)V",
                        {VmValue::Ref(VmObjectRef{})}),
        "Ljava/lang/NullPointerException;");

    ObservableVm::RequireOk(fixture.Virtual(
        observable, "addObserver", "(Ljava/util/Observer;)V",
        {VmValue::Ref(observer)}));
    ObservableVm::RequireOk(fixture.Virtual(
        observable, "addObserver", "(Ljava/util/Observer;)V",
        {VmValue::Ref(observer)}));
    CHECK(fixture.Virtual(observable, "countObservers", "()I")
              .value.AsInt() == 1);
    ObservableVm::RequireOk(fixture.Virtual(
        observable, "deleteObserver", "(Ljava/util/Observer;)V",
        {VmValue::Ref(VmObjectRef{})}));

    ObservableVm::RequireOk(
        fixture.Virtual(observable, "notifyObservers",
                        "(Ljava/lang/Object;)V", {VmValue::Ref(data)}));
    CHECK(fixture.control->events.empty());
    ObservableVm::RequireOk(
        fixture.Virtual(observable, "setChanged", "()V"));
    ObservableVm::RequireOk(
        fixture.Virtual(observable, "notifyObservers",
                        "(Ljava/lang/Object;)V", {VmValue::Ref(data)}));
    REQUIRE(fixture.control->events.size() == 1U);
    CHECK(fixture.control->events[0].observer == observer);
    CHECK(fixture.control->events[0].observable == observable);
    CHECK(fixture.control->events[0].data == data);
    CHECK_FALSE(fixture.control->events[0].observable_monitor_held);
    CHECK(fixture.Virtual(observable, "hasChanged", "()Z").value.AsInt() ==
          0);

    ObservableVm::RequireOk(
        fixture.Virtual(observable, "setChanged", "()V"));
    ObservableVm::RequireOk(
        fixture.Virtual(observable, "clearChanged", "()V"));
    ObservableVm::RequireOk(
        fixture.Virtual(observable, "notifyObservers", "()V"));
    CHECK(fixture.control->events.size() == 1U);

    ObservableVm::RequireOk(
        fixture.Virtual(observable, "setChanged", "()V"));
    ObservableVm::RequireOk(
        fixture.Virtual(observable, "notifyObservers", "()V"));
    REQUIRE(fixture.control->events.size() == 2U);
    CHECK_FALSE(fixture.control->events[1].data.IsValid());

    ObservableVm::RequireOk(fixture.Virtual(
        observable, "deleteObserver", "(Ljava/util/Observer;)V",
        {VmValue::Ref(observer)}));
    CHECK(fixture.Virtual(observable, "countObservers", "()I")
              .value.AsInt() == 0);
}

TEST_CASE("java.util Observable uses a rooted callback snapshot") {
    ObservableVm fixture;
    const auto observable = fixture.NewObservable();
    const auto first = fixture.NewObserver();
    const auto second = fixture.NewObserver();
    fixture.control->delete_all_and_collect.insert(first.Value());

    for (const auto observer : {first, second}) {
        ObservableVm::RequireOk(fixture.Virtual(
            observable, "addObserver", "(Ljava/util/Observer;)V",
            {VmValue::Ref(observer)}));
    }
    ObservableVm::RequireOk(
        fixture.Virtual(observable, "setChanged", "()V"));
    ObservableVm::RequireOk(
        fixture.Virtual(observable, "notifyObservers", "()V"));

    REQUIRE(fixture.control->events.size() == 2U);
    CHECK(fixture.control->events[0].observer == first);
    CHECK(fixture.control->events[1].observer == second);
    CHECK(fixture.Virtual(observable, "countObservers", "()I")
              .value.AsInt() == 0);
}

TEST_CASE("java.util Observable propagates observer failures") {
    ObservableVm fixture;
    const auto observable = fixture.NewObservable();
    const auto failing = fixture.NewObserver();
    const auto skipped = fixture.NewObserver();
    fixture.control->throw_on_update.insert(failing.Value());

    for (const auto observer : {failing, skipped}) {
        ObservableVm::RequireOk(fixture.Virtual(
            observable, "addObserver", "(Ljava/util/Observer;)V",
            {VmValue::Ref(observer)}));
    }
    ObservableVm::RequireOk(
        fixture.Virtual(observable, "setChanged", "()V"));
    fixture.RequireException(
        fixture.Virtual(observable, "notifyObservers", "()V"),
        "Ljava/lang/IllegalStateException;");
    REQUIRE(fixture.control->events.size() == 1U);
    CHECK(fixture.control->events[0].observer == failing);
    CHECK(fixture.Virtual(observable, "hasChanged", "()Z").value.AsInt() ==
          0);
}

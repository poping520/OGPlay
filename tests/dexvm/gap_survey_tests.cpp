// Gap survey mode (diagnostic): unresolved platform surfaces become recorded
// neutral stubs so one run harvests the whole work queue. Every case here has
// a "survey off => honest failure" counterpart, per the quirk rule.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/gap_survey.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

struct Linked final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;

    Linked()
        : model(strings, arrays, {}),
          interpreter(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, {}) {}
};

constexpr const char* kAbsent = "Landroid/bogus/Absent;";

}  // namespace

TEST_CASE("unknown platform classes fail unless the gap survey is enabled") {
    Linked off;
    CHECK_THROWS_AS(static_cast<void>(off.linker.ResolveDescriptor(kAbsent)),
                    DexVmError);
    CHECK(off.linker.GapSurveyHits().empty());

    Linked on;
    on.linker.EnableGapSurvey();
    const auto java_class = on.linker.ResolveDescriptor(kAbsent);
    REQUIRE(java_class.IsValid());
    CHECK(on.linker.Class(java_class).is_intrinsic);
    const auto hits = on.linker.GapSurveyHits();
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].owner_descriptor == kAbsent);
    CHECK(hits[0].member.empty());
}

TEST_CASE("survey stubs answer neutrally and record every call") {
    Linked vm;
    vm.linker.EnableGapSurvey();
    const auto java_class = vm.linker.ResolveDescriptor(kAbsent);
    const auto method = vm.linker.SynthesizeSurveyMethod(
        java_class, "measure", "(I)I", InvokeKind::virtual_call);
    // Dispatch must find the stub through the normal virtual lookup.
    CHECK(vm.linker.FindVtableIndex(java_class, "measure", "(I)I")
              .has_value());

    const auto instance = vm.interpreter.NewIntrinsicInstance(kAbsent);
    const std::vector<VmValue> arguments{VmValue::Ref(instance),
                                         VmValue::Int(7)};
    for (int call = 0; call < 3; ++call) {
        const auto outcome = vm.interpreter.Call(method, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        REQUIRE(outcome.value.kind == VmValue::Kind::cat1);
        CHECK(outcome.value.AsInt() == 0);
    }

    const auto hits = vm.linker.GapSurveyHits();
    REQUIRE(hits.size() == 2);  // the class plus the member
    bool found_member = false;
    for (const auto& hit : hits) {
        if (hit.member == "measure(I)I") {
            found_member = true;
            CHECK(hit.owner_descriptor == kAbsent);
            CHECK(hit.hits == 3U);
        }
    }
    CHECK(found_member);
}

TEST_CASE("survey stubs are rejected outside survey mode") {
    Linked vm;
    const auto object = vm.linker.FindClass("Ljava/lang/Object;");
    REQUIRE(object.has_value());
    CHECK_THROWS_AS(static_cast<void>(vm.linker.SynthesizeSurveyMethod(
                        *object, "measure", "()V", InvokeKind::static_call)),
                    DexVmError);
}

TEST_CASE("gap survey json is a hottest-first work queue marked as a survey") {
    const std::vector<GapSurveyHit> hits{
        {"Landroid/bogus/Absent;", "", 1},
        {"Landroid/bogus/Absent;", "cold()V", 2},
        {"Landroid/bogus/Absent;", "hot()V", 40},
    };
    const auto json = RenderGapSurveyJson(hits, "Title.apk");
    CHECK(json.find("\"survey\": true") != std::string::npos);
    CHECK(json.find("\"title\": \"Title.apk\"") != std::string::npos);
    CHECK(json.find("\"missing_classes\": 1") != std::string::npos);
    CHECK(json.find("\"missing_members\": 2") != std::string::npos);
    CHECK(json.find("\"stub_hits\": 43") != std::string::npos);
    CHECK(json.find("hot()V") < json.find("cold()V"));
}

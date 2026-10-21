#include "boot_dex.h"
// Gap survey mode (diagnostic): unresolved platform surfaces become recorded
// neutral stubs so one run harvests the whole work queue. Every case here has
// a "survey off => honest failure" counterpart, per the quirk rule.

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/gap_survey.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"

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
                  linker.RegisterIntrinsics(AndroidIntrinsicCatalog(
                      std::make_shared<DexVmAndroidContext>()));
                  ogplay::test::RegisterBootDex(linker);
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

TEST_CASE("gap survey json carries source, channel and both orderings") {
    GapSurveyHit early{"Landroid/view/View;", "dispatchTouchEvent(Landroid/view/MotionEvent;)Z", 2};
    early.source = GapSurveySource::skeleton;
    early.first_channel = GapSurveyChannel::jni;
    early.channel_hits = {1U, 1U, 0U};
    early.first_hit_index = 1;
    early.first_uptime_ms = 250;
    early.first_stack = {"Lgame/Main;->onTouch(I)V pc=4"};
    GapSurveyHit hot{"Landroid/widget/TextView;", "setText(Ljava/lang/CharSequence;)V", 90};
    hot.source = GapSurveySource::intrinsic;
    hot.channel_hits = {90U, 0U, 0U};
    hot.first_hit_index = 5;
    GapSurveyHit field{"Landroid/view/View;", "mFlags:I", 3};
    field.kind = GapSurveyKind::field;
    field.source = GapSurveySource::bootdex;
    field.channel_hits = {0U, 0U, 3U};
    field.first_channel = GapSurveyChannel::reflection;
    field.first_hit_index = 3;
    const std::vector<GapSurveyHit> hits{hot, early, field};

    const auto json = RenderGapSurveyJson(hits, "Title.apk");
    CHECK(json.find("\"survey\": true") != std::string::npos);
    CHECK(json.find("never a compatibility result") != std::string::npos);
    CHECK(json.find("\"members_by_source\": {\"skeleton\": 1, \"intrinsic\": 1, "
                    "\"bootdex\": 1, \"missing\": 0}") != std::string::npos);
    CHECK(json.find("\"member_hits_by_channel\": {\"java\": 91, \"jni\": 1, "
                    "\"reflection\": 3}") != std::string::npos);
    CHECK(json.find("\"source\": \"skeleton\", \"channel\": \"jni\"") != std::string::npos);
    CHECK(json.find("\"kind\": \"field\"") != std::string::npos);
    CHECK(json.find("\"first_uptime_ms\": 250") != std::string::npos);
    CHECK(json.find("Lgame/Main;->onTouch(I)V pc=4") != std::string::npos);
    // missing_members is hottest first; members_by_first_hit is earliest first.
    const auto by_first = json.find("\"members_by_first_hit\"");
    REQUIRE(by_first != std::string::npos);
    const auto hot_hits = json.find("setText(");
    const auto early_hits = json.find("dispatchTouchEvent(");
    CHECK(hot_hits < early_hits);
    const auto first_early = json.find("dispatchTouchEvent(", by_first);
    const auto first_field = json.find("mFlags:I", by_first);
    const auto first_hot = json.find("setText(", by_first);
    CHECK(first_early < first_field);
    CHECK(first_field < first_hot);
}

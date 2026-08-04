#include <array>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/loader/dex_analysis.h"

namespace {

struct AnalysisFixture final {
    AnalysisFixture() {
        image.strings = {{0, u"nativeStep"}, {0, u"onDrawFrame"}};
        image.types = {{0, "Lsample/Peer;"}, {0, "Landroid/View;"}};
        image.methods = {{0, 0, 0}, {0, 0, 1}, {1, 0, 1}};
        ogplay::loader::DexClassDef app;
        app.class_type_index = 0;
        ogplay::loader::DexClassDef framework;
        framework.class_type_index = 1;
        image.classes = {app, framework};

        ogplay::loader::DexClassData app_data;
        app_data.class_def_index = 0;
        app_data.direct_methods.push_back({0, 0x0109, std::nullopt});
        app_data.virtual_methods.push_back(
            {1, 0x0001,
             ogplay::loader::DexCodeInfo{0x100, 2, 1, 0, 0, 0, 300}});
        ogplay::loader::DexClassData framework_data;
        framework_data.class_def_index = 1;
        framework_data.virtual_methods.push_back(
            {2, 0x0001,
             ogplay::loader::DexCodeInfo{0x200, 1, 1, 0, 0, 0, 9000}});
        classes = {app_data, framework_data};
    }

    ogplay::loader::DexImage image;
    std::vector<ogplay::loader::DexClassData> classes;
};

}  // namespace

TEST_CASE("DEX L1 report counts application code native and rendering facts") {
    AnalysisFixture fixture;
    const auto report =
        ogplay::loader::AnalyzeDexL1(fixture.image, fixture.classes);
    CHECK(report.class_count == 2);
    CHECK(report.application_class_count == 1);
    CHECK(report.declared_method_count == 2);
    CHECK(report.native_method_count == 1);
    CHECK(report.instruction_units == 300);
    CHECK(report.rendering_instruction_units == 300);
    CHECK(report.rendering_override_count == 1);
    CHECK(report.java_thickness == ogplay::loader::DexJavaThickness::moderate);
    CHECK(report.dex_execution_candidate);
}

TEST_CASE("DEX engine fingerprints use declarative library and symbol evidence") {
    AnalysisFixture fixture;
    const std::array libraries{
        ogplay::loader::DexNativeLibraryFact{
            "lib/armeabi-v7a/libsample.so", {"engine_boot", "render_step"}}};
    const std::array signatures{
        ogplay::loader::DexEngineSignature{
            "sample-engine", {"libsample.so"}, {"engine_boot"}},
        ogplay::loader::DexEngineSignature{
            "missing-engine", {"libmissing.so"}, {}}};
    const auto report = ogplay::loader::AnalyzeDexL1(
        fixture.image, fixture.classes, libraries, signatures);
    CHECK(report.engine_fingerprints ==
          std::vector<std::string>{"sample-engine"});
}

TEST_CASE("DEX L1 analysis rejects mismatched facts and signature catalogs") {
    AnalysisFixture fixture;
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::AnalyzeDexL1(fixture.image, {})),
        ogplay::loader::DexAnalysisError);
    auto duplicate = fixture.classes;
    duplicate[1].class_def_index = 0;
    CHECK_THROWS_AS(static_cast<void>(ogplay::loader::AnalyzeDexL1(
                        fixture.image, duplicate)),
                    ogplay::loader::DexAnalysisError);
    const std::array invalid{
        ogplay::loader::DexEngineSignature{"", {"lib.so"}, {}}};
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::loader::AnalyzeDexL1(
            fixture.image, fixture.classes, {}, invalid)),
        ogplay::loader::DexAnalysisError);
}

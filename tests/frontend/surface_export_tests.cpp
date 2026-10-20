#include <doctest/doctest.h>
#include "ogplay/app/diagnostics.h"
#include "ogplay/app/json.h"
#include <filesystem>
#include <string>
#include <limits>

TEST_CASE("DIAG-4 surface exports linked intrinsic BootDex constants and missing bindings") {
    const auto json = ogplay::app::DumpDexSurface(std::filesystem::path(OGPLAY_SOURCE_DIR) /
        "data/android/19/framework/bootdex.jar");
    ogplay::app::JsonParseError error;
    const auto doc = ogplay::app::JsonDocument::ParseStrict(json, error, 64U * 1024U * 1024U);
    REQUIRE(doc.has_value());
    const auto classes = doc->Root().Member("classes");
    REQUIRE(classes.has_value());
    bool intrinsic = false, boot = false, constant = false, string_constant = false, float_constant = false, infinity = false, ledger = false, bound = false;
    for (std::size_t i = 0; i < classes->Size(); ++i) {
        const auto cls = *classes->Element(i);
        const auto name = *cls.Member("descriptor")->String();
        if (name == "Ljava/lang/Object;") intrinsic = cls.Member("source")->String() == "intrinsic";
        if (name == "Ljava/util/ArrayList;") boot = cls.Member("source")->String() == "bootdex";
        const auto fields = *cls.Member("fields");
        for (std::size_t j = 0; j < fields.Size(); ++j) {
            const auto field = *fields.Element(j);
            if (name == "Landroid/os/Build$VERSION_CODES;" && field.Member("name")->String() == "KITKAT")
                constant = field.Member("constant")->Integer() == 19;
            if (name == "Ljava/lang/Double;" && field.Member("name")->String() == "MAX_VALUE")
                float_constant = field.Member("constant")->Number() == std::numeric_limits<double>::max();
            if (name == "Ljava/lang/Double;" && field.Member("name")->String() == "POSITIVE_INFINITY")
                infinity = field.Member("constant")->String() == "+Infinity";
            if (name == "Landroid/os/Environment;" && field.Member("name")->String() == "MEDIA_MOUNTED")
                string_constant = field.Member("constant")->String() == "mounted";
        }
        const auto methods = *cls.Member("methods");
        for (std::size_t j = 0; j < methods.Size(); ++j) {
            const auto method = *methods.Element(j);
            if (name == "Landroid/content/res/XmlResourceParser$Impl;" &&
                method.Member("name")->String() == "getDepth")
                ledger = method.Member("status")->String() == "unimplemented-ledger" &&
                    method.Member("binding")->String() == "intrinsic";
            if (name == "Ljava/lang/System;" && method.Member("name")->String() == "loadLibrary")
                bound = method.Member("status")->String() == "implemented";
        }
    }
    CHECK(intrinsic); CHECK(boot); CHECK(constant); CHECK(string_constant); CHECK(float_constant); CHECK(infinity); CHECK(ledger); CHECK(bound);
    CHECK(doc->Root().Member("native_exports")->Size() > 100);
}

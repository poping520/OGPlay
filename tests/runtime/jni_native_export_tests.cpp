#include <doctest/doctest.h>

#include <string>

#include "ogplay/runtime/jni/jni_native_export.h"
#include "ogplay/runtime/jni/jni_signature.h"

TEST_CASE("JNI native export names follow short and long name mangling") {
    const auto names = ogplay::runtime::BuildJniNativeExportNames(
        "org/example/Outer$Inner", "native_init", "(I[Ljava/lang/String;)V");
    CHECK(names.short_name ==
          "Java_org_example_Outer_00024Inner_native_1init");
    CHECK(names.long_name ==
          "Java_org_example_Outer_00024Inner_native_1init__"
          "I_3Ljava_lang_String_2");

    const auto no_arguments = ogplay::runtime::BuildJniNativeExportNames(
        "org/example/Renderer", "render", "()V");
    CHECK(no_arguments.long_name == "Java_org_example_Renderer_render__");
}

TEST_CASE("JNI native export names encode Unicode as UTF-16 code units") {
    const auto names = ogplay::runtime::BuildJniNativeExportNames(
        "org/example/\xCE\xA9\xF0\x9F\x98\x80", "run", "()V");
    CHECK(names.short_name ==
          "Java_org_example__003a9_0d83d_0de00_run");
}

TEST_CASE("JNI native export names reject invalid names and descriptors") {
    CHECK_THROWS_AS(static_cast<void>(ogplay::runtime::BuildJniNativeExportNames(
                        "org.example.Bad", "run", "()V")),
                    ogplay::runtime::JniNativeExportError);
    CHECK_THROWS_AS(static_cast<void>(ogplay::runtime::BuildJniNativeExportNames(
                        "org/example/Bad", "run", "(I")),
                    ogplay::runtime::JniSignatureError);
    const std::string invalid_utf8{"org/example/\xC0\x80", 14};
    CHECK_THROWS_AS(static_cast<void>(ogplay::runtime::BuildJniNativeExportNames(
                        invalid_utf8, "run", "()V")),
                    ogplay::runtime::JniNativeExportError);
}

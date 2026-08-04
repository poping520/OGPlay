#include <string>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni_signature.h"

TEST_CASE("JNI field descriptors preserve primitive object and array facts") {
    using ogplay::runtime::JniTypeKind;
    const auto integer = ogplay::runtime::ParseJniFieldDescriptor("I");
    CHECK(integer.kind == JniTypeKind::integer);
    CHECK(integer.element_kind == JniTypeKind::integer);
    CHECK(integer.ParameterSlots() == 1);
    CHECK_FALSE(integer.IsReference());

    const auto object =
        ogplay::runtime::ParseJniFieldDescriptor("Ljava/lang/String;");
    CHECK(object.kind == JniTypeKind::object);
    CHECK(object.object_class == "java/lang/String");
    CHECK(object.IsReference());

    const auto array =
        ogplay::runtime::ParseJniFieldDescriptor("[[Ljava/lang/String;");
    CHECK(array.kind == JniTypeKind::array);
    CHECK(array.element_kind == JniTypeKind::object);
    CHECK(array.array_dimensions == 2);
    CHECK(array.object_class == "java/lang/String");
    CHECK(array.ParameterSlots() == 1);

    CHECK(ogplay::runtime::ParseJniFieldDescriptor("J").ParameterSlots() == 2);
    CHECK(ogplay::runtime::ParseJniFieldDescriptor("D").ParameterSlots() == 2);
}

TEST_CASE("JNI method descriptors produce deterministic parameter layouts") {
    using ogplay::runtime::JniTypeKind;
    const auto method = ogplay::runtime::ParseJniMethodDescriptor(
        "(ILjava/lang/String;[J)Ljava/lang/Object;");
    REQUIRE(method.parameters.size() == 3);
    CHECK(method.parameters[0].kind == JniTypeKind::integer);
    CHECK(method.parameters[1].object_class == "java/lang/String");
    CHECK(method.parameters[2].kind == JniTypeKind::array);
    CHECK(method.parameters[2].element_kind == JniTypeKind::long_integer);
    CHECK(method.ParameterSlots() == 3);
    CHECK(method.result.kind == JniTypeKind::object);
    CHECK(method.result.object_class == "java/lang/Object");

    const auto wide = ogplay::runtime::ParseJniMethodDescriptor("(JD)V");
    CHECK(wide.ParameterSlots() == 4);
    CHECK(wide.result.kind == JniTypeKind::void_value);
    const auto empty = ogplay::runtime::ParseJniMethodDescriptor("()V");
    CHECK(empty.parameters.empty());
}

TEST_CASE("JNI descriptor parser rejects malformed and oversized inputs") {
    const auto parse_field = [](const std::string& descriptor) {
        static_cast<void>(
            ogplay::runtime::ParseJniFieldDescriptor(descriptor));
    };
    const auto parse_method = [](const std::string& descriptor) {
        static_cast<void>(
            ogplay::runtime::ParseJniMethodDescriptor(descriptor));
    };

    CHECK_THROWS_AS(parse_field(""),
                    ogplay::runtime::JniSignatureError);
    CHECK_THROWS_AS(parse_field("V"),
                    ogplay::runtime::JniSignatureError);
    CHECK_THROWS_AS(parse_field("[V"),
                    ogplay::runtime::JniSignatureError);
    CHECK_THROWS_AS(parse_field("Ljava/lang/String"),
                    ogplay::runtime::JniSignatureError);
    CHECK_THROWS_AS(parse_field("L;"),
                    ogplay::runtime::JniSignatureError);
    CHECK_THROWS_AS(parse_field("Ljava.lang.String;"),
                    ogplay::runtime::JniSignatureError);
    CHECK_THROWS_AS(parse_field("II"),
                    ogplay::runtime::JniSignatureError);
    CHECK_THROWS_AS(parse_method("I)V"),
                    ogplay::runtime::JniSignatureError);
    CHECK_THROWS_AS(parse_method("(V)V"),
                    ogplay::runtime::JniSignatureError);
    CHECK_THROWS_AS(parse_method("(I"),
                    ogplay::runtime::JniSignatureError);
    CHECK_THROWS_AS(parse_method("(I)"),
                    ogplay::runtime::JniSignatureError);
    CHECK_THROWS_AS(parse_method("()VV"),
                    ogplay::runtime::JniSignatureError);

    const std::string too_deep(256, '[');
    CHECK_THROWS_AS(parse_field(too_deep + "I"),
                    ogplay::runtime::JniSignatureError);
    const std::string too_many_parameters =
        "(" + std::string(256, 'I') + ")V";
    CHECK_THROWS_AS(
        parse_method(too_many_parameters),
        ogplay::runtime::JniSignatureError);
}

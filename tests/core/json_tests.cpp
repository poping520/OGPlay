#include "ogplay/core/json.h"

#include <cstdint>
#include <stdexcept>
#include <string>

#include <doctest/doctest.h>

TEST_CASE("strict JSON parses nested UTF-8 values and exact integers") {
    ogplay::core::JsonParseError error;
    auto document = ogplay::core::JsonDocument::ParseStrict(
        R"({"message":"\u6e38\u620f","params":{"limit":18446744073709551615},"items":[true,null]})",
        error);
    REQUIRE(document.has_value());
    const auto root = document->Root();
    REQUIRE(root.IsObject());
    REQUIRE(root.Member("message").has_value());
    const auto message = root.Member("message")->String();
    REQUIRE(message.has_value());
    CHECK(std::string(*message) == "游戏");
    REQUIRE(root.Member("params").has_value());
    const auto limit = root.Member("params")->Member("limit");
    REQUIRE(limit.has_value());
    CHECK(limit->UnsignedInteger() == UINT64_MAX);
    REQUIRE(root.Member("items").has_value());
    CHECK(root.Member("items")->Size() == 2U);
    CHECK(root.Member("items")->Element(0)->Bool() == true);
    CHECK(root.Member("items")->Element(1)->IsNull());
}

TEST_CASE("strict JSON rejects syntax duplicates size and nesting limits") {
    ogplay::core::JsonParseError error;
    CHECK_FALSE(ogplay::core::JsonDocument::ParseStrict("{", error).has_value());
    CHECK(error.kind == ogplay::core::JsonParseErrorKind::syntax);
    CHECK(error.position == 1U);

    CHECK_FALSE(ogplay::core::JsonDocument::ParseStrict(
                    R"({"id":1,"id":2})", error)
                    .has_value());
    CHECK(error.kind == ogplay::core::JsonParseErrorKind::duplicate_key);

    CHECK_FALSE(ogplay::core::JsonDocument::ParseStrict("{}", error, 1U).has_value());
    CHECK(error.kind == ogplay::core::JsonParseErrorKind::size_limit);

    CHECK_FALSE(ogplay::core::JsonDocument::ParseStrict(
                    R"({"a":{"b":1}})", error, 1024U, 2U)
                    .has_value());
    CHECK(error.kind == ogplay::core::JsonParseErrorKind::nesting_limit);

    const ogplay::core::JsonValue empty;
    CHECK_FALSE(empty.IsNull());
    CHECK_FALSE(empty.IsObject());
    CHECK_FALSE(empty.Integer().has_value());
    CHECK_FALSE(empty.Member("value").has_value());
    CHECK(empty.Size() == 0U);

    const ogplay::core::JsonDocument empty_document;
    CHECK_FALSE(empty_document.Root().IsValid());
}

TEST_CASE("JSON writer escapes strings and round trips values through yyjson") {
    ogplay::core::JsonWriter writer;
    const auto root = writer.Object();
    writer.AddString(root, "text", std::string("line\n\0end", 9U));
    writer.AddInteger(root, "negative", -7);
    writer.AddReal(root, "ratio", 1.25);
    const auto values = writer.Array();
    writer.Append(values, writer.Bool(true));
    writer.Append(values, writer.Null());
    writer.Add(root, "values", values);
    const auto text = writer.Serialize(root);

    ogplay::core::JsonParseError error;
    auto parsed = ogplay::core::JsonDocument::ParseStrict(text, error);
    REQUIRE(parsed.has_value());
    CHECK(parsed->Root().Member("text")->String()->size() == 9U);
    CHECK(parsed->Root().Member("negative")->Integer() == -7);
    CHECK(parsed->Root().Member("values")->Size() == 2U);
}

TEST_CASE("JSON writer rejects duplicate object members") {
    ogplay::core::JsonWriter writer;
    const auto root = writer.Object();
    writer.AddString(root, "id", "first");
    CHECK_THROWS_AS(writer.AddString(root, "id", "second"), std::runtime_error);
}

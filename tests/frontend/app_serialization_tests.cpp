#include <doctest/doctest.h>
#include "ogplay/app/json.h"
TEST_CASE("App serialization rejects invalid handles and strict malformed JSON") {
    ogplay::app::JsonWriter writer;
    CHECK_THROWS(static_cast<void>(writer.Serialize({})));
    CHECK_THROWS(writer.Add({}, "key", writer.Null()));
    CHECK_THROWS(writer.Append({}, writer.Null()));
    ogplay::app::JsonParseError error;
    CHECK_FALSE(ogplay::app::JsonDocument::ParseStrict("{\"key\":1,\"key\":2}", error));
    CHECK(error.kind == ogplay::app::JsonParseErrorKind::duplicate_key);
}

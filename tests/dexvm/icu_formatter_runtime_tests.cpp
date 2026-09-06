#include <doctest/doctest.h>

#include "ogplay/runtime/dexvm/icu_formatter_runtime.h"

using ogplay::runtime::dexvm::IcuFormatterRuntime;

TEST_CASE("DVM-102 logical formatter handles clone independently") {
  IcuFormatterRuntime runtime;
  const auto token = runtime.Open(u"0000", {});
  const auto clone = runtime.Clone(token);
  runtime.SetAttribute(clone, 4, 2);
  CHECK(runtime.FormatLong(token, 7).text == u"0007");
  CHECK(runtime.FormatLong(clone, 7).text == u"07");
  runtime.Close(token);
  CHECK_FALSE(runtime.Contains(token));
  CHECK(runtime.Contains(clone));
}

TEST_CASE("DVM-102 formatter tokens reject invalid and duplicate release") {
  IcuFormatterRuntime runtime;
  const auto token = runtime.Open(u"0", {});
  runtime.Close(token);
  CHECK_THROWS_AS(runtime.Close(token), std::invalid_argument);
  CHECK_THROWS_AS(static_cast<void>(runtime.FormatLong(0, 1)),
                  std::invalid_argument);
}

TEST_CASE("DVM-102 formatter parse and teardown are bounded") {
  IcuFormatterRuntime runtime;
  const auto token = runtime.Open(u"#,##0", {});
  const auto formatted = runtime.FormatLong(token, -12345);
  CHECK(formatted.text == u"-12,345");
  const auto parsed = runtime.ParseInteger(token, u"-12,345 tail", 0);
  REQUIRE(parsed.has_value());
  CHECK(parsed->value == -12345);
  CHECK(parsed->end == 7);
  runtime.Clear();
  CHECK(runtime.Size() == 0);
}

// loader.dex_code checked reading over dexasm fixtures.
// Structure rules follow AOSP libdex DexFile.h / DexCatch.h at the pinned
// baseline; negative cases mirror DexSwapVerify's per-section strictness.

#include <doctest/doctest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "ogplay/loader/dex.h"
#include "ogplay/loader/dex_class_data.h"
#include "ogplay/loader/dex_code.h"

namespace {

std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

std::string Ascii(const ogplay::loader::DexString& value) {
    std::string out;
    for (const auto unit : value.value) out.push_back(static_cast<char>(unit));
    return out;
}

struct Fixture final {
    std::vector<std::uint8_t> bytes;
    ogplay::loader::DexImage image;
    std::vector<ogplay::loader::DexClassData> class_data;

    Fixture() : bytes(ReadFixture("core.dex")),
                image(ogplay::loader::ParseDex(bytes)),
                class_data(ogplay::loader::ReadDexClassData(bytes, image)) {}

    [[nodiscard]] const ogplay::loader::DexEncodedMethod& Method(
        const std::string& name) const {
        for (const auto& data : class_data) {
            for (const auto* list : {&data.direct_methods,
                                     &data.virtual_methods}) {
                for (const auto& method : *list) {
                    const auto& id = image.methods[method.method_index];
                    if (Ascii(image.strings[id.name_string_index]) == name) {
                        return method;
                    }
                }
            }
        }
        FAIL("method not found: ", name);
        throw std::logic_error("unreachable");
    }
};

}  // namespace

TEST_CASE("dex_code reads instruction stream and try blocks") {
    const Fixture fixture;

    const auto& divide = fixture.Method("divide");
    REQUIRE(divide.code.has_value());
    const auto code = ogplay::loader::ReadDexMethodCode(
        fixture.bytes, fixture.image, *divide.code);
    CHECK(code.instructions.size() == divide.code->instruction_units);
    REQUIRE(code.tries.size() == 1);
    const auto& block = code.tries[0];
    CHECK(block.start_pc == 0);
    CHECK(block.instruction_count == 2);
    REQUIRE(block.typed_handlers.size() == 1);
    CHECK(!block.catch_all_pc.has_value());
    CHECK(fixture.image.types[block.typed_handlers[0].type_index]
              .descriptor == "Ljava/lang/ArithmeticException;");
    CHECK(block.typed_handlers[0].handler_pc < code.instructions.size());

    // div-int is opcode 0x93 (23x): first unit low byte must match.
    CHECK((code.instructions[0] & 0xffU) == 0x93U);
}

TEST_CASE("dex_code reads switch payload instruction units verbatim") {
    const Fixture fixture;
    const auto& pick = fixture.Method("pick");
    REQUIRE(pick.code.has_value());
    const auto code = ogplay::loader::ReadDexMethodCode(
        fixture.bytes, fixture.image, *pick.code);
    // packed-switch payload ident 0x0100 and sparse ident 0x0200 are present.
    bool packed = false;
    bool sparse = false;
    for (const auto unit : code.instructions) {
        packed |= unit == 0x0100U;
        sparse |= unit == 0x0200U;
    }
    CHECK(packed);
    CHECK(sparse);
}

TEST_CASE("dex_code reads static initial values") {
    const Fixture fixture;
    const auto& fixture_class = fixture.image.classes[1];
    REQUIRE(fixture_class.static_values_offset != 0);
    const auto values = ogplay::loader::ReadDexStaticValues(
        fixture.bytes, fixture.image, fixture_class.static_values_offset);
    REQUIRE(values.size() == 2);
    CHECK(values[0].kind == ogplay::loader::DexEncodedValueKind::int_value);
    CHECK(values[0].integral == 41);
    CHECK(values[1].kind ==
          ogplay::loader::DexEncodedValueKind::string_index);
    CHECK(Ascii(fixture.image.strings[values[1].index]) == "fixture");
}

TEST_CASE("dex_code rejects malformed structures") {
    const Fixture fixture;

    SUBCASE("static values offset outside data section") {
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::loader::ReadDexStaticValues(
                fixture.bytes, fixture.image, 4)),
            ogplay::loader::DexError);
    }
    SUBCASE("instruction stream truncated by header lie") {
        const auto& divide = fixture.Method("divide");
        auto lied = *divide.code;
        lied.instruction_units = 0x00ffffffU;
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::loader::ReadDexMethodCode(
                fixture.bytes, fixture.image, lied)),
            ogplay::loader::DexError);
    }
    SUBCASE("byte size mismatch is rejected") {
        auto truncated = fixture.bytes;
        truncated.pop_back();
        const auto& divide = fixture.Method("divide");
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::loader::ReadDexMethodCode(
                truncated, fixture.image, *divide.code)),
            ogplay::loader::DexError);
    }
    SUBCASE("corrupted catch handler address is rejected") {
        // Locate the divide try handler and force its pc out of range.
        const auto& divide = fixture.Method("divide");
        auto bytes = fixture.bytes;
        const std::size_t insns_end =
            divide.code->offset + 16U + divide.code->instruction_units * 2U;
        const std::size_t tries_offset =
            insns_end + ((divide.code->instruction_units & 1U) != 0 ? 2U : 0U);
        const std::size_t handlers_offset = tries_offset + 8U;
        // handlers: count(uleb)=1, size(sleb)=1, type(uleb), addr(uleb)
        std::size_t cursor = handlers_offset + 2U;
        while ((bytes[cursor] & 0x80U) != 0) ++cursor;
        ++cursor;  // past type index
        bytes[cursor] = 0x7f;  // absurd handler pc (single-byte uleb)
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::loader::ReadDexMethodCode(
                bytes, fixture.image, *divide.code)),
            ogplay::loader::DexError);
    }
}

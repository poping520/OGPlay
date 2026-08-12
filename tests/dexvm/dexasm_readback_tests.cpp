// dexasm output cross-checked by the independent C++ L1 parser
// (docs/design/dexvm/05-verification.md §1: golden locks bytes, readback
// locks structure). Fixtures are assembled at build time by tools/dexasm.py.

#include <doctest/doctest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "ogplay/loader/dex.h"
#include "ogplay/loader/dex_class_data.h"

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
    for (const auto unit : value.value) {
        REQUIRE(unit <= 0x7f);
        out.push_back(static_cast<char>(unit));
    }
    return out;
}

}  // namespace

TEST_CASE("dexasm core fixture parses through the strict L1 parser") {
    const auto bytes = ReadFixture("core.dex");
    const auto image = ogplay::loader::ParseDex(bytes);

    CHECK(image.header.version == "035");
    REQUIRE(image.classes.size() == 2);

    const auto& base = image.classes[0];
    const auto& fixture = image.classes[1];
    CHECK(image.types[base.class_type_index].descriptor == "LBase;");
    CHECK(image.types[fixture.class_type_index].descriptor == "LFixture;");
    REQUIRE(fixture.superclass_type_index.has_value());
    CHECK(image.types[*fixture.superclass_type_index].descriptor == "LBase;");
    REQUIRE(fixture.interface_type_indices.size() == 1);
    CHECK(image.types[fixture.interface_type_indices[0]].descriptor ==
          "Ljava/lang/Runnable;");
    CHECK(fixture.static_values_offset != 0);

    const auto class_data = ogplay::loader::ReadDexClassData(bytes, image);
    REQUIRE(class_data.size() == 2);

    const auto& fixture_data = class_data[1];
    CHECK(fixture_data.static_fields.size() == 2);
    CHECK(fixture_data.instance_fields.size() == 1);

    std::size_t with_code = 0;
    std::size_t native_count = 0;
    bool saw_divide_try = false;
    bool saw_wide = false;
    for (const auto& method : fixture_data.direct_methods) {
        const auto& id = image.methods[method.method_index];
        const auto name = Ascii(image.strings[id.name_string_index]);
        if (method.code.has_value()) ++with_code;
        if ((method.access_flags & 0x0100u) != 0) {
            ++native_count;
            CHECK(!method.code.has_value());
            CHECK(name == "nativeHook");
        }
        if (name == "divide") {
            REQUIRE(method.code.has_value());
            CHECK(method.code->registers_size == 4);
            CHECK(method.code->incoming_words == 2);
            CHECK(method.code->tries_size == 1);
            saw_divide_try = true;
        }
        if (name == "wide") {
            REQUIRE(method.code.has_value());
            CHECK(method.code->instruction_units == 6);
            saw_wide = true;
        }
    }
    CHECK(saw_divide_try);
    CHECK(saw_wide);
    CHECK(native_count == 1);
    CHECK(with_code >= 4);

    // Virtual methods: <init> is direct; run/value are virtual.
    std::vector<std::string> virtual_names;
    for (const auto& method : fixture_data.virtual_methods) {
        const auto& id = image.methods[method.method_index];
        virtual_names.push_back(Ascii(image.strings[id.name_string_index]));
    }
    CHECK(virtual_names == std::vector<std::string>{"run", "value"});
}

TEST_CASE("corrupted dexasm output is rejected by the strict parser") {
    auto bytes = ReadFixture("core.dex");
    SUBCASE("truncated file") {
        bytes.resize(bytes.size() / 2);
        CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(bytes)),
                        ogplay::loader::DexError);
    }
    SUBCASE("corrupted file_size field") {
        bytes[0x20] ^= 0xff;
        CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(bytes)),
                        ogplay::loader::DexError);
    }
    SUBCASE("bad magic") {
        bytes[0] = 'x';
        CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ParseDex(bytes)),
                        ogplay::loader::DexError);
    }
}

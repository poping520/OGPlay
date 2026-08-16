// resources.arsc strict reader tests: a synthetic minimal table for CI and
// an exact-APK cross-check that runs when the local demo APK is present.

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ogplay/loader/apk.h"
#include "ogplay/loader/arsc.h"

namespace {

void PushU16(std::vector<std::uint8_t>& out, const std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
}
void PushU32(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    PushU16(out, static_cast<std::uint16_t>(value & 0xffffU));
    PushU16(out, static_cast<std::uint16_t>(value >> 16U));
}
void PatchU32(std::vector<std::uint8_t>& out, const std::size_t offset,
              const std::uint32_t value) {
    out[offset] = static_cast<std::uint8_t>(value & 0xffU);
    out[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    out[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    out[offset + 3] = static_cast<std::uint8_t>(value >> 24U);
}

std::vector<std::uint8_t> Utf8Pool(
    const std::vector<std::string>& strings) {
    std::vector<std::uint8_t> pool;
    PushU16(pool, 0x0001);  // RES_STRING_POOL_TYPE
    PushU16(pool, 28);      // header size
    PushU32(pool, 0);       // size (patched)
    PushU32(pool, static_cast<std::uint32_t>(strings.size()));
    PushU32(pool, 0);  // style count
    PushU32(pool, 1U << 8U);  // UTF-8 flag
    PushU32(pool, 0);  // strings start (patched)
    PushU32(pool, 0);  // styles start
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint8_t> data;
    for (const auto& value : strings) {
        offsets.push_back(static_cast<std::uint32_t>(data.size()));
        data.push_back(static_cast<std::uint8_t>(value.size()));
        data.push_back(static_cast<std::uint8_t>(value.size()));
        for (const auto character : value) {
            data.push_back(static_cast<std::uint8_t>(character));
        }
        data.push_back(0);
    }
    for (const auto offset : offsets) PushU32(pool, offset);
    const auto strings_start = pool.size();
    pool.insert(pool.end(), data.begin(), data.end());
    while (pool.size() % 4 != 0) pool.push_back(0);
    PatchU32(pool, 4, static_cast<std::uint32_t>(pool.size()));
    PatchU32(pool, 20, static_cast<std::uint32_t>(strings_start));
    return pool;
}

std::vector<std::uint8_t> SyntheticArsc() {
    // Global pool: one path string. Type pool: ["raw"]. Key pool:
    // ["raw_000", "raw_001"].
    const auto global = Utf8Pool({"res/raw/raw_000.ogg"});
    const auto type_pool = Utf8Pool({"raw"});
    const auto key_pool = Utf8Pool({"raw_000", "raw_001"});

    // Type chunk: two entries, second one absent.
    std::vector<std::uint8_t> type_chunk;
    PushU16(type_chunk, 0x0201);
    PushU16(type_chunk, 20 + 36);  // header size incl. minimal config
    PushU32(type_chunk, 0);        // size (patched)
    type_chunk.push_back(1);       // type id
    type_chunk.push_back(0);
    PushU16(type_chunk, 0);
    PushU32(type_chunk, 2);        // entry count
    PushU32(type_chunk, 0);        // entries start (patched)
    PushU32(type_chunk, 36);       // config size
    for (int filler = 0; filler < 32; ++filler) type_chunk.push_back(0);
    PushU32(type_chunk, 0);           // entry 0 offset
    PushU32(type_chunk, 0xFFFFFFFF);  // entry 1 absent
    const auto entries_start = type_chunk.size();
    PatchU32(type_chunk, 16, static_cast<std::uint32_t>(entries_start));
    // entry 0: ResTable_entry + Res_value(TYPE_STRING -> global[0])
    PushU16(type_chunk, 8);   // entry size
    PushU16(type_chunk, 0);   // flags
    PushU32(type_chunk, 0);   // key index -> "raw_000"
    PushU16(type_chunk, 8);   // value size
    type_chunk.push_back(0);  // res0
    type_chunk.push_back(0x03);  // TYPE_STRING
    PushU32(type_chunk, 0);      // global string 0
    PatchU32(type_chunk, 4, static_cast<std::uint32_t>(type_chunk.size()));

    // Package chunk.
    std::vector<std::uint8_t> package;
    PushU16(package, 0x0200);
    PushU16(package, 288);
    PushU32(package, 0);  // size (patched)
    PushU32(package, 0x7f);
    for (int unit = 0; unit < 128; ++unit) {
        PushU16(package, unit < 4 ? static_cast<std::uint16_t>("test"[unit])
                                  : 0);
    }
    const std::size_t type_strings_offset_at = package.size();
    PushU32(package, 0);  // typeStrings (patched)
    PushU32(package, 0);  // lastPublicType
    const std::size_t key_strings_offset_at = package.size();
    PushU32(package, 0);  // keyStrings (patched)
    PushU32(package, 0);  // lastPublicKey
    while (package.size() < 288) package.push_back(0);
    PatchU32(package, type_strings_offset_at,
             static_cast<std::uint32_t>(package.size()));
    package.insert(package.end(), type_pool.begin(), type_pool.end());
    PatchU32(package, key_strings_offset_at,
             static_cast<std::uint32_t>(package.size()));
    package.insert(package.end(), key_pool.begin(), key_pool.end());
    package.insert(package.end(), type_chunk.begin(), type_chunk.end());
    PatchU32(package, 4, static_cast<std::uint32_t>(package.size()));

    std::vector<std::uint8_t> table;
    PushU16(table, 0x0002);
    PushU16(table, 12);
    PushU32(table, 0);  // size (patched)
    PushU32(table, 1);  // package count
    table.insert(table.end(), global.begin(), global.end());
    table.insert(table.end(), package.begin(), package.end());
    PatchU32(table, 4, static_cast<std::uint32_t>(table.size()));
    return table;
}

}  // namespace

TEST_CASE("arsc reader maps ids, names and file paths") {
    const auto bytes = SyntheticArsc();
    const auto table = ogplay::loader::ParseArsc(bytes);
    CHECK(table.package_id == 0x7f);
    CHECK(table.package_name == "test");
    REQUIRE(table.entries.size() == 1);
    const auto* by_id = table.FindById(0x7f010000);
    REQUIRE(by_id != nullptr);
    CHECK(by_id->type_name == "raw");
    CHECK(by_id->entry_name == "raw_000");
    REQUIRE(by_id->string_value.has_value());
    CHECK(*by_id->string_value == "res/raw/raw_000.ogg");
    CHECK(by_id->value_type == 0x03);
    CHECK(by_id->value_data == 0);
    const auto* by_name = table.FindByName("raw", "raw_000");
    REQUIRE(by_name != nullptr);
    CHECK(by_name->resource_id == 0x7f010000);
    CHECK(table.FindById(0x7f010001) == nullptr);
}

TEST_CASE("arsc reader rejects truncated tables") {
    auto bytes = SyntheticArsc();
    bytes.resize(bytes.size() / 2);
    CHECK_THROWS(static_cast<void>(ogplay::loader::ParseArsc(bytes)));
}

TEST_CASE("arsc reader agrees with the local exact APK when present") {
    const auto apk_path =
        std::filesystem::path(OGPLAY_SOURCE_DIR) / "docs" / "demo" /
        "games" / "Asphalt_5_1.1.3_Samsungapps.apk";
    if (!std::filesystem::exists(apk_path)) {
        return;  // exact data is local-only; CI has no game payloads
    }
    std::ifstream stream(apk_path, std::ios::binary);
    std::vector<std::byte> apk(
        static_cast<std::size_t>(std::filesystem::file_size(apk_path)));
    stream.read(reinterpret_cast<char*>(apk.data()),
                static_cast<std::streamsize>(apk.size()));
    const auto archive = ogplay::loader::ParseApkArchive(apk);
    const auto entry = ogplay::loader::ReadApkEntry(
        apk, archive, "resources.arsc");
    const auto table = ogplay::loader::ParseArsc(
        std::span(reinterpret_cast<const std::uint8_t*>(entry.data()),
                  entry.size()));
    CHECK(table.package_id == 0x7f);
    // The sound-pool base id observed in the interpreted glue is
    // 0x7f040009 + n; entry 9 of type "raw" must be raw_000.
    const auto* base = table.FindById(0x7f040009);
    REQUIRE(base != nullptr);
    CHECK(base->type_name == "raw");
    CHECK(base->entry_name == "raw_000");
    REQUIRE(base->string_value.has_value());
    CHECK(*base->string_value == "res/raw/raw_000.ogg");
}

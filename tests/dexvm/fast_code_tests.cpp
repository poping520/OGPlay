#include <doctest/doctest.h>

#include "ogplay/runtime/dexvm/fast_code.h"
#include "ogplay/runtime/dexvm/dexvm_types.h"

namespace ogplay::runtime::dexvm {
namespace {

loader::DexMethodCode Code(std::initializer_list<std::uint16_t> units) {
    loader::DexMethodCode code;
    code.instructions.assign(units);
    return code;
}

}  // namespace

TEST_CASE("FastCode maps executable dex pcs and preassembles operands") {
    // const/4 v0, #-1; const v1, #0x12345678; goto +1; return v1
    const auto fast = BuildFastCode(
        Code({0xf012U, 0x0114U, 0x5678U, 0x1234U, 0x0128U, 0x010fU}),
        "LFast;.linear");
    REQUIRE(fast.instructions.size() == 4);
    CHECK(fast.instructions[0].handler == FastHandler::straight);
    CHECK(fast.instructions[0].a == 0);
    CHECK(static_cast<std::int64_t>(fast.instructions[0].extra) == -1);
    CHECK(fast.instructions[1].a == 1);
    CHECK(fast.instructions[1].extra == 0x12345678U);
    CHECK(fast.instructions[2].branch_target == 3);
    CHECK(fast.IndexForDexPc(5) == 3);
    CHECK_THROWS_AS(static_cast<void>(fast.IndexForDexPc(2)),
                    std::out_of_range);
    CHECK(fast.storage_bytes >= fast.instructions.size() * sizeof(FastInstruction));

    const auto branch =
        BuildFastCode(Code({0x2132U, 0x0002U, 0x000eU}), "LFast;.ifEq");
    CHECK(branch.instructions[0].a == 1);
    CHECK(branch.instructions[0].b == 2);
    CHECK(branch.instructions[0].branch_target == 1);
}

TEST_CASE("FastCode parses switch and array payload side tables") {
    // packed-switch v0, payload@4; return-void; payload {key=7,target=3}
    const auto packed = BuildFastCode(
        Code({0x002bU, 0x0004U, 0x0000U, 0x000eU,
              0x0100U, 0x0001U, 0x0007U, 0x0000U, 0x0003U, 0x0000U}),
        "LFast;.packed");
    REQUIRE(packed.payloads.size() == 1);
    CHECK(packed.instructions[0].handler == FastHandler::object_fast);
    CHECK(packed.payloads[0].kind == FastPayloadKind::packed_switch);
    CHECK(packed.payloads[0].keys == std::vector<std::int32_t>{7});
    CHECK(packed.payloads[0].targets == std::vector<std::uint32_t>{1});

    // fill-array-data v0, payload@4; return-void; byte[3] {1,2,3}
    const auto array = BuildFastCode(
        Code({0x0026U, 0x0004U, 0x0000U, 0x000eU,
              0x0300U, 0x0001U, 0x0003U, 0x0000U, 0x0201U, 0x0003U}),
        "LFast;.array");
    REQUIRE(array.payloads.size() == 1);
    CHECK(array.payloads[0].kind == FastPayloadKind::array_data);
    CHECK(array.payloads[0].element_width == 1);
    CHECK(array.payloads[0].element_count == 3);
    CHECK(array.payloads[0].data_units ==
          std::vector<std::uint16_t>{0x0201U, 0x0003U});
}

TEST_CASE("FastCode rejects malformed boundaries and payload references") {
    CHECK_THROWS_AS(static_cast<void>(BuildFastCode(
                        Code({0x0114U}), "LBad;.truncated")),
                    DexVmError);
    CHECK_THROWS_AS(
        static_cast<void>(BuildFastCode(
            Code({0x002bU, 0x0003U, 0x0000U, 0x000eU}),
            "LBad;.payload")),
        DexVmError);
    CHECK_THROWS_AS(static_cast<void>(BuildFastCode(
                        Code({0x0228U, 0x000eU}), "LBad;.branch")),
                    DexVmError);
}

}  // namespace ogplay::runtime::dexvm

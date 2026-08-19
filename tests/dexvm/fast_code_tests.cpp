#include <doctest/doctest.h>

#include <string>

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

void ExpectFastCodeRejects(std::initializer_list<std::uint16_t> units,
                           const char* where, const char* detail,
                           const DexVmErrorReason reason) {
    try {
        static_cast<void>(BuildFastCode(Code(units), where));
        FAIL("expected DexVmError");
    } catch (const DexVmError& error) {
        CHECK(error.Reason() == reason);
        CHECK(std::string(error.what()) ==
              std::string(where) + ": " + detail);
    }
}

TEST_CASE("FastCode rejects malformed boundaries and payload references") {
    ExpectFastCodeRejects({0x0114U}, "LBad;.truncated",
                          "instruction exceeds method end",
                          DexVmErrorReason::invalid_code);
    ExpectFastCodeRejects({0x0228U, 0x000eU}, "LBad;.branch",
                          "branch target out of method",
                          DexVmErrorReason::invalid_code);
    ExpectFastCodeRejects({0x002bU, 0x0003U, 0x0000U, 0x000eU},
                          "LBad;.payload",
                          "payload reference does not hit a payload",
                          DexVmErrorReason::invalid_code);
    ExpectFastCodeRejects({0x00ffU}, "LBad;.opcode", "rejected opcode 255",
                          DexVmErrorReason::invalid_opcode);
    ExpectFastCodeRejects({0x6071U, 0x0000U, 0x0000U, 0x000eU},
                          "LBad;.invoke",
                          "35c register count exceeds 5 at pc 0",
                          DexVmErrorReason::invalid_code);
}

TEST_CASE("FastCode round-trips opcode, dex pc, and invoke words") {
    const auto units = std::vector<std::uint16_t>{
        0xf012U, 0x0114U, 0x5678U, 0x1234U, 0x0128U, 0x010fU};
    const auto fast = BuildFastCode(Code({0xf012U, 0x0114U, 0x5678U, 0x1234U,
                                          0x0128U, 0x010fU}),
                                    "LFast;.linear");
    REQUIRE(fast.instructions.size() == 4);
    for (std::uint32_t index = 0; index < fast.instructions.size(); ++index) {
        const auto& instruction = fast.instructions[index];
        CHECK((units[instruction.dex_pc] & 0xffU) == instruction.opcode);
        CHECK(fast.IndexForDexPc(instruction.dex_pc) == index);
    }
    CHECK(fast.instructions[2].branch_target == 3);
    CHECK(fast.instructions[3].dex_pc == 5);

    const auto invoke = BuildFastCode(
        Code({0x2071U, 0x0003U, 0x0021U, 0x000eU}), "LFast;.invoke");
    REQUIRE(invoke.instructions.size() == 2);
    CHECK((0x2071U & 0xffU) == invoke.instructions[0].opcode);
    CHECK(invoke.invokes[0].registers == std::vector<std::uint16_t>{1, 2});
    CHECK(invoke.IndexForDexPc(3) == 1);

    const auto packed = BuildFastCode(
        Code({0x002bU, 0x0004U, 0x0000U, 0x000eU, 0x0100U, 0x0001U, 0x0007U,
              0x0000U, 0x0003U, 0x0000U}),
        "LFast;.packed");
    CHECK((0x002bU & 0xffU) == packed.instructions[0].opcode);
    CHECK(packed.payloads[0].keys == std::vector<std::int32_t>{7});
    CHECK(packed.payloads[0].targets == std::vector<std::uint32_t>{1});
    CHECK(packed.instructions[1].opcode == 0x0eU);
}

TEST_CASE("FastCode preassembles invoke register words") {
    const auto fast = BuildFastCode(
        Code({0x2071U, 0x0003U, 0x0021U, 0x000eU}),
        "LFast;.invoke");
    REQUIRE(fast.invokes.size() == 1);
    CHECK(fast.instructions[0].handler == FastHandler::invoke_checked);
    CHECK(fast.invokes[0].base_opcode == 0x71U);
    CHECK(fast.invokes[0].registers ==
          std::vector<std::uint16_t>{1, 2});
}

}  // namespace ogplay::runtime::dexvm

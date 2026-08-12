// Lazy structural method precheck (02 §5 step 4). Rule subset follows AOSP
// vm/analysis/CodeVerify.cpp structural checks at the pinned baseline; full
// type-inference dataflow is intentionally not performed — runtime tagged
// registers catch type confusion with pc diagnostics.

#include <set>

#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/generated/opcode_table.h"

namespace ogplay::runtime::dexvm {
namespace {

namespace gen = ogplay::runtime::dexvm::generated;

struct Decoded final {
    std::uint8_t opcode{};
    std::uint32_t width{};
    bool is_payload{};
};

[[nodiscard]] Decoded DecodeAt(const std::vector<std::uint16_t>& units,
                               const std::uint32_t pc,
                               const std::string& where) {
    const auto unit = units[pc];
    const auto opcode = static_cast<std::uint8_t>(unit & 0xffU);
    if (opcode == 0x00 && (unit >> 8U) != 0) {
        // Payload pseudo-instructions.
        const auto ident = unit;
        std::uint64_t width{};
        if (ident == 0x0100U) {  // packed-switch payload
            if (pc + 2 > units.size()) {
                throw DexVmError(DexVmErrorReason::invalid_code,
                                 where + ": packed payload truncated");
            }
            width = 4ULL + 2ULL * units[pc + 1];
        } else if (ident == 0x0200U) {  // sparse-switch payload
            if (pc + 2 > units.size()) {
                throw DexVmError(DexVmErrorReason::invalid_code,
                                 where + ": sparse payload truncated");
            }
            width = 2ULL + 4ULL * units[pc + 1];
        } else if (ident == 0x0300U) {  // fill-array-data payload
            if (pc + 4 > units.size()) {
                throw DexVmError(DexVmErrorReason::invalid_code,
                                 where + ": array payload truncated");
            }
            const auto element_width = units[pc + 1];
            const auto count = static_cast<std::uint32_t>(units[pc + 2]) |
                               (static_cast<std::uint32_t>(units[pc + 3])
                                << 16U);
            width = 4ULL +
                    (static_cast<std::uint64_t>(element_width) * count + 1U) /
                        2U;
        } else {
            throw DexVmError(DexVmErrorReason::invalid_opcode,
                             where + ": unknown payload ident");
        }
        if (width > units.size() - pc) {
            throw DexVmError(DexVmErrorReason::invalid_code,
                             where + ": payload exceeds method end");
        }
        return {opcode, static_cast<std::uint32_t>(width), true};
    }
    const auto& info = gen::kDexOpcodeTable[opcode];
    if (!info.defined) {
        throw DexVmError(
            DexVmErrorReason::invalid_opcode,
            where + ": rejected opcode " + std::to_string(opcode));
    }
    if (info.width > units.size() - pc) {
        throw DexVmError(DexVmErrorReason::invalid_code,
                         where + ": instruction exceeds method end");
    }
    return {opcode, info.width, false};
}

}  // namespace

void DexClassLinker::PrecheckMethod(const VmMethodId id) {
    auto& method = MutableMethod(id);
    if (method.prechecked) return;
    if (method.kind != MethodKind::interpreted) {
        method.prechecked = true;
        return;
    }
    const auto& code = *method.code;
    const auto& units = code.instructions;
    const auto registers = code.info.registers_size;
    const auto where = Class(method.owner).descriptor + "." + method.name;
    if (units.empty()) {
        throw DexVmError(DexVmErrorReason::invalid_code,
                         where + ": empty instruction stream");
    }
    if (method.ins_words > registers) {
        throw DexVmError(DexVmErrorReason::invalid_code,
                         where + ": ins exceed registers");
    }

    // Pass 1: decode boundaries.
    std::set<std::uint32_t> boundaries;
    std::set<std::uint32_t> payload_starts;
    std::uint32_t pc = 0;
    while (pc < units.size()) {
        const auto decoded = DecodeAt(units, pc, where);
        if (decoded.is_payload) {
            payload_starts.insert(pc);
        } else {
            boundaries.insert(pc);
        }
        pc += decoded.width;
    }
    if (pc != units.size()) {
        throw DexVmError(DexVmErrorReason::invalid_code,
                         where + ": instruction stream is misaligned");
    }

    // Pass 2: operand structural rules.
    std::uint8_t previous_opcode = 0;
    bool previous_sets_result = false;
    for (const auto instruction_pc : boundaries) {
        const auto unit = units[instruction_pc];
        const auto opcode = static_cast<std::uint8_t>(unit & 0xffU);
        const auto& info = gen::kDexOpcodeTable[opcode];

        const auto check_register = [&](const std::uint32_t reg,
                                        const bool wide) {
            const std::uint32_t needed = wide ? 2U : 1U;
            if (reg >= registers || registers - reg < needed) {
                throw DexVmError(
                    DexVmErrorReason::invalid_register,
                    where + ": register out of range at pc " +
                        std::to_string(instruction_pc));
            }
        };
        const bool wide_dest =
            info.name.find("-wide") != std::string_view::npos;

        using gen::DexInstructionFormat;
        switch (info.format) {
            case DexInstructionFormat::k12x:
                check_register((unit >> 8U) & 0xfU, wide_dest);
                check_register((unit >> 12U) & 0xfU, wide_dest);
                break;
            case DexInstructionFormat::k22x:
                check_register((unit >> 8U) & 0xffU, wide_dest);
                check_register(units[instruction_pc + 1], wide_dest);
                break;
            case DexInstructionFormat::k32x:
                check_register(units[instruction_pc + 1], wide_dest);
                check_register(units[instruction_pc + 2], wide_dest);
                break;
            case DexInstructionFormat::k11n:
                check_register((unit >> 8U) & 0xfU, wide_dest);
                break;
            case DexInstructionFormat::k11x:
            case DexInstructionFormat::k21s:
            case DexInstructionFormat::k21h:
            case DexInstructionFormat::k21c:
            case DexInstructionFormat::k31i:
            case DexInstructionFormat::k31c:
            case DexInstructionFormat::k51l:
                check_register((unit >> 8U) & 0xffU, wide_dest);
                break;
            case DexInstructionFormat::k23x:
                check_register((unit >> 8U) & 0xffU, wide_dest);
                check_register(units[instruction_pc + 1] & 0xffU, false);
                check_register((units[instruction_pc + 1] >> 8U) & 0xffU,
                               false);
                break;
            case DexInstructionFormat::k22b:
                // AA|op CC|BB: the second unit's high byte is a signed
                // literal, not a register (libdex/InstrUtils.cpp).
                check_register((unit >> 8U) & 0xffU, wide_dest);
                check_register(units[instruction_pc + 1] & 0xffU, false);
                break;
            case DexInstructionFormat::k22t:
            case DexInstructionFormat::k22s:
            case DexInstructionFormat::k22c:
                check_register((unit >> 8U) & 0xfU, false);
                check_register((unit >> 12U) & 0xfU, false);
                break;
            case DexInstructionFormat::k21t:
                check_register((unit >> 8U) & 0xffU, false);
                break;
            default:
                break;
        }

        // Branch target validation.
        const bool is_branch = (info.flags & gen::kFlagBranch) != 0;
        if (is_branch || (info.flags & gen::kFlagSwitch) != 0 ||
            info.format == DexInstructionFormat::k31t) {
            std::int32_t offset{};
            switch (info.format) {
                case DexInstructionFormat::k10t:
                    offset = static_cast<std::int8_t>((unit >> 8U) & 0xffU);
                    break;
                case DexInstructionFormat::k20t:
                case DexInstructionFormat::k21t:
                case DexInstructionFormat::k22t:
                    offset = static_cast<std::int16_t>(
                        units[instruction_pc + 1]);
                    break;
                case DexInstructionFormat::k30t:
                case DexInstructionFormat::k31t:
                    offset = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(
                            units[instruction_pc + 1]) |
                        (static_cast<std::uint32_t>(
                             units[instruction_pc + 2])
                         << 16U));
                    break;
                default:
                    break;
            }
            const auto target =
                static_cast<std::int64_t>(instruction_pc) + offset;
            if (target < 0 ||
                target >= static_cast<std::int64_t>(units.size())) {
                throw DexVmError(DexVmErrorReason::invalid_code,
                                 where + ": branch target out of method");
            }
            const auto target_pc = static_cast<std::uint32_t>(target);
            if (info.format == DexInstructionFormat::k31t) {
                if (!payload_starts.contains(target_pc)) {
                    throw DexVmError(
                        DexVmErrorReason::invalid_code,
                        where + ": payload reference does not hit a payload");
                }
                const auto ident = units[target_pc];
                const bool fill = info.name == "fill-array-data";
                const bool packed = info.name == "packed-switch";
                const bool sparse = info.name == "sparse-switch";
                if ((fill && ident != 0x0300U) ||
                    (packed && ident != 0x0100U) ||
                    (sparse && ident != 0x0200U)) {
                    throw DexVmError(DexVmErrorReason::invalid_code,
                                     where + ": payload ident mismatch");
                }
            } else if (!boundaries.contains(target_pc)) {
                throw DexVmError(
                    DexVmErrorReason::invalid_code,
                    where + ": branch target is not an instruction boundary");
            }
        }

        // move-result* must directly follow an invoke or filled-new-array.
        if (info.name.starts_with("move-result")) {
            if (!previous_sets_result) {
                throw DexVmError(
                    DexVmErrorReason::invalid_code,
                    where + ": move-result without preceding invoke at pc " +
                        std::to_string(instruction_pc));
            }
        }
        previous_sets_result =
            (info.flags & gen::kFlagInvoke) != 0 ||
            info.name.starts_with("filled-new-array");
        previous_opcode = opcode;
        (void)previous_opcode;
    }
    method.prechecked = true;
}

}  // namespace ogplay::runtime::dexvm

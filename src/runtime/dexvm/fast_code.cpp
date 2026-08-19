#include "ogplay/runtime/dexvm/fast_code.h"

#include <limits>
#include <stdexcept>
#include <unordered_map>

#include "ogplay/runtime/dexvm/dexvm_types.h"
#include "ogplay/runtime/dexvm/generated/opcode_table.h"

namespace ogplay::runtime::dexvm {
namespace {

namespace gen = ogplay::runtime::dexvm::generated;

[[noreturn]] void Invalid(const std::string_view where,
                          const std::string& detail) {
    throw DexVmError(DexVmErrorReason::invalid_code,
                     std::string(where) + ": " + detail);
}

[[nodiscard]] std::uint32_t U32(const std::vector<std::uint16_t>& units,
                                const std::uint32_t offset) {
    return static_cast<std::uint32_t>(units[offset]) |
           (static_cast<std::uint32_t>(units[offset + 1]) << 16U);
}

[[nodiscard]] std::int32_t S32(const std::vector<std::uint16_t>& units,
                               const std::uint32_t offset) {
    return static_cast<std::int32_t>(U32(units, offset));
}

[[nodiscard]] std::uint64_t U64(const std::vector<std::uint16_t>& units,
                                const std::uint32_t offset) {
    return static_cast<std::uint64_t>(units[offset]) |
           (static_cast<std::uint64_t>(units[offset + 1]) << 16U) |
           (static_cast<std::uint64_t>(units[offset + 2]) << 32U) |
           (static_cast<std::uint64_t>(units[offset + 3]) << 48U);
}

[[nodiscard]] std::uint32_t PayloadWidth(
    const std::vector<std::uint16_t>& units, const std::uint32_t pc,
    const std::string_view where) {
    if (pc >= units.size()) Invalid(where, "payload pc is out of range");
    const auto ident = units[pc];
    std::uint64_t width{};
    if (ident == 0x0100U) {
        if (units.size() - pc < 2U) Invalid(where, "packed payload truncated");
        width = 4ULL + 2ULL * units[pc + 1];
    } else if (ident == 0x0200U) {
        if (units.size() - pc < 2U) Invalid(where, "sparse payload truncated");
        width = 2ULL + 4ULL * units[pc + 1];
    } else if (ident == 0x0300U) {
        if (units.size() - pc < 4U) Invalid(where, "array payload truncated");
        const auto count = U32(units, pc + 2);
        width = 4ULL +
                (static_cast<std::uint64_t>(units[pc + 1]) * count + 1U) /
                    2U;
    } else {
        Invalid(where, "unknown payload ident");
    }
    if (width > units.size() - pc ||
        width > std::numeric_limits<std::uint32_t>::max()) {
        Invalid(where, "payload exceeds method end");
    }
    return static_cast<std::uint32_t>(width);
}

void DecodeOperands(const std::vector<std::uint16_t>& units,
                    FastInstruction& out) {
    const auto unit = units[out.dex_pc];
    const auto next = [&](const std::uint32_t n) {
        return units[out.dex_pc + n];
    };
    using gen::DexInstructionFormat;
    const auto format = gen::kDexOpcodeTable[out.opcode].format;
    switch (format) {
        case DexInstructionFormat::k12x:
            out.a = (unit >> 8U) & 0xfU;
            out.b = (unit >> 12U) & 0xfU;
            break;
        case DexInstructionFormat::k22x:
            out.a = (unit >> 8U) & 0xffU;
            out.b = next(1);
            break;
        case DexInstructionFormat::k32x:
            out.a = next(1);
            out.b = next(2);
            break;
        case DexInstructionFormat::k11n:
            out.a = (unit >> 8U) & 0xfU;
            out.extra = static_cast<std::uint64_t>(static_cast<std::int64_t>(
                static_cast<std::int32_t>(
                    static_cast<std::uint32_t>((unit >> 12U) & 0xfU) << 28U) >>
                28U));
            break;
        case DexInstructionFormat::k11x:
            out.a = (unit >> 8U) & 0xffU;
            break;
        case DexInstructionFormat::k21s:
            out.a = (unit >> 8U) & 0xffU;
            out.extra = static_cast<std::uint64_t>(static_cast<std::int64_t>(
                static_cast<std::int16_t>(next(1))));
            break;
        case DexInstructionFormat::k21h:
            out.a = (unit >> 8U) & 0xffU;
            out.extra = next(1);
            break;
        case DexInstructionFormat::k31i:
        case DexInstructionFormat::k31c:
            out.a = (unit >> 8U) & 0xffU;
            out.extra = U32(units, out.dex_pc + 1);
            break;
        case DexInstructionFormat::k51l:
            out.a = (unit >> 8U) & 0xffU;
            out.extra = U64(units, out.dex_pc + 1);
            break;
        case DexInstructionFormat::k21c:
        case DexInstructionFormat::k21t:
            out.a = (unit >> 8U) & 0xffU;
            if (format == DexInstructionFormat::k21c) out.extra = next(1);
            break;
        case DexInstructionFormat::k23x:
            out.a = (unit >> 8U) & 0xffU;
            out.b = next(1) & 0xffU;
            out.c = (next(1) >> 8U) & 0xffU;
            break;
        case DexInstructionFormat::k22b:
            out.a = (unit >> 8U) & 0xffU;
            out.b = next(1) & 0xffU;
            out.extra = static_cast<std::uint64_t>(static_cast<std::int64_t>(
                static_cast<std::int8_t>(next(1) >> 8U)));
            break;
        case DexInstructionFormat::k22s:
        case DexInstructionFormat::k22c:
        case DexInstructionFormat::k22t:
            out.a = (unit >> 8U) & 0xfU;
            out.b = (unit >> 12U) & 0xfU;
            out.extra = format == DexInstructionFormat::k22s
                            ? static_cast<std::uint64_t>(
                                  static_cast<std::int64_t>(
                                      static_cast<std::int16_t>(next(1))))
                            : next(1);
            break;
        case DexInstructionFormat::k31t:
            out.a = (unit >> 8U) & 0xffU;
            break;
        case DexInstructionFormat::k35c:
            out.a = (unit >> 12U) & 0xfU;
            out.b = (unit >> 8U) & 0xfU;
            out.extra = next(1);
            out.c = next(2);
            break;
        case DexInstructionFormat::k3rc:
            out.a = (unit >> 8U) & 0xffU;
            out.b = next(2);
            out.extra = next(1);
            break;
        default:
            break;
    }
}

[[nodiscard]] std::int32_t BranchOffset(
    const FastInstruction& instruction,
    const std::vector<std::uint16_t>& units) {
    const auto unit = units[instruction.dex_pc];
    using gen::DexInstructionFormat;
    switch (gen::kDexOpcodeTable[instruction.opcode].format) {
        case DexInstructionFormat::k10t:
            return static_cast<std::int8_t>(unit >> 8U);
        case DexInstructionFormat::k20t:
        case DexInstructionFormat::k21t:
        case DexInstructionFormat::k22t:
            return static_cast<std::int16_t>(units[instruction.dex_pc + 1]);
        case DexInstructionFormat::k30t:
        case DexInstructionFormat::k31t:
            return S32(units, instruction.dex_pc + 1);
        default:
            return 0;
    }
}

[[nodiscard]] bool IsStraightOpcode(const std::uint8_t opcode) {
    return opcode <= 0x1cU ||
           (opcode >= 0x28U && opcode <= 0x2aU) ||
           (opcode >= 0x2dU && opcode <= 0x3dU) ||
           (opcode >= 0x7bU && opcode <= 0xe2U);
}

[[nodiscard]] bool IsObjectOpcode(const std::uint8_t opcode) {
    return (opcode >= 0x1dU && opcode <= 0x27U) ||
           opcode == 0x2bU || opcode == 0x2cU ||
           (opcode >= 0x44U && opcode <= 0x6dU);
}

[[nodiscard]] bool ObjectOpcodeNeedsResolution(const std::uint8_t opcode) {
    return (opcode >= 0x1fU && opcode <= 0x25U) ||
           (opcode >= 0x52U && opcode <= 0x6dU);
}

}  // namespace

std::uint32_t FastCode::IndexForDexPc(const std::uint32_t dex_pc) const {
    if (dex_pc >= dex_pc_to_index.size() ||
        dex_pc_to_index[dex_pc] == kInvalidFastIndex) {
        throw std::out_of_range("dex pc is not an executable instruction");
    }
    return dex_pc_to_index[dex_pc];
}

FastCode BuildFastCode(const loader::DexMethodCode& code,
                       const std::string_view where) {
    const auto& units = code.instructions;
    if (units.empty()) Invalid(where, "empty instruction stream");

    FastCode result;
    result.dex_pc_to_index.assign(units.size(), kInvalidFastIndex);
    std::unordered_map<std::uint32_t, std::uint32_t> payload_widths;
    std::uint32_t pc = 0;
    while (pc < units.size()) {
        const auto unit = units[pc];
        const auto opcode = static_cast<std::uint8_t>(unit & 0xffU);
        if (opcode == 0x00U && (unit >> 8U) != 0U) {
            const auto width = PayloadWidth(units, pc, where);
            payload_widths.emplace(pc, width);
            pc += width;
            continue;
        }
        const auto& info = gen::kDexOpcodeTable[opcode];
        if (!info.defined) Invalid(where, "rejected opcode");
        if (info.width == 0 || info.width > units.size() - pc) {
            Invalid(where, "instruction exceeds method end");
        }
        FastInstruction instruction;
        instruction.opcode = opcode;
        instruction.width = info.width;
        instruction.dex_pc = pc;
        if (IsStraightOpcode(opcode)) {
            instruction.handler = FastHandler::straight;
        } else if (IsObjectOpcode(opcode)) {
            instruction.handler = ObjectOpcodeNeedsResolution(opcode)
                                      ? FastHandler::object_checked
                                      : FastHandler::object_fast;
        } else if ((opcode >= 0x6eU && opcode <= 0x72U) ||
                   (opcode >= 0x74U && opcode <= 0x78U)) {
            instruction.handler = FastHandler::invoke_checked;
        }
        DecodeOperands(units, instruction);
        result.dex_pc_to_index[pc] =
            static_cast<std::uint32_t>(result.instructions.size());
        result.instructions.push_back(instruction);
        pc += info.width;
    }
    if (pc != units.size()) Invalid(where, "instruction stream misaligned");

    for (auto& instruction : result.instructions) {
        const auto& info = gen::kDexOpcodeTable[instruction.opcode];
        const auto format = info.format;
        const bool payload_reference =
            format == gen::DexInstructionFormat::k31t;
        const bool branch = (info.flags & gen::kFlagBranch) != 0;
        if (!branch && !payload_reference) continue;
        const auto offset = BranchOffset(instruction, units);
        const auto target64 = static_cast<std::int64_t>(instruction.dex_pc) +
                              static_cast<std::int64_t>(offset);
        if (target64 < 0 || target64 >= static_cast<std::int64_t>(units.size())) {
            Invalid(where, "branch target out of method");
        }
        const auto target = static_cast<std::uint32_t>(target64);
        if (!payload_reference) {
            if (result.dex_pc_to_index[target] == kInvalidFastIndex) {
                Invalid(where, "branch target is not an instruction boundary");
            }
            instruction.branch_target = result.dex_pc_to_index[target];
            continue;
        }
        const auto payload_width = payload_widths.find(target);
        if (payload_width == payload_widths.end()) {
            Invalid(where, "payload reference does not hit a payload");
        }
        FastPayload payload;
        payload.dex_pc = target;
        const auto ident = units[target];
        if (instruction.opcode == 0x2bU && ident == 0x0100U) {
            payload.kind = FastPayloadKind::packed_switch;
            const auto size = units[target + 1];
            const auto first_key = S32(units, target + 2);
            payload.tick_weight = size;
            payload.keys.reserve(size);
            payload.targets.reserve(size);
            for (std::uint32_t index = 0; index < size; ++index) {
                payload.keys.push_back(first_key + static_cast<std::int32_t>(index));
                const auto relative = S32(units, target + 4 + index * 2U);
                const auto branch_pc = static_cast<std::int64_t>(instruction.dex_pc) + relative;
                if (branch_pc < 0 || branch_pc >= static_cast<std::int64_t>(units.size()) ||
                    result.dex_pc_to_index[static_cast<std::size_t>(branch_pc)] == kInvalidFastIndex) {
                    Invalid(where, "packed-switch target is not executable");
                }
                payload.targets.push_back(result.dex_pc_to_index[static_cast<std::size_t>(branch_pc)]);
            }
        } else if (instruction.opcode == 0x2cU && ident == 0x0200U) {
            payload.kind = FastPayloadKind::sparse_switch;
            const auto size = units[target + 1];
            payload.tick_weight = size;
            payload.keys.reserve(size);
            payload.targets.reserve(size);
            for (std::uint32_t index = 0; index < size; ++index) {
                payload.keys.push_back(S32(units, target + 2 + index * 2U));
                const auto relative = S32(units, target + 2 + size * 2U + index * 2U);
                const auto branch_pc = static_cast<std::int64_t>(instruction.dex_pc) + relative;
                if (branch_pc < 0 || branch_pc >= static_cast<std::int64_t>(units.size()) ||
                    result.dex_pc_to_index[static_cast<std::size_t>(branch_pc)] == kInvalidFastIndex) {
                    Invalid(where, "sparse-switch target is not executable");
                }
                payload.targets.push_back(result.dex_pc_to_index[static_cast<std::size_t>(branch_pc)]);
            }
        } else if (instruction.opcode == 0x26U && ident == 0x0300U) {
            payload.kind = FastPayloadKind::array_data;
            payload.element_width = units[target + 1];
            payload.element_count = U32(units, target + 2);
            payload.tick_weight = payload.element_count;
            payload.data_units.assign(units.begin() + target + 4,
                                      units.begin() + target + payload_width->second);
        } else {
            Invalid(where, "payload ident mismatch");
        }
        instruction.payload = static_cast<std::uint32_t>(result.payloads.size());
        result.payloads.push_back(std::move(payload));
    }

    result.storage_bytes = sizeof(FastCode) +
                           result.instructions.capacity() * sizeof(FastInstruction) +
                           result.dex_pc_to_index.capacity() * sizeof(std::uint32_t);
    for (const auto& payload : result.payloads) {
        result.storage_bytes += sizeof(FastPayload) +
                                payload.keys.capacity() * sizeof(std::int32_t) +
                                payload.targets.capacity() * sizeof(std::uint32_t) +
                                payload.data_units.capacity() * sizeof(std::uint16_t);
    }
    return result;
}

}  // namespace ogplay::runtime::dexvm

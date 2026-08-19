// Main dispatch loop: moves, consts, control flow, objects, arrays, fields
// and invokes. Arithmetic/compare families live in interp_arith.cpp.
// Per-opcode semantics follow AOSP vm/mterp/c/OP_*.cpp at the pinned
// baseline (07 §2 mode B); conformance fixtures assert the edge cases.

#include "interpreter_internal.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

namespace ogplay::runtime::dexvm {
namespace {

[[nodiscard]] std::int32_t SignExtend4(const std::uint32_t value) {
    return static_cast<std::int32_t>(value << 28U) >> 28;
}

[[nodiscard]] JniPrimitiveKind ArrayKindFor(const std::string& element) {
    if (element == "Z") return JniPrimitiveKind::boolean;
    if (element == "B") return JniPrimitiveKind::byte;
    if (element == "C") return JniPrimitiveKind::character;
    if (element == "S") return JniPrimitiveKind::short_integer;
    if (element == "I") return JniPrimitiveKind::integer;
    if (element == "J") return JniPrimitiveKind::long_integer;
    if (element == "F") return JniPrimitiveKind::float_value;
    if (element == "D") return JniPrimitiveKind::double_value;
    throw DexVmError(DexVmErrorReason::invalid_operand,
                     "not a primitive array element: " + element);
}

}  // namespace

void Interpreter::Impl::Step(InterpreterExecutionState& execution) {
    auto& frames = execution.frames;
    auto& pending_exception = execution.pending_exception;
    auto& exit_result = execution.exit_result;
    auto& frame = frames.back();
    const auto& code = *frame.method->code;
    const auto& units = code.instructions;
    if (frame.pc >= units.size()) {
        FailCode("pc ran off the end of the method");
    }
    const auto unit = units[frame.pc];
    const auto opcode = static_cast<std::uint8_t>(unit & 0xffU);
    const auto& info = gen::kDexOpcodeTable[opcode];
    if (!info.defined) {
        if (ledger != nullptr) {
            ledger->RecordUnimplemented(
                "dexvm.opcode." + std::to_string(opcode), 0);
        }
        throw DexVmError(DexVmErrorReason::invalid_opcode,
                         "rejected opcode " + std::to_string(opcode));
    }
    Tick(execution);
    ++stats.executed_instructions;
    const auto width = info.width;
    const auto advance = [&] { frames.back().pc += width; };

    // Arithmetic / compare families first (separate translation unit).
    if (ExecuteArithmetic(frame, opcode, unit)) {
        if (!pending_exception.IsValid()) advance();
        return;
    }

    const auto vAA = static_cast<std::uint32_t>((unit >> 8U) & 0xffU);
    const auto vA = static_cast<std::uint32_t>((unit >> 8U) & 0xfU);
    const auto vB4 = static_cast<std::uint32_t>((unit >> 12U) & 0xfU);

    switch (opcode) {
        case 0x00:  // nop (payload idents never execute: guarded below)
            if ((unit >> 8U) != 0) {
                FailCode("execution reached payload data");
            }
            advance();
            return;

        // ---- moves -----------------------------------------------------
        case 0x01:  // move
            SetCat1(frame, vA, GetCat1(frame, vB4));
            advance();
            return;
        case 0x02:  // move/from16
            SetCat1(frame, vAA, GetCat1(frame, units[frame.pc + 1]));
            advance();
            return;
        case 0x03:  // move/16
            SetCat1(frame, units[frame.pc + 1],
                    GetCat1(frame, units[frame.pc + 2]));
            advance();
            return;
        case 0x04:  // move-wide
            SetWide(frame, vA, GetWide(frame, vB4));
            advance();
            return;
        case 0x05:  // move-wide/from16
            SetWide(frame, vAA, GetWide(frame, units[frame.pc + 1]));
            advance();
            return;
        case 0x06:  // move-wide/16
            SetWide(frame, units[frame.pc + 1],
                    GetWide(frame, units[frame.pc + 2]));
            advance();
            return;
        case 0x07:  // move-object
            SetRef(frame, vA, GetRef(frame, vB4));
            advance();
            return;
        case 0x08:  // move-object/from16
            SetRef(frame, vAA, GetRef(frame, units[frame.pc + 1]));
            advance();
            return;
        case 0x09:  // move-object/16
            SetRef(frame, units[frame.pc + 1],
                    GetRef(frame, units[frame.pc + 2]));
            advance();
            return;
        case 0x0a:  // move-result
        case 0x0c:  // move-result-object
            if (opcode == 0x0a) {
                if (frame.last_result.kind != VmValue::Kind::cat1) {
                    FailCode("move-result without cat1 result");
                }
                SetCat1(frame, vAA, frame.last_result.cat1);
            } else {
                if (frame.last_result.kind != VmValue::Kind::ref) {
                    FailCode("move-result-object without reference result");
                }
                SetRef(frame, vAA, frame.last_result.ref);
            }
            advance();
            return;
        case 0x0b:  // move-result-wide
            if (frame.last_result.kind != VmValue::Kind::wide) {
                FailCode("move-result-wide without wide result");
            }
            SetWide(frame, vAA, frame.last_result.wide);
            advance();
            return;
        case 0x0d:  // move-exception
            if (!frame.caught.IsValid()) {
                FailCode("move-exception without caught exception");
            }
            SetRef(frame, vAA, frame.caught);
            frame.caught = VmObjectRef{};
            advance();
            return;

        // ---- returns ---------------------------------------------------
        case 0x0e:  // return-void
        case 0x0f:  // return
        case 0x10:  // return-wide
        case 0x11: {  // return-object
            VmValue result;
            if (opcode == 0x0f) result = VmValue::Int(
                static_cast<std::int32_t>(GetCat1(frame, vAA)));
            if (opcode == 0x10) result = VmValue::Long(
                static_cast<std::int64_t>(GetWide(frame, vAA)));
            if (opcode == 0x11) result = VmValue::Ref(GetRef(frame, vAA));
            exit_result = result;
            frames.pop_back();
            if (!frames.empty()) {
                auto& caller = frames.back();
                caller.last_result = result;
                caller.pc += caller.pending_advance;
                caller.pending_advance = 0;
            }
            return;
        }

        // ---- consts ----------------------------------------------------
        case 0x12:  // const/4
            SetCat1(frame, vA, static_cast<std::uint32_t>(SignExtend4(vB4)));
            advance();
            return;
        case 0x13:  // const/16
            SetCat1(frame, vAA,
                    static_cast<std::uint32_t>(
                        static_cast<std::int16_t>(units[frame.pc + 1])));
            advance();
            return;
        case 0x14:  // const
            SetCat1(frame, vAA,
                    static_cast<std::uint32_t>(units[frame.pc + 1]) |
                        (static_cast<std::uint32_t>(units[frame.pc + 2])
                         << 16U));
            advance();
            return;
        case 0x15:  // const/high16
            SetCat1(frame, vAA,
                    static_cast<std::uint32_t>(units[frame.pc + 1]) << 16U);
            advance();
            return;
        case 0x16:  // const-wide/16
            SetWide(frame, vAA,
                    static_cast<std::uint64_t>(static_cast<std::int64_t>(
                        static_cast<std::int16_t>(units[frame.pc + 1]))));
            advance();
            return;
        case 0x17: {  // const-wide/32
            const auto value = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(units[frame.pc + 1]) |
                (static_cast<std::uint32_t>(units[frame.pc + 2]) << 16U));
            SetWide(frame, vAA,
                    static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(value)));
            advance();
            return;
        }
        case 0x18:  // const-wide
            SetWide(frame, vAA,
                    static_cast<std::uint64_t>(units[frame.pc + 1]) |
                        (static_cast<std::uint64_t>(units[frame.pc + 2])
                         << 16U) |
                        (static_cast<std::uint64_t>(units[frame.pc + 3])
                         << 32U) |
                        (static_cast<std::uint64_t>(units[frame.pc + 4])
                         << 48U));
            advance();
            return;
        case 0x19:  // const-wide/high16
            SetWide(frame, vAA,
                    static_cast<std::uint64_t>(units[frame.pc + 1]) << 48U);
            advance();
            return;
        case 0x1a:  // const-string
            PrepareSafeAllocation(
                JavaObjectModel::EstimateStringBytes(
                    linker->Image().strings[units[frame.pc + 1]].value.size()),
                "const-string");
            SetRef(frame, vAA, InternDexString(units[frame.pc + 1]));
            advance();
            return;
        case 0x1b:  // const-string/jumbo
        {
            const auto string_index =
                static_cast<std::uint32_t>(units[frame.pc + 1]) |
                (static_cast<std::uint32_t>(units[frame.pc + 2]) << 16U);
            PrepareSafeAllocation(JavaObjectModel::EstimateStringBytes(
                                      linker->Image().strings[string_index].value.size()),
                                  "const-string-jumbo");
            SetRef(frame, vAA, InternDexString(string_index));
            advance();
            return;
        }
        case 0x1c: {  // const-class (does not trigger <clinit>, 02 §5)
            const auto java_class =
                linker->ResolveTypeIndex(units[frame.pc + 1]);
            SetRef(frame, vAA, model->ClassObject(java_class));
            advance();
            return;
        }

        // ---- monitors (04 §4; AOSP vm/Sync.cpp lockMonitor) -------------
        case 0x1d: {  // monitor-enter
            const auto ref = GetRef(frame, vAA);
            if (!ref.IsValid()) {
                ThrowJava("Ljava/lang/NullPointerException;",
                          "monitor-enter on null");
                return;
            }
            // Parks (releasing the execution lock) while another thread
            // owns this object.
            monitors->Enter(ref, execution.token);
            advance();
            return;
        }
        case 0x1e: {  // monitor-exit
            const auto ref = GetRef(frame, vAA);
            if (!ref.IsValid()) {
                ThrowJava("Ljava/lang/NullPointerException;",
                          "monitor-exit on null");
                return;
            }
            monitors->Exit(ref, execution.token);
            advance();
            return;
        }

        // ---- type checks ------------------------------------------------
        case 0x1f: {  // check-cast
            const auto ref = GetRef(frame, vAA);
            if (ref.IsValid()) {
                const auto target =
                    linker->ResolveTypeIndex(units[frame.pc + 1]);
                const auto source = model->ObjectClass(ref);
                if (!source.IsValid() ||
                    !linker->IsAssignable(target, source)) {
                    ThrowJava("Ljava/lang/ClassCastException;",
                              (source.IsValid()
                                   ? linker->Class(source).descriptor
                                   : std::string("<external>")) +
                                  " cannot be cast to " +
                                  linker->Class(target).descriptor);
                    return;
                }
            }
            advance();
            return;
        }
        case 0x20: {  // instance-of (does not trigger <clinit>)
            const auto ref = GetRef(frame, vB4);
            bool result = false;
            if (ref.IsValid()) {
                const auto target =
                    linker->ResolveTypeIndex(units[frame.pc + 1]);
                const auto source = model->ObjectClass(ref);
                result = source.IsValid() &&
                         linker->IsAssignable(target, source);
            }
            SetCat1(frame, vA, result ? 1U : 0U);
            advance();
            return;
        }
        case 0x21: {  // array-length
            const auto ref = GetRef(frame, vB4);
            if (!ref.IsValid()) {
                ThrowJava("Ljava/lang/NullPointerException;",
                          "array-length on null");
                return;
            }
            SetCat1(frame, vA,
                    static_cast<std::uint32_t>(model->ArrayLength(ref)));
            advance();
            return;
        }

        // ---- allocation --------------------------------------------------
        case 0x22: {  // new-instance
            const auto java_class =
                linker->ResolveTypeIndex(units[frame.pc + 1]);
            PrepareSafeAllocation(JavaObjectModel::EstimateInstanceBytes(
                                      linker->Class(java_class).instance_slots),
                                  "new-instance");
            EnsureInitialized(execution, java_class);
            if (pending_exception.IsValid()) return;
            SetRef(frame, vAA, AllocateInstance(java_class));
            advance();
            return;
        }
        case 0x23: {  // new-array
            const auto length =
                static_cast<std::int32_t>(GetCat1(frame, vB4));
            if (length < 0) {
                ThrowJava("Ljava/lang/NegativeArraySizeException;",
                          std::to_string(length));
                return;
            }
            const auto array_class =
                linker->ResolveTypeIndex(units[frame.pc + 1]);
            const auto& linked = linker->Class(array_class);
            if (!linked.is_array) {
                FailCode("new-array with non-array type");
            }
            const auto& element = linked.array_element_descriptor;
            if (element.starts_with("L") || element.starts_with("[")) {
                const auto element_class = linker->ResolveDescriptor(element);
                PrepareSafeAllocation(
                    JavaObjectModel::EstimateObjectArrayBytes(length),
                    "new-array");
                SetRef(frame, vA,
                       model->NewObjectArray(array_class, element_class,
                                             length));
            } else {
                const auto kind = ArrayKindFor(element);
                PrepareSafeAllocation(
                    JavaObjectModel::EstimatePrimitiveArrayBytes(kind, length),
                    "new-array");
                SetRef(frame, vA,
                       model->NewPrimitiveArray(array_class, kind, length));
            }
            advance();
            return;
        }
        case 0x24:    // filled-new-array
        case 0x25: {  // filled-new-array/range
            const auto array_class =
                linker->ResolveTypeIndex(units[frame.pc + 1]);
            const auto& linked = linker->Class(array_class);
            if (!linked.is_array) FailCode("filled-new-array non-array type");
            const auto& element = linked.array_element_descriptor;
            std::vector<std::uint32_t> registers;
            if (opcode == 0x24) {
                const auto count = (unit >> 12U) & 0xfU;
                const auto regs_unit = units[frame.pc + 2];
                const std::uint32_t candidates[5] = {
                    (regs_unit >> 0U) & 0xfU, (regs_unit >> 4U) & 0xfU,
                    (regs_unit >> 8U) & 0xfU, (regs_unit >> 12U) & 0xfU,
                    (unit >> 8U) & 0xfU};
                for (std::uint32_t index = 0; index < count; ++index) {
                    registers.push_back(candidates[index]);
                }
            } else {
                const auto count = vAA;
                const auto first = units[frame.pc + 2];
                for (std::uint32_t index = 0; index < count; ++index) {
                    registers.push_back(first + index);
                }
            }
            if (element == "I") {
                PrepareSafeAllocation(
                    JavaObjectModel::EstimatePrimitiveArrayBytes(
                        JniPrimitiveKind::integer,
                        static_cast<JniSize>(registers.size())),
                    "filled-new-array");
                const auto array = model->NewPrimitiveArray(
                    array_class, JniPrimitiveKind::integer,
                    static_cast<JniSize>(registers.size()));
                for (std::size_t index = 0; index < registers.size();
                     ++index) {
                    model->SetPrimitiveElement(
                        array, static_cast<JniSize>(index),
                        GetCat1(frame, registers[index]));
                }
                frame.last_result = VmValue::Ref(array);
            } else if (element.starts_with("L") || element.starts_with("[")) {
                const auto element_class = linker->ResolveDescriptor(element);
                PrepareSafeAllocation(
                    JavaObjectModel::EstimateObjectArrayBytes(
                        static_cast<JniSize>(registers.size())),
                    "filled-new-array");
                const auto array = model->NewObjectArray(
                    array_class, element_class,
                    static_cast<JniSize>(registers.size()));
                for (std::size_t index = 0; index < registers.size();
                     ++index) {
                    model->SetObjectElement(array,
                                            static_cast<JniSize>(index),
                                            GetRef(frame, registers[index]));
                }
                frame.last_result = VmValue::Ref(array);
            } else {
                FailCode("filled-new-array element type is unsupported: " +
                         element);
            }
            advance();
            return;
        }
        case 0x26: {  // fill-array-data
            const auto ref = GetRef(frame, vAA);
            if (!ref.IsValid()) {
                ThrowJava("Ljava/lang/NullPointerException;",
                          "fill-array-data on null");
                return;
            }
            const auto offset = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(units[frame.pc + 1]) |
                (static_cast<std::uint32_t>(units[frame.pc + 2]) << 16U));
            const auto payload_pc =
                static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(frame.pc) + offset);
            const auto element_width = units[payload_pc + 1];
            const auto count =
                static_cast<std::uint32_t>(units[payload_pc + 2]) |
                (static_cast<std::uint32_t>(units[payload_pc + 3]) << 16U);
            const auto length = model->ArrayLength(ref);
            if (count > static_cast<std::uint32_t>(length)) {
                ThrowJava("Ljava/lang/ArrayIndexOutOfBoundsException;",
                          "fill-array-data size exceeds array length");
                return;
            }
            Tick(execution, count);
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(
                units.data() + payload_pc + 4);
            for (std::uint32_t index = 0; index < count; ++index) {
                std::uint64_t bits{};
                for (std::uint32_t byte = 0; byte < element_width; ++byte) {
                    bits |= static_cast<std::uint64_t>(
                                bytes[index * element_width + byte])
                            << (byte * 8U);
                }
                // Sub-word elements are stored sign-appropriately by kind.
                const auto kind = model->PrimitiveArrayKind(ref);
                if (kind == JniPrimitiveKind::byte) {
                    bits = static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(static_cast<std::int32_t>(
                            static_cast<std::int8_t>(bits))));
                } else if (kind == JniPrimitiveKind::short_integer) {
                    bits = static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(static_cast<std::int32_t>(
                            static_cast<std::int16_t>(bits))));
                }
                model->SetPrimitiveElement(ref,
                                           static_cast<JniSize>(index), bits);
            }
            advance();
            return;
        }

        // ---- throw / goto / switch ---------------------------------------
        case 0x27:  // throw
            SetPending(GetRef(frame, vAA));
            return;
        case 0x28: {  // goto
            const auto offset = static_cast<std::int8_t>((unit >> 8U) & 0xffU);
            frame.pc = static_cast<std::uint32_t>(
                static_cast<std::int64_t>(frame.pc) + offset);
            return;
        }
        case 0x29: {  // goto/16
            const auto offset =
                static_cast<std::int16_t>(units[frame.pc + 1]);
            frame.pc = static_cast<std::uint32_t>(
                static_cast<std::int64_t>(frame.pc) + offset);
            return;
        }
        case 0x2a: {  // goto/32
            const auto offset = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(units[frame.pc + 1]) |
                (static_cast<std::uint32_t>(units[frame.pc + 2]) << 16U));
            frame.pc = static_cast<std::uint32_t>(
                static_cast<std::int64_t>(frame.pc) + offset);
            return;
        }
        case 0x2b:    // packed-switch
        case 0x2c: {  // sparse-switch
            const auto key = static_cast<std::int32_t>(GetCat1(frame, vAA));
            const auto offset = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(units[frame.pc + 1]) |
                (static_cast<std::uint32_t>(units[frame.pc + 2]) << 16U));
            const auto payload_pc = static_cast<std::uint32_t>(
                static_cast<std::int64_t>(frame.pc) + offset);
            const auto size = units[payload_pc + 1];
            std::int64_t target_offset = width;  // default: fall through
            if (opcode == 0x2b) {
                const auto first_key = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(units[payload_pc + 2]) |
                    (static_cast<std::uint32_t>(units[payload_pc + 3])
                     << 16U));
                const auto index =
                    static_cast<std::int64_t>(key) - first_key;
                if (index >= 0 && index < size) {
                    const auto entry = payload_pc + 4 +
                                       static_cast<std::uint32_t>(index) * 2U;
                    target_offset = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(units[entry]) |
                        (static_cast<std::uint32_t>(units[entry + 1])
                         << 16U));
                }
            } else {
                Tick(execution, size);
                for (std::uint32_t index = 0; index < size; ++index) {
                    const auto key_entry = payload_pc + 2 + index * 2U;
                    const auto candidate = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(units[key_entry]) |
                        (static_cast<std::uint32_t>(units[key_entry + 1])
                         << 16U));
                    if (candidate == key) {
                        const auto target_entry =
                            payload_pc + 2 + size * 2U + index * 2U;
                        target_offset = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(units[target_entry]) |
                            (static_cast<std::uint32_t>(
                                 units[target_entry + 1])
                             << 16U));
                        break;
                    }
                }
            }
            frame.pc = static_cast<std::uint32_t>(
                static_cast<std::int64_t>(frame.pc) + target_offset);
            return;
        }

        // ---- conditional branches -----------------------------------------
        case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37: {
            // if-eq/ne/lt/ge/gt/le vA, vB, +CCCC
            // References may be compared with eq/ne (Dalvik allows both
            // cat1-zero and ref operands; tags are checked pairwise).
            const auto& lhs_slot = RegAt(frame, vA);
            const auto& rhs_slot = RegAt(frame, vB4);
            std::int32_t lhs{};
            std::int32_t rhs{};
            if (lhs_slot.tag == SlotTag::ref || rhs_slot.tag == SlotTag::ref) {
                if (opcode != 0x32 && opcode != 0x33) {
                    FailCode("reference compared with ordered if");
                }
                lhs = static_cast<std::int32_t>(GetRef(frame, vA).Value());
                rhs = static_cast<std::int32_t>(GetRef(frame, vB4).Value());
            } else {
                lhs = static_cast<std::int32_t>(GetCat1(frame, vA));
                rhs = static_cast<std::int32_t>(GetCat1(frame, vB4));
            }
            bool taken = false;
            switch (opcode) {
                case 0x32: taken = lhs == rhs; break;
                case 0x33: taken = lhs != rhs; break;
                case 0x34: taken = lhs < rhs; break;
                case 0x35: taken = lhs >= rhs; break;
                case 0x36: taken = lhs > rhs; break;
                case 0x37: taken = lhs <= rhs; break;
                default: break;
            }
            if (taken) {
                frame.pc = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(frame.pc) +
                    static_cast<std::int16_t>(units[frame.pc + 1]));
            } else {
                advance();
            }
            return;
        }
        case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: {
            // if-eqz/nez/ltz/gez/gtz/lez vAA, +BBBB
            const auto& slot = RegAt(frame, vAA);
            std::int32_t value{};
            if (slot.tag == SlotTag::ref) {
                if (opcode != 0x38 && opcode != 0x39) {
                    FailCode("reference compared with ordered ifz");
                }
                value = slot.bits == 0 ? 0 : 1;
            } else {
                value = static_cast<std::int32_t>(GetCat1(frame, vAA));
            }
            bool taken = false;
            switch (opcode) {
                case 0x38: taken = value == 0; break;
                case 0x39: taken = value != 0; break;
                case 0x3a: taken = value < 0; break;
                case 0x3b: taken = value >= 0; break;
                case 0x3c: taken = value > 0; break;
                case 0x3d: taken = value <= 0; break;
                default: break;
            }
            if (taken) {
                frame.pc = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(frame.pc) +
                    static_cast<std::int16_t>(units[frame.pc + 1]));
            } else {
                advance();
            }
            return;
        }

        default:
            break;
    }

    // Remaining families are in the second half of the dispatcher.
    StepObjectOrInvoke(execution, frame, opcode, unit);
}

}  // namespace ogplay::runtime::dexvm

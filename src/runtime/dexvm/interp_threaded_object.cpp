// Direct FastCode handlers for monitor/type/allocation/array/switch/field
// families. Constant-pool indices resolve once under VmExecutionLock and
// flip the derived instruction from checked to fast without touching DEX.

#include <algorithm>

#include "interpreter_internal.h"

namespace ogplay::runtime::dexvm {
namespace {

constexpr std::uint32_t kObjectElementFlag = 0x80000000U;

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

void Interpreter::Impl::StepObject(
    InterpreterExecutionState& execution, const FastCode& code,
    const FastInstruction& instruction) {
    auto& frames = execution.frames;
    auto& pending_exception = execution.pending_exception;
    auto& frame = frames.back();
    const auto opcode = instruction.opcode;
    Tick(execution);
    ++stats.executed_instructions;
    if (!trace_ring.empty()) {
        RecordTrace(DexVmTraceKind::instruction, execution, frame.method,
                    instruction.dex_pc, opcode);
    }

    if (instruction.handler == FastHandler::object_checked) {
        if (opcode >= 0x1fU && opcode <= 0x25U) {
            const auto java_class = linker->ResolveTypeIndex(
                static_cast<std::uint32_t>(instruction.extra));
            instruction.resolved_id = java_class.Value();
            if (opcode == 0x23U || opcode == 0x24U || opcode == 0x25U) {
                const auto& linked = linker->Class(java_class);
                if (!linked.is_array) {
                    FailCode("array opcode with non-array type");
                }
                const auto& element = linked.array_element_descriptor;
                if (element.starts_with("L") || element.starts_with("[")) {
                    instruction.resolved_aux =
                        kObjectElementFlag |
                        linker->ResolveDescriptor(element).Value();
                } else {
                    instruction.resolved_aux =
                        static_cast<std::uint32_t>(ArrayKindFor(element));
                }
            }
        } else if (opcode >= 0x52U && opcode <= 0x6dU) {
            const bool is_static = opcode >= 0x60U;
            instruction.resolved_id =
                linker->ResolveFieldIndex(
                           static_cast<std::uint32_t>(instruction.extra),
                           is_static)
                    .field.Value();
        }
        instruction.handler = FastHandler::object_fast;
    }

    const auto advance = [&] { frame.pc += instruction.width; };
    const auto java_class = [&] {
        return DexClassId(instruction.resolved_id);
    };

    switch (opcode) {
        case 0x1d:
        case 0x1e: {
            const auto ref = GetFastRef(frame, instruction.a);
            if (!ref.IsValid()) {
                ThrowJava("Ljava/lang/NullPointerException;",
                          opcode == 0x1d ? "monitor-enter on null"
                                         : "monitor-exit on null");
                return;
            }
            if (opcode == 0x1d) {
                monitors->Enter(ref, execution.token);
                RecordTrace(DexVmTraceKind::monitor_enter, execution,
                            frame.method, frame.pc, opcode, ref.Value());
            } else {
                monitors->Exit(ref, execution.token);
                RecordTrace(DexVmTraceKind::monitor_exit, execution,
                            frame.method, frame.pc, opcode, ref.Value());
            }
            advance();
            return;
        }
        case 0x1f: {
            const auto ref = GetFastRef(frame, instruction.a);
            if (ref.IsValid()) {
                const auto source = model->ObjectClass(ref);
                if (!source.IsValid() ||
                    !linker->IsAssignable(java_class(), source)) {
                    ThrowJava(
                        "Ljava/lang/ClassCastException;",
                        (source.IsValid()
                             ? linker->Class(source).descriptor
                             : std::string("<external>")) +
                            " cannot be cast to " +
                            linker->Class(java_class()).descriptor);
                    return;
                }
            }
            advance();
            return;
        }
        case 0x20: {
            const auto ref = GetFastRef(frame, instruction.b);
            bool result = false;
            if (ref.IsValid()) {
                const auto source = model->ObjectClass(ref);
                result = source.IsValid() &&
                         linker->IsAssignable(java_class(), source);
            }
            SetFastCat1(frame, instruction.a, result ? 1U : 0U);
            advance();
            return;
        }
        case 0x21: {
            const auto ref = GetFastRef(frame, instruction.b);
            if (!ref.IsValid()) {
                ThrowJava("Ljava/lang/NullPointerException;",
                          "array-length on null");
                return;
            }
            SetFastCat1(frame, instruction.a,
                        static_cast<std::uint32_t>(model->ArrayLength(ref)));
            advance();
            return;
        }
        case 0x22: {
            const auto type = java_class();
            PrepareSafeAllocation(JavaObjectModel::EstimateInstanceBytes(
                                      linker->Class(type).instance_slots),
                                  "new-instance");
            EnsureInitialized(execution, type);
            if (pending_exception.IsValid()) return;
            SetFastRef(frame, instruction.a, AllocateInstance(type));
            advance();
            return;
        }
        case 0x23: {
            const auto length = static_cast<std::int32_t>(
                GetFastCat1(frame, instruction.b));
            if (length < 0) {
                ThrowJava("Ljava/lang/NegativeArraySizeException;",
                          std::to_string(length));
                return;
            }
            const auto type = java_class();
            if ((instruction.resolved_aux & kObjectElementFlag) != 0U) {
                PrepareSafeAllocation(
                    JavaObjectModel::EstimateObjectArrayBytes(length),
                    "new-array");
                SetFastRef(frame, instruction.a,
                           model->NewObjectArray(
                               type,
                               DexClassId(instruction.resolved_aux &
                                          ~kObjectElementFlag),
                               length));
            } else {
                const auto kind = static_cast<JniPrimitiveKind>(
                    instruction.resolved_aux);
                PrepareSafeAllocation(
                    JavaObjectModel::EstimatePrimitiveArrayBytes(kind,
                                                                  length),
                    "new-array");
                SetFastRef(frame, instruction.a,
                           model->NewPrimitiveArray(type, kind, length));
            }
            advance();
            return;
        }
        case 0x24:
        case 0x25: {
            std::vector<std::uint32_t> registers;
            if (opcode == 0x24) {
                const std::uint32_t candidates[5] = {
                    (instruction.c >> 0U) & 0xfU,
                    (instruction.c >> 4U) & 0xfU,
                    (instruction.c >> 8U) & 0xfU,
                    (instruction.c >> 12U) & 0xfU,
                    instruction.b};
                for (std::uint32_t index = 0; index < instruction.a;
                     ++index) {
                    registers.push_back(candidates[index]);
                }
            } else {
                for (std::uint32_t index = 0; index < instruction.a;
                     ++index) {
                    registers.push_back(instruction.b + index);
                }
            }
            const auto type = java_class();
            if ((instruction.resolved_aux & kObjectElementFlag) != 0U) {
                PrepareSafeAllocation(
                    JavaObjectModel::EstimateObjectArrayBytes(
                        static_cast<JniSize>(registers.size())),
                    "filled-new-array");
                const auto array = model->NewObjectArray(
                    type,
                    DexClassId(instruction.resolved_aux & ~kObjectElementFlag),
                    static_cast<JniSize>(registers.size()));
                for (std::size_t index = 0; index < registers.size(); ++index) {
                    model->SetObjectElement(
                        array, static_cast<JniSize>(index),
                        GetFastRef(frame, registers[index]));
                }
                frame.last_result = VmValue::Ref(array);
            } else {
                const auto kind = static_cast<JniPrimitiveKind>(
                    instruction.resolved_aux);
                if (kind != JniPrimitiveKind::integer) {
                    FailCode("filled-new-array element type is unsupported");
                }
                PrepareSafeAllocation(
                    JavaObjectModel::EstimatePrimitiveArrayBytes(
                        kind, static_cast<JniSize>(registers.size())),
                    "filled-new-array");
                const auto array = model->NewPrimitiveArray(
                    type, kind, static_cast<JniSize>(registers.size()));
                for (std::size_t index = 0; index < registers.size(); ++index) {
                    model->SetPrimitiveElement(
                        array, static_cast<JniSize>(index),
                        GetFastCat1(frame, registers[index]));
                }
                frame.last_result = VmValue::Ref(array);
            }
            advance();
            return;
        }
        case 0x26: {
            const auto ref = GetFastRef(frame, instruction.a);
            if (!ref.IsValid()) {
                ThrowJava("Ljava/lang/NullPointerException;",
                          "fill-array-data on null");
                return;
            }
            const auto& payload = code.payloads[instruction.payload];
            if (payload.element_count >
                static_cast<std::uint32_t>(model->ArrayLength(ref))) {
                ThrowJava("Ljava/lang/ArrayIndexOutOfBoundsException;",
                          "fill-array-data size exceeds array length");
                return;
            }
            Tick(execution, payload.tick_weight);
            const auto kind = model->PrimitiveArrayKind(ref);
            for (std::uint32_t index = 0; index < payload.element_count;
                 ++index) {
                std::uint64_t bits{};
                for (std::uint32_t byte = 0; byte < payload.element_width;
                     ++byte) {
                    const auto byte_index = index * payload.element_width + byte;
                    const auto unit = payload.data_units[byte_index / 2U];
                    const auto value = static_cast<std::uint8_t>(
                        unit >> ((byte_index & 1U) * 8U));
                    bits |= static_cast<std::uint64_t>(value) << (byte * 8U);
                }
                if (kind == JniPrimitiveKind::byte) {
                    bits = static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(
                            static_cast<std::int8_t>(bits)));
                } else if (kind == JniPrimitiveKind::short_integer) {
                    bits = static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(
                            static_cast<std::int16_t>(bits)));
                }
                model->SetPrimitiveElement(ref, static_cast<JniSize>(index),
                                           bits);
            }
            advance();
            return;
        }
        case 0x27:
            SetPending(GetFastRef(frame, instruction.a));
            return;
        case 0x2b:
        case 0x2c: {
            const auto key = static_cast<std::int32_t>(
                GetFastCat1(frame, instruction.a));
            const auto& payload = code.payloads[instruction.payload];
            if (opcode == 0x2c) Tick(execution, payload.tick_weight);
            const auto found = std::find(payload.keys.begin(),
                                         payload.keys.end(), key);
            frame.pc = instruction.dex_pc + instruction.width;
            if (found != payload.keys.end()) {
                const auto index = static_cast<std::size_t>(
                    found - payload.keys.begin());
                frame.pc = code.instructions[payload.targets[index]].dex_pc;
            }
            return;
        }
        default:
            break;
    }

    if (opcode >= 0x44U && opcode <= 0x51U) {
        const auto array = GetFastRef(frame, instruction.b);
        if (!array.IsValid()) {
            ThrowJava("Ljava/lang/NullPointerException;",
                      "array access on null");
            return;
        }
        const auto index = static_cast<std::int32_t>(
            GetFastCat1(frame, instruction.c));
        const auto length = model->ArrayLength(array);
        if (index < 0 || index >= length) {
            ThrowJava("Ljava/lang/ArrayIndexOutOfBoundsException;",
                      "index " + std::to_string(index) + " length " +
                          std::to_string(length));
            return;
        }
        const bool is_object = opcode == 0x46U || opcode == 0x4dU;
        const bool is_wide = opcode == 0x45U || opcode == 0x4cU;
        const bool is_get = opcode <= 0x4aU;
        if (is_object) {
            if (model->Kind(array) != VmObjectKind::object_array) {
                FailCode("aget/aput-object on non-object array");
            }
            if (is_get) {
                SetFastRef(frame, instruction.a,
                           model->GetObjectElement(array, index));
            } else {
                const auto value = GetFastRef(frame, instruction.a);
                if (value.IsValid()) {
                    const auto element_class =
                        model->ObjectArrayElementClass(array);
                    const auto value_class = model->ObjectClass(value);
                    if (value_class.IsValid() &&
                        !linker->IsAssignable(element_class, value_class)) {
                        ThrowJava("Ljava/lang/ArrayStoreException;",
                                  linker->Class(value_class).descriptor);
                        return;
                    }
                }
                model->SetObjectElement(array, index, value);
            }
            advance();
            return;
        }
        if (model->Kind(array) != VmObjectKind::primitive_array &&
            model->Kind(array) != VmObjectKind::external) {
            FailCode("primitive array op on non-primitive array");
        }
        const auto kind = model->PrimitiveArrayKind(array);
        const auto require = [&](const bool ok) {
            if (!ok) FailCode("array element kind does not match opcode");
        };
        switch (opcode) {
            case 0x44: case 0x4b:
                require(kind == JniPrimitiveKind::integer ||
                        kind == JniPrimitiveKind::float_value);
                break;
            case 0x45: case 0x4c:
                require(kind == JniPrimitiveKind::long_integer ||
                        kind == JniPrimitiveKind::double_value);
                break;
            case 0x47: case 0x4e:
                require(kind == JniPrimitiveKind::boolean);
                break;
            case 0x48: case 0x4f:
                require(kind == JniPrimitiveKind::byte);
                break;
            case 0x49: case 0x50:
                require(kind == JniPrimitiveKind::character);
                break;
            case 0x4a: case 0x51:
                require(kind == JniPrimitiveKind::short_integer);
                break;
            default:
                break;
        }
        if (is_get) {
            const auto bits = model->GetPrimitiveElement(array, index);
            if (is_wide) {
                SetFastWide(frame, instruction.a, bits);
            } else {
                std::uint32_t narrowed = static_cast<std::uint32_t>(bits);
                if (kind == JniPrimitiveKind::byte) {
                    narrowed = static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(
                            static_cast<std::int8_t>(narrowed)));
                } else if (kind == JniPrimitiveKind::short_integer) {
                    narrowed = static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(
                            static_cast<std::int16_t>(narrowed)));
                }
                SetFastCat1(frame, instruction.a, narrowed);
            }
        } else {
            model->SetPrimitiveElement(
                array, index,
                is_wide ? GetFastWide(frame, instruction.a)
                        : static_cast<std::uint64_t>(
                              GetFastCat1(frame, instruction.a)));
        }
        advance();
        return;
    }

    if (opcode >= 0x52U && opcode <= 0x5fU) {
        const auto& field = linker->Field(VmFieldId(instruction.resolved_id));
        const auto receiver = GetFastRef(frame, instruction.b);
        if (!receiver.IsValid()) {
            ThrowJava("Ljava/lang/NullPointerException;",
                      "field access on null: " + field.name);
            return;
        }
        if (model->Kind(receiver) != VmObjectKind::vm_instance) {
            FailCode("field access on non-VM instance: " + field.name);
        }
        auto slots = model->InstanceSlots(receiver);
        if (field.slot + (field.is_wide ? 2U : 1U) > slots.size()) {
            FailCode("field slot out of range: " + field.name);
        }
        const bool is_get = opcode <= 0x58U;
        if (is_get) {
            if (field.is_wide) {
                SetFastWide(
                    frame, instruction.a,
                    static_cast<std::uint64_t>(slots[field.slot].bits) |
                        (static_cast<std::uint64_t>(
                             slots[field.slot + 1].bits)
                         << 32U));
            } else if (field.is_ref) {
                SetFastRef(frame, instruction.a,
                           VmObjectRef(slots[field.slot].bits));
            } else {
                SetFastCat1(frame, instruction.a, slots[field.slot].bits);
            }
        } else if (field.is_wide) {
            const auto bits = GetFastWide(frame, instruction.a);
            slots[field.slot] = {static_cast<std::uint32_t>(bits),
                                 SlotTag::wide_lo};
            slots[field.slot + 1] = {
                static_cast<std::uint32_t>(bits >> 32U), SlotTag::wide_hi};
        } else if (field.is_ref) {
            slots[field.slot] = {
                GetFastRef(frame, instruction.a).Value(), SlotTag::ref};
        } else {
            slots[field.slot] = {
                GetFastCat1(frame, instruction.a), SlotTag::cat1};
        }
        advance();
        return;
    }

    if (opcode >= 0x60U && opcode <= 0x6dU) {
        const auto& field = linker->Field(VmFieldId(instruction.resolved_id));
        EnsureInitialized(execution, field.owner);
        if (pending_exception.IsValid()) return;
        auto& storage = linker->MutableClass(field.owner).static_storage;
        if (field.slot + (field.is_wide ? 2U : 1U) > storage.size()) {
            FailCode("static slot out of range: " + field.name);
        }
        const bool is_get = opcode <= 0x66U;
        if (is_get) {
            if (field.is_wide) {
                SetFastWide(
                    frame, instruction.a,
                    static_cast<std::uint64_t>(storage[field.slot]) |
                        (static_cast<std::uint64_t>(storage[field.slot + 1])
                         << 32U));
            } else if (field.is_ref) {
                SetFastRef(frame, instruction.a,
                           VmObjectRef(storage[field.slot]));
            } else {
                SetFastCat1(frame, instruction.a, storage[field.slot]);
            }
        } else if (field.is_wide) {
            const auto bits = GetFastWide(frame, instruction.a);
            storage[field.slot] = static_cast<std::uint32_t>(bits);
            storage[field.slot + 1] =
                static_cast<std::uint32_t>(bits >> 32U);
        } else if (field.is_ref) {
            storage[field.slot] = GetFastRef(frame, instruction.a).Value();
        } else {
            storage[field.slot] = GetFastCat1(frame, instruction.a);
        }
        advance();
        return;
    }

    FailCode("non-object opcode reached object handler");
}

}  // namespace ogplay::runtime::dexvm

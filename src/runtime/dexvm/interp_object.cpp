// Array element, instance field, static field and invoke families.
// Semantics follow AOSP vm/mterp/c/OP_AGET*.cpp, OP_IGET*.cpp, OP_SGET*.cpp
// and OP_INVOKE_*.cpp at the pinned baseline (07 §2 mode B).

#include "interpreter_internal.h"

namespace ogplay::runtime::dexvm {

void Interpreter::Impl::StepObjectOrInvoke(Frame& frame,
                                           const std::uint8_t opcode,
                                           const std::uint16_t unit) {
    const auto& units = frame.method->code->instructions;
    const auto& info = gen::kDexOpcodeTable[opcode];
    const auto width = info.width;
    const auto advance = [&] { frames.back().pc += width; };
    const auto vAA = static_cast<std::uint32_t>((unit >> 8U) & 0xffU);
    const auto vA = static_cast<std::uint32_t>((unit >> 8U) & 0xfU);
    const auto vB4 = static_cast<std::uint32_t>((unit >> 12U) & 0xfU);

    // ---- aget/aput family (0x44..0x51) ----------------------------------
    if (opcode >= 0x44 && opcode <= 0x51) {
        const auto second = units[frame.pc + 1];
        const auto array_reg = static_cast<std::uint32_t>(second & 0xffU);
        const auto index_reg =
            static_cast<std::uint32_t>((second >> 8U) & 0xffU);
        const auto array = GetRef(frame, array_reg);
        if (!array.IsValid()) {
            ThrowJava("Ljava/lang/NullPointerException;",
                      "array access on null");
            return;
        }
        const auto index = static_cast<std::int32_t>(
            GetCat1(frame, index_reg));
        const auto length = model->ArrayLength(array);
        if (index < 0 || index >= length) {
            ThrowJava("Ljava/lang/ArrayIndexOutOfBoundsException;",
                      "index " + std::to_string(index) + " length " +
                          std::to_string(length));
            return;
        }
        const bool is_object = opcode == 0x46 || opcode == 0x4d;
        const bool is_wide = opcode == 0x45 || opcode == 0x4c;
        const bool is_get = opcode <= 0x4a;
        if (is_object) {
            if (model->Kind(array) != VmObjectKind::object_array) {
                FailCode("aget/aput-object on non-object array");
            }
            if (is_get) {
                SetRef(frame, vAA, model->GetObjectElement(array, index));
            } else {
                const auto value = GetRef(frame, vAA);
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
        // Element category must match the opcode (02 §7 tag rules).
        const auto require = [&](const bool ok) {
            if (!ok) FailCode("array element kind does not match opcode");
        };
        switch (opcode) {
            case 0x44: case 0x4b:  // aget/aput (int|float)
                require(kind == JniPrimitiveKind::integer ||
                        kind == JniPrimitiveKind::float_value);
                break;
            case 0x45: case 0x4c:  // wide
                require(kind == JniPrimitiveKind::long_integer ||
                        kind == JniPrimitiveKind::double_value);
                break;
            case 0x47: case 0x4e:  // boolean
                require(kind == JniPrimitiveKind::boolean);
                break;
            case 0x48: case 0x4f:  // byte
                require(kind == JniPrimitiveKind::byte);
                break;
            case 0x49: case 0x50:  // char
                require(kind == JniPrimitiveKind::character);
                break;
            case 0x4a: case 0x51:  // short
                require(kind == JniPrimitiveKind::short_integer);
                break;
            default:
                break;
        }
        if (is_get) {
            const auto bits = model->GetPrimitiveElement(array, index);
            if (is_wide) {
                SetWide(frame, vAA, bits);
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
                SetCat1(frame, vAA, narrowed);
            }
        } else {
            const auto bits = is_wide
                                  ? GetWide(frame, vAA)
                                  : static_cast<std::uint64_t>(
                                        GetCat1(frame, vAA));
            model->SetPrimitiveElement(array, index, bits);
        }
        advance();
        return;
    }

    // ---- iget/iput family (0x52..0x5f) -----------------------------------
    if (opcode >= 0x52 && opcode <= 0x5f) {
        const auto resolved =
            linker->ResolveFieldIndex(units[frame.pc + 1], false);
        const auto& field = linker->Field(resolved.field);
        const auto receiver = GetRef(frame, vB4);
        if (!receiver.IsValid()) {
            ThrowJava("Ljava/lang/NullPointerException;",
                      "field access on null: " + field.name);
            return;
        }
        if (model->Kind(receiver) != VmObjectKind::vm_instance) {
            // Intrinsic fields go through handlers (03 §2); none of the
            // stage-1 core classes expose raw fields.
            FailCode("field access on non-VM instance: " + field.name);
        }
        auto slots = model->InstanceSlots(receiver);
        if (field.slot + (field.is_wide ? 2U : 1U) > slots.size()) {
            FailCode("field slot out of range: " + field.name);
        }
        const bool is_get = opcode <= 0x58;
        if (is_get) {
            if (field.is_wide) {
                const auto bits =
                    static_cast<std::uint64_t>(slots[field.slot].bits) |
                    (static_cast<std::uint64_t>(slots[field.slot + 1].bits)
                     << 32U);
                SetWide(frame, vA, bits);
            } else if (field.is_ref) {
                SetRef(frame, vA, VmObjectRef(slots[field.slot].bits));
            } else {
                SetCat1(frame, vA, slots[field.slot].bits);
            }
        } else {
            if (field.is_wide) {
                const auto bits = GetWide(frame, vA);
                slots[field.slot] = {static_cast<std::uint32_t>(bits),
                                     SlotTag::wide_lo};
                slots[field.slot + 1] = {
                    static_cast<std::uint32_t>(bits >> 32U),
                    SlotTag::wide_hi};
            } else if (field.is_ref) {
                slots[field.slot] = {GetRef(frame, vA).Value(), SlotTag::ref};
            } else {
                slots[field.slot] = {GetCat1(frame, vA), SlotTag::cat1};
            }
        }
        advance();
        return;
    }

    // ---- sget/sput family (0x60..0x6d) ------------------------------------
    if (opcode >= 0x60 && opcode <= 0x6d) {
        const auto resolved =
            linker->ResolveFieldIndex(units[frame.pc + 1], true);
        const auto& field = linker->Field(resolved.field);
        EnsureInitialized(field.owner);
        if (pending_exception.IsValid()) return;
        auto& storage = linker->MutableClass(field.owner).static_storage;
        if (field.slot + (field.is_wide ? 2U : 1U) > storage.size()) {
            FailCode("static slot out of range: " + field.name);
        }
        const bool is_get = opcode <= 0x66;
        if (is_get) {
            if (field.is_wide) {
                SetWide(frame, vAA,
                        static_cast<std::uint64_t>(storage[field.slot]) |
                            (static_cast<std::uint64_t>(
                                 storage[field.slot + 1])
                             << 32U));
            } else if (field.is_ref) {
                SetRef(frame, vAA, VmObjectRef(storage[field.slot]));
            } else {
                SetCat1(frame, vAA, storage[field.slot]);
            }
        } else {
            if (field.is_wide) {
                const auto bits = GetWide(frame, vAA);
                storage[field.slot] = static_cast<std::uint32_t>(bits);
                storage[field.slot + 1] =
                    static_cast<std::uint32_t>(bits >> 32U);
            } else if (field.is_ref) {
                storage[field.slot] = GetRef(frame, vAA).Value();
            } else {
                storage[field.slot] = GetCat1(frame, vAA);
            }
        }
        advance();
        return;
    }

    // ---- invoke family (0x6e..0x78) ----------------------------------------
    if ((opcode >= 0x6e && opcode <= 0x72) ||
        (opcode >= 0x74 && opcode <= 0x78)) {
        const bool is_range = opcode >= 0x74;
        const auto base = static_cast<std::uint8_t>(
            is_range ? opcode - 0x74 + 0x6e : opcode);
        // base: 0x6e virtual, 0x6f super, 0x70 direct, 0x71 static,
        //       0x72 interface
        const bool direct_or_static = base == 0x70 || base == 0x71;
        const auto resolved = linker->ResolveMethodIndex(
            units[frame.pc + 1], direct_or_static);
        const auto& named = linker->Method(resolved.method);

        std::vector<std::uint32_t> registers;
        if (is_range) {
            const auto first = units[frame.pc + 2];
            for (std::uint32_t index = 0; index < vAA; ++index) {
                registers.push_back(first + index);
            }
        } else {
            const auto count = (unit >> 12U) & 0xfU;
            const auto regs_unit = units[frame.pc + 2];
            const std::uint32_t candidates[5] = {
                (regs_unit >> 0U) & 0xfU, (regs_unit >> 4U) & 0xfU,
                (regs_unit >> 8U) & 0xfU, (regs_unit >> 12U) & 0xfU,
                (unit >> 8U) & 0xfU};
            for (std::uint32_t index = 0; index < count; ++index) {
                registers.push_back(candidates[index]);
            }
        }
        if (registers.size() != named.ins_words) {
            FailCode("invoke argument words do not match descriptor of " +
                     named.name + " (" + std::to_string(registers.size()) +
                     " vs " + std::to_string(named.ins_words) + ")");
        }

        // Marshal arguments by descriptor with tag checks (02 §7).
        std::vector<VmValue> arguments;
        std::size_t cursor = 0;
        VmObjectRef receiver;
        if (!named.is_static) {
            receiver = GetRef(frame, registers[cursor]);
            if (!receiver.IsValid()) {
                // Name the target and the call site: a null receiver here is
                // almost always an earlier lookup (findViewById, getSystemService)
                // that answered null, and the caller is the only clue to which.
                ThrowJava(
                    "Ljava/lang/NullPointerException;",
                    "invoke on null receiver: " +
                        linker->Class(named.owner).descriptor + "." +
                        named.name + named.descriptor + " from v" +
                        std::to_string(registers[cursor]) + " called by " +
                        linker->Class(frame.method->owner).descriptor + "." +
                        frame.method->name + " pc " +
                        std::to_string(frame.pc));
                return;
            }
            arguments.push_back(VmValue::Ref(receiver));
            ++cursor;
        }
        const auto parts_begin = named.descriptor.find('(') + 1;
        std::size_t descriptor_index = parts_begin;
        while (named.descriptor[descriptor_index] != ')') {
            const char shorty = named.descriptor[descriptor_index];
            if (shorty == 'J' || shorty == 'D') {
                arguments.push_back(VmValue::Long(static_cast<std::int64_t>(
                    GetWide(frame, registers[cursor]))));
                if (shorty == 'D') arguments.back().kind = VmValue::Kind::wide;
                cursor += 2;
                ++descriptor_index;
            } else if (shorty == 'L' || shorty == '[') {
                arguments.push_back(
                    VmValue::Ref(GetRef(frame, registers[cursor])));
                ++cursor;
                while (named.descriptor[descriptor_index] == '[') {
                    ++descriptor_index;
                }
                if (named.descriptor[descriptor_index] == 'L') {
                    descriptor_index =
                        named.descriptor.find(';', descriptor_index) + 1;
                } else {
                    ++descriptor_index;
                }
            } else {
                arguments.push_back(VmValue::Int(static_cast<std::int32_t>(
                    GetCat1(frame, registers[cursor]))));
                ++cursor;
                ++descriptor_index;
            }
        }

        // Dispatch selection (02 §7 invoke semantics).
        VmMethodId target = resolved.method;
        if (base == 0x6e || base == 0x72) {  // virtual / interface
            const auto receiver_class = model->ObjectClass(receiver);
            if (!receiver_class.IsValid()) {
                FailCode("receiver class is unknown for " + named.name);
            }
            const auto index = linker->FindVtableIndex(
                receiver_class, named.name, named.descriptor);
            if (index.has_value()) {
                target = linker->Class(receiver_class).vtable[*index];
            } else if (linker->GapSurveyEnabled() &&
                       linker->Class(receiver_class).is_intrinsic) {
                // Survey mode: the receiver's platform class does not declare
                // the member; stub it on that class and keep going. Copies
                // first because synthesizing invalidates `named`.
                const auto name = named.name;
                const auto descriptor = named.descriptor;
                target = linker->SynthesizeSurveyMethod(receiver_class, name,
                                                        descriptor, false);
            } else {
                FailCode("virtual dispatch failed for " + named.name + " on " +
                         linker->Class(receiver_class).descriptor);
            }
        } else if (base == 0x6f) {  // super
            const auto& current = linker->Class(frame.method->owner);
            if (!current.super.has_value()) {
                FailCode("invoke-super without superclass");
            }
            const auto index = linker->FindVtableIndex(
                *current.super, named.name, named.descriptor);
            if (!index.has_value()) {
                FailCode("invoke-super dispatch failed for " + named.name);
            }
            target = linker->Class(*current.super).vtable[*index];
        }
        const auto& method = linker->Method(target);

        if (method.is_static) {
            EnsureInitialized(method.owner);
            if (pending_exception.IsValid()) return;
        }

        switch (method.kind) {
            case MethodKind::interpreted: {
                linker->PrecheckMethod(target);
                PushInterpretedFrame(method, arguments, width);
                return;
            }
            case MethodKind::intrinsic: {
                const auto rest =
                    method.is_static
                        ? std::span<const VmValue>(arguments)
                        : std::span<const VmValue>(arguments).subspan(1);
                frame.last_result = InvokeIntrinsic(method, receiver, rest);
                if (!pending_exception.IsValid()) advance();
                return;
            }
            case MethodKind::native: {
                if (bridge == nullptr) {
                    if (ledger != nullptr) {
                        ledger->RecordUnimplemented("dexvm.native_bridge",
                                                        0);
                    }
                    throw DexVmError(
                        DexVmErrorReason::native_bridge_unavailable,
                        "native method requires the JNI bridge: " +
                            linker->Class(method.owner).descriptor + "." +
                            method.name + method.descriptor);
                }
                const auto rest =
                    method.is_static
                        ? std::span<const VmValue>(arguments)
                        : std::span<const VmValue>(arguments).subspan(1);
                ++stats.native_calls;
                frame.last_result = bridge->Invoke(method, receiver, rest);
                if (!pending_exception.IsValid()) advance();
                return;
            }
            case MethodKind::abstract:
                FailCode("abstract method invoked: " + method.name);
        }
        return;
    }

    if (ledger != nullptr) {
        ledger->RecordUnimplemented(
            "dexvm.opcode." + std::string(info.name), 0);
    }
    throw DexVmError(DexVmErrorReason::unimplemented_opcode,
                     "opcode is not implemented: " + std::string(info.name));
}

}  // namespace ogplay::runtime::dexvm

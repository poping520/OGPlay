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

    const auto java_class = [&] {
        return DexClassId(instruction.resolved_id);
    };

    switch (opcode) {
        case 0x1d:
        case 0x1e: {
            const auto ref = GetFastRef(regs, instruction.a);
            if (!ref.IsValid()) {
                ThrowJava("Ljava/lang/NullPointerException;",
                          opcode == 0x1d ? "monitor-enter on null"
                                         : "monitor-exit on null");
                goto yield;
            }
            if (opcode == 0x1d) {
                monitors->Enter(ref, execution.token);
                OGPLAY_RECORD_TRACE(DexVmTraceKind::monitor_enter, execution,
                            frame.method, frame.pc, opcode, ref.Value());
            } else {
                monitors->Exit(ref, execution.token);
                OGPLAY_RECORD_TRACE(DexVmTraceKind::monitor_exit, execution,
                            frame.method, frame.pc, opcode, ref.Value());
            }
            OGPLAY_DISPATCH_NEXT();
        }
        case 0x1f: {
            const auto ref = GetFastRef(regs, instruction.a);
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
                    goto yield;
                }
            }
            OGPLAY_DISPATCH_NEXT();
        }
        case 0x20: {
            const auto ref = GetFastRef(regs, instruction.b);
            bool result = false;
            if (ref.IsValid()) {
                const auto source = model->ObjectClass(ref);
                result = source.IsValid() &&
                         linker->IsAssignable(java_class(), source);
            }
            SetFastCat1(regs, instruction.a, result ? 1U : 0U);
            OGPLAY_DISPATCH_NEXT();
        }
        case 0x21: {
            const auto ref = GetFastRef(regs, instruction.b);
            if (!ref.IsValid()) {
                ThrowJava("Ljava/lang/NullPointerException;",
                          "array-length on null");
                goto yield;
            }
            SetFastCat1(regs, instruction.a,
                        static_cast<std::uint32_t>(model->ArrayLength(ref)));
            OGPLAY_DISPATCH_NEXT();
        }
        case 0x22: {
            const auto type = java_class();
            PrepareSafeAllocation(JavaObjectModel::EstimateInstanceBytes(
                                      linker->Class(type).instance_slots),
                                  "new-instance");
            OGPLAY_SYNC_EXECUTION();
            EnsureInitialized(execution, type);
            ticks = execution.ticks;
            if (pending_exception.IsValid()) goto yield;
            SetFastRef(regs, instruction.a, AllocateInstance(type));
            OGPLAY_DISPATCH_NEXT();
        }
        case 0x23: {
            const auto length = static_cast<std::int32_t>(
                GetFastCat1(regs, instruction.b));
            if (length < 0) {
                ThrowJava("Ljava/lang/NegativeArraySizeException;",
                          std::to_string(length));
                goto yield;
            }
            const auto type = java_class();
            if ((instruction.resolved_aux & kObjectElementFlag) != 0U) {
                PrepareSafeAllocation(
                    JavaObjectModel::EstimateObjectArrayBytes(length),
                    "new-array");
                SetFastRef(regs, instruction.a,
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
                SetFastRef(regs, instruction.a,
                           model->NewPrimitiveArray(type, kind, length));
            }
            OGPLAY_DISPATCH_NEXT();
        }
        case 0x24:
        case 0x25: {
            std::array<std::uint32_t, 256> registers{};
            std::uint32_t register_count = instruction.a;
            if (opcode == 0x24) {
                const std::uint32_t candidates[5] = {
                    (instruction.c >> 0U) & 0xfU,
                    (instruction.c >> 4U) & 0xfU,
                    (instruction.c >> 8U) & 0xfU,
                    (instruction.c >> 12U) & 0xfU,
                    instruction.b};
                for (std::uint32_t index = 0; index < register_count;
                     ++index) {
                    registers[index] = candidates[index];
                }
            } else {
                for (std::uint32_t index = 0; index < register_count;
                     ++index) {
                    registers[index] = instruction.b + index;
                }
            }
            const auto type = java_class();
            if ((instruction.resolved_aux & kObjectElementFlag) != 0U) {
                PrepareSafeAllocation(
                    JavaObjectModel::EstimateObjectArrayBytes(
                        static_cast<JniSize>(register_count)),
                    "filled-new-array");
                const auto array = model->NewObjectArray(
                    type,
                    DexClassId(instruction.resolved_aux & ~kObjectElementFlag),
                    static_cast<JniSize>(register_count));
                for (std::uint32_t index = 0; index < register_count;
                     ++index) {
                    model->SetObjectElement(
                        array, static_cast<JniSize>(index),
                        GetFastRef(regs, registers[index]));
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
                        kind, static_cast<JniSize>(register_count)),
                    "filled-new-array");
                const auto array = model->NewPrimitiveArray(
                    type, kind, static_cast<JniSize>(register_count));
                for (std::uint32_t index = 0; index < register_count;
                     ++index) {
                    model->SetPrimitiveElement(
                        array, static_cast<JniSize>(index),
                        GetFastCat1(regs, registers[index]));
                }
                frame.last_result = VmValue::Ref(array);
            }
            OGPLAY_DISPATCH_NEXT();
        }
        case 0x26: {
            const auto ref = GetFastRef(regs, instruction.a);
            if (!ref.IsValid()) {
                ThrowJava("Ljava/lang/NullPointerException;",
                          "fill-array-data on null");
                goto yield;
            }
            const auto& payload = code.payloads[instruction.payload];
            if (payload.element_count >
                static_cast<std::uint32_t>(model->ArrayLength(ref))) {
                ThrowJava("Ljava/lang/ArrayIndexOutOfBoundsException;",
                          "fill-array-data size exceeds array length");
                goto yield;
            }
            consume_extra_ticks(payload.tick_weight);
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
            OGPLAY_DISPATCH_NEXT();
        }
        case 0x27:
            SetPending(GetFastRef(regs, instruction.a));
            goto yield;
        case 0x2b:
        case 0x2c: {
            const auto key = static_cast<std::int32_t>(
                GetFastCat1(regs, instruction.a));
            const auto& payload = code.payloads[instruction.payload];
            if (opcode == 0x2c) consume_extra_ticks(payload.tick_weight);
            if (opcode == 0x2b) {
                if (!payload.keys.empty()) {
                    const auto index =
                        static_cast<std::int64_t>(key) - payload.keys.front();
                    if (index >= 0 &&
                        static_cast<std::uint64_t>(index) <
                            payload.targets.size()) {
                        OGPLAY_DISPATCH_TO(
                            payload.targets[static_cast<std::size_t>(index)]);
                    }
                }
                OGPLAY_DISPATCH_NEXT();
            }
            const auto found = std::find(payload.keys.begin(),
                                         payload.keys.end(), key);
            if (found != payload.keys.end()) {
                OGPLAY_DISPATCH_TO(payload.targets[static_cast<std::size_t>(
                    found - payload.keys.begin())]);
            }
            OGPLAY_DISPATCH_NEXT();
        }
        default:
            break;
    }

    if (opcode >= 0x44U && opcode <= 0x51U) {
        const auto array = GetFastRef(regs, instruction.b);
        if (!array.IsValid()) {
            ThrowJava("Ljava/lang/NullPointerException;",
                      "array access on null");
            goto yield;
        }
        const auto index = static_cast<std::int32_t>(
            GetFastCat1(regs, instruction.c));
        const auto length = model->ArrayLength(array);
        if (index < 0 || index >= length) {
            ThrowJava("Ljava/lang/ArrayIndexOutOfBoundsException;",
                      "index " + std::to_string(index) + " length " +
                          std::to_string(length));
            goto yield;
        }
        const bool is_object = opcode == 0x46 || opcode == 0x4d;
        const bool is_wide = opcode == 0x45 || opcode == 0x4c;
        const bool is_get = opcode <= 0x4a;
        if (is_object) {
            if (model->Kind(array) != VmObjectKind::object_array) {
                FailCode("aget/aput-object on non-object array");
            }
            if (is_get) {
                SetFastRef(regs, instruction.a,
                           model->GetObjectElement(array, index));
            } else {
                const auto value = GetFastRef(regs, instruction.a);
                if (value.IsValid()) {
                    const auto element_class =
                        model->ObjectArrayElementClass(array);
                    const auto value_class = model->ObjectClass(value);
                    if (value_class.IsValid() &&
                        !linker->IsAssignable(element_class, value_class)) {
                        ThrowJava("Ljava/lang/ArrayStoreException;",
                                  linker->Class(value_class).descriptor);
                        goto yield;
                    }
                }
                model->SetObjectElement(array, index, value);
            }
            OGPLAY_DISPATCH_NEXT();
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
                SetFastWide(regs, instruction.a, bits);
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
                SetFastCat1(regs, instruction.a, narrowed);
            }
        } else {
            model->SetPrimitiveElement(
                array, index,
                is_wide ? GetFastWide(regs, instruction.a)
                        : static_cast<std::uint64_t>(
                              GetFastCat1(regs, instruction.a)));
        }
        OGPLAY_DISPATCH_NEXT();
    }

    if (opcode >= 0x52U && opcode <= 0x5fU) {
        const auto& field = linker->Field(VmFieldId(instruction.resolved_id));
        const auto receiver = GetFastRef(regs, instruction.b);
        if (!receiver.IsValid()) {
            ThrowJava("Ljava/lang/NullPointerException;",
                      "field access on null: " + field.name);
            goto yield;
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
                SetFastRef(regs, instruction.a,
                           VmObjectRef(slots[field.slot].bits));
            } else {
                SetFastCat1(regs, instruction.a, slots[field.slot].bits);
            }
        } else if (field.is_wide) {
            const auto bits = GetFastWide(regs, instruction.a);
            slots[field.slot] = {static_cast<std::uint32_t>(bits),
                                 SlotTag::wide_lo};
            slots[field.slot + 1] = {
                static_cast<std::uint32_t>(bits >> 32U), SlotTag::wide_hi};
        } else if (field.is_ref) {
            slots[field.slot] = {
                GetFastRef(regs, instruction.a).Value(), SlotTag::ref};
        } else {
            slots[field.slot] = {
                GetFastCat1(regs, instruction.a), SlotTag::cat1};
        }
        OGPLAY_DISPATCH_NEXT();
    }

    if (opcode >= 0x60U && opcode <= 0x6dU) {
        const auto& field = linker->Field(VmFieldId(instruction.resolved_id));
        OGPLAY_SYNC_EXECUTION();
        EnsureInitialized(execution, field.owner);
        ticks = execution.ticks;
        if (pending_exception.IsValid()) goto yield;
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
                SetFastRef(regs, instruction.a,
                           VmObjectRef(storage[field.slot]));
            } else {
                SetFastCat1(regs, instruction.a, storage[field.slot]);
            }
        } else if (field.is_wide) {
            const auto bits = GetFastWide(regs, instruction.a);
            storage[field.slot] = static_cast<std::uint32_t>(bits);
            storage[field.slot + 1] =
                static_cast<std::uint32_t>(bits >> 32U);
        } else if (field.is_ref) {
            storage[field.slot] = GetFastRef(regs, instruction.a).Value();
        } else {
            storage[field.slot] = GetFastCat1(regs, instruction.a);
        }
        OGPLAY_DISPATCH_NEXT();
    }

    FailCode("non-object opcode reached object handler");

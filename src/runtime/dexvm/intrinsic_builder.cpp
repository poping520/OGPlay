#include "ogplay/runtime/dexvm/intrinsic_builder.h"

#include <atomic>
#include <bit>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "ogplay/runtime/jni/jni_signature.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm {
    namespace {
        std::atomic<std::uint64_t> next_field_binding_token{1U};

        [[noreturn]] void FailBuild(std::string message) {
            throw DexVmError(DexVmErrorReason::internal_invariant, std::move(message));
        }

        void ValidateConstructorDescriptor(const std::string_view descriptor) {
            try {
                const auto parsed = ParseJniMethodDescriptor(descriptor);
                if (parsed.result.kind != JniTypeKind::void_value) {
                    FailBuild("intrinsic constructor must return void: " + std::string(descriptor));
                }
            } catch (const JniSignatureError& error) {
                FailBuild(
                    "invalid intrinsic constructor descriptor " + std::string(descriptor) + ": " + error.what());
            }
        }

        void ValidateClassDescriptor(const std::string_view descriptor) {
            try {
                const auto parsed = ParseJniFieldDescriptor(descriptor);
                if (parsed.kind != JniTypeKind::object) {
                    FailBuild("intrinsic class descriptor is not an object: " + std::string(descriptor));
                }
            } catch (const JniSignatureError& error) {
                FailBuild("invalid intrinsic class descriptor " + std::string(descriptor) + ": " + error.what());
            }
        }

        void ValidateMethodDescriptor(const std::string_view descriptor) {
            try {
                static_cast<void>(ParseJniMethodDescriptor(descriptor));
            } catch (const JniSignatureError& error) {
                FailBuild("invalid intrinsic method descriptor " + std::string(descriptor) + ": " + error.what());
            }
        }

        void ValidateFieldDescriptor(const std::string_view descriptor) {
            try {
                static_cast<void>(ParseJniFieldDescriptor(descriptor));
            } catch (const JniSignatureError& error) {
                FailBuild("invalid intrinsic field descriptor " + std::string(descriptor) + ": " + error.what());
            }
        }

        [[nodiscard]] std::string MemberKey(const std::string& name,
                                            const std::string& descriptor) {
            return name + '\0' + descriptor;
        }

        void ValidateImplementedHandler(const IntrinsicHandler& handler,
                                        const std::string_view member) {
            if (!handler) {
                FailBuild("intrinsic implemented member has an empty handler: " + std::string(member));
            }
        }

        void ValidateOrdinaryMethodName(const std::string_view name) {
            if (name == "<init>" || name == "<clinit>") {
                FailBuild("intrinsic ordinary method uses reserved name: " + std::string(name));
            }
        }

        void ValidateIntegralConstant(const IntrinsicFieldDecl& field) {
            const auto value = field.integral;
            const auto in_range = [value](const std::int64_t minimum, const std::int64_t maximum) {
                return value >= minimum && value <= maximum;
            };

            bool valid = false;
            if (field.descriptor == "Z") {
                valid = value == 0 || value == 1;
            } else if (field.descriptor == "B") {
                valid = in_range(std::numeric_limits<std::int8_t>::min(), std::numeric_limits<std::int8_t>::max());
            } else if (field.descriptor == "C") {
                valid = in_range(0, std::numeric_limits<std::uint16_t>::max());
            } else if (field.descriptor == "S") {
                valid = in_range(std::numeric_limits<std::int16_t>::min(), std::numeric_limits<std::int16_t>::max());
            } else if (field.descriptor == "I") {
                valid = in_range(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
            } else if (field.descriptor == "J" || field.descriptor == "D") {
                valid = true;
            } else if (field.descriptor == "F") {
                // Float and double constants retain the existing raw IEEE-754 bit
                // representation used by IntrinsicFieldDecl.
                valid = in_range(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
            }

            if (!valid) {
                FailBuild("intrinsic integral constant has incompatible descriptor " +
                          field.descriptor + " or out-of-range value: " + field.name);
            }
        }

        [[nodiscard]] const VmValue& Argument(const IntrinsicContext& context,
                                              const std::size_t index,
                                              const VmValue::Kind kind) {
            if (index >= context.arguments.size() ||
                context.arguments[index].kind != kind) {
                FailBuild("intrinsic argument kind mismatch at index " +
                          std::to_string(index));
            }
            return context.arguments[index];
        }

        [[nodiscard]] const LinkedField& BoundField(
            IntrinsicContext& context, const std::uint64_t token) {
            return context.vm.Linker().Field(
                context.vm.Linker().ResolveIntrinsicFieldBinding(token));
        }

        [[nodiscard]] VmObjectRef FieldObject(IntrinsicContext& context,
                                              const LinkedField& field,
                                              const VmObjectRef requested,
                                              const bool explicit_object) {
            if (field.is_static) {
                if (explicit_object) {
                    FailBuild("static intrinsic field used with an object");
                }
                return VmObjectRef{};
            }
            const auto object = explicit_object ? requested : context.receiver;
            if (!object.IsValid()) {
                FailBuild("instance intrinsic field has no receiver");
            }
            const auto actual = context.vm.Model().ObjectClass(object);
            if (!context.vm.Linker().IsAssignable(field.owner, actual)) {
                FailBuild("intrinsic field receiver has an incompatible class");
            }
            return object;
        }

        [[nodiscard]] std::span<Slot> FieldSlots(IntrinsicContext& context,
                                                 const LinkedField& field,
                                                 const VmObjectRef object) {
            auto slots = context.vm.Model().InstanceSlots(object);
            const auto width = field.is_wide ? 2U : 1U;
            if (static_cast<std::size_t>(field.slot) + width > slots.size()) {
                FailBuild("intrinsic field slot is out of range: " + field.name);
            }
            return slots;
        }

        void RequireIntField(const LinkedField& field) {
            if (field.is_ref || field.is_wide || field.descriptor == "F") {
                FailBuild("intrinsic field is not int-like: " + field.name);
            }
        }

        void RequireLongField(const LinkedField& field) {
            if (field.descriptor != "J") {
                FailBuild("intrinsic field is not long: " + field.name);
            }
        }

        void RequireFloatField(const LinkedField& field) {
            if (field.descriptor != "F") {
                FailBuild("intrinsic field is not float: " + field.name);
            }
        }

        void RequireDoubleField(const LinkedField& field) {
            if (field.descriptor != "D") {
                FailBuild("intrinsic field is not double: " + field.name);
            }
        }

        void RequireRefField(const LinkedField& field) {
            if (!field.is_ref) {
                FailBuild("intrinsic field is not a reference: " + field.name);
            }
        }

        [[nodiscard]] std::uint32_t ReadCat1Slot(const Slot& slot,
                                                 const LinkedField& field) {
            if (slot.tag == SlotTag::uninit) return 0U;
            if (slot.tag != SlotTag::cat1) {
                FailBuild("intrinsic field has an invalid cat1 slot tag: " +
                          field.name);
            }
            return slot.bits;
        }

        [[nodiscard]] std::uint64_t ReadWideSlots(const std::span<Slot> slots,
                                                  const LinkedField& field) {
            if (slots[field.slot].tag == SlotTag::uninit &&
                slots[field.slot + 1U].tag == SlotTag::uninit) {
                return 0U;
            }
            if (slots[field.slot].tag != SlotTag::wide_lo ||
                slots[field.slot + 1U].tag != SlotTag::wide_hi) {
                FailBuild("intrinsic field has invalid wide slot tags: " +
                          field.name);
            }
            return static_cast<std::uint64_t>(slots[field.slot].bits) |
                   (static_cast<std::uint64_t>(slots[field.slot + 1U].bits) << 32U);
        }

        [[nodiscard]] VmObjectRef ReadRefSlot(const Slot& slot,
                                              const LinkedField& field) {
            if (slot.tag == SlotTag::uninit) return VmObjectRef{};
            if (slot.tag != SlotTag::ref) {
                FailBuild("intrinsic reference field has an invalid slot tag: " +
                          field.name);
            }
            return VmObjectRef(slot.bits);
        }
    } // namespace

    Interpreter& IntrinsicCall::Vm() const noexcept { return context_->vm; }

    VmObjectRef IntrinsicCall::Receiver() const {
        if (!context_->receiver.IsValid()) {
            FailBuild("intrinsic call has no receiver");
        }
        return context_->receiver;
    }

    std::int32_t IntrinsicCall::Int(const std::size_t index) const {
        return Argument(*context_, index, VmValue::Kind::cat1).AsInt();
    }

    std::int64_t IntrinsicCall::Long(const std::size_t index) const {
        return Argument(*context_, index, VmValue::Kind::wide).AsLong();
    }

    float IntrinsicCall::Float(const std::size_t index) const {
        return Argument(*context_, index, VmValue::Kind::cat1).AsFloat();
    }

    double IntrinsicCall::Double(const std::size_t index) const {
        return Argument(*context_, index, VmValue::Kind::wide).AsDouble();
    }

    VmObjectRef IntrinsicCall::Ref(const std::size_t index) const {
        return Argument(*context_, index, VmValue::Kind::ref).ref;
    }

    VmObjectRef IntrinsicCall::NonNullRef(
        const std::size_t index, const std::string_view parameter) const {
        const auto value = Ref(index);
        if (!value.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                              std::string(parameter) + " == null"};
        }
        return value;
    }

    std::int32_t IntrinsicCall::GetInt(const IntrinsicFieldHandle field) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireIntField(linked);
        const auto object = FieldObject(*context_, linked, VmObjectRef{}, false);
        if (linked.is_static) {
            return static_cast<std::int32_t>(
                context_->vm.Linker().Class(linked.owner).static_storage[linked.slot]);
        }
        const auto slot = FieldSlots(*context_, linked, object)[linked.slot];
        return static_cast<std::int32_t>(ReadCat1Slot(slot, linked));
    }

    std::int32_t IntrinsicCall::GetInt(const IntrinsicFieldHandle field,
                                       const VmObjectRef object) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireIntField(linked);
        const auto actual = FieldObject(*context_, linked, object, true);
        const auto slot = FieldSlots(*context_, linked, actual)[linked.slot];
        return static_cast<std::int32_t>(ReadCat1Slot(slot, linked));
    }

    void IntrinsicCall::SetInt(const IntrinsicFieldHandle field,
                               const std::int32_t value) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireIntField(linked);
        const auto object = FieldObject(*context_, linked, VmObjectRef{}, false);
        if (linked.is_static) {
            context_->vm.Linker().MutableClass(linked.owner)
                .static_storage[linked.slot] = static_cast<std::uint32_t>(value);
            return;
        }
        FieldSlots(*context_, linked, object)[linked.slot] = {
            static_cast<std::uint32_t>(value), SlotTag::cat1};
    }

    void IntrinsicCall::SetInt(const IntrinsicFieldHandle field,
                               const VmObjectRef object,
                               const std::int32_t value) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireIntField(linked);
        const auto actual = FieldObject(*context_, linked, object, true);
        FieldSlots(*context_, linked, actual)[linked.slot] = {
            static_cast<std::uint32_t>(value), SlotTag::cat1};
    }

    std::int64_t IntrinsicCall::GetLong(const IntrinsicFieldHandle field) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireLongField(linked);
        const auto object = FieldObject(*context_, linked, VmObjectRef{}, false);
        if (linked.is_static) {
            const auto& storage = context_->vm.Linker().Class(linked.owner).static_storage;
            return static_cast<std::int64_t>(
                static_cast<std::uint64_t>(storage[linked.slot]) |
                (static_cast<std::uint64_t>(storage[linked.slot + 1U]) << 32U));
        }
        const auto slots = FieldSlots(*context_, linked, object);
        return static_cast<std::int64_t>(ReadWideSlots(slots, linked));
    }

    std::int64_t IntrinsicCall::GetLong(const IntrinsicFieldHandle field,
                                        const VmObjectRef object) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireLongField(linked);
        const auto actual = FieldObject(*context_, linked, object, true);
        const auto slots = FieldSlots(*context_, linked, actual);
        return static_cast<std::int64_t>(ReadWideSlots(slots, linked));
    }

    void IntrinsicCall::SetLong(const IntrinsicFieldHandle field,
                                const std::int64_t value) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireLongField(linked);
        const auto object = FieldObject(*context_, linked, VmObjectRef{}, false);
        const auto bits = static_cast<std::uint64_t>(value);
        if (linked.is_static) {
            auto& storage = context_->vm.Linker().MutableClass(linked.owner).static_storage;
            storage[linked.slot] = static_cast<std::uint32_t>(bits);
            storage[linked.slot + 1U] = static_cast<std::uint32_t>(bits >> 32U);
            return;
        }
        auto slots = FieldSlots(*context_, linked, object);
        slots[linked.slot] = {static_cast<std::uint32_t>(bits), SlotTag::wide_lo};
        slots[linked.slot + 1U] = {static_cast<std::uint32_t>(bits >> 32U),
                                  SlotTag::wide_hi};
    }

    float IntrinsicCall::GetFloat(const IntrinsicFieldHandle field) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireFloatField(linked);
        const auto object = FieldObject(*context_, linked, VmObjectRef{}, false);
        if (linked.is_static) {
            return std::bit_cast<float>(context_->vm.Linker().Class(linked.owner)
                                            .static_storage[linked.slot]);
        }
        const auto slot = FieldSlots(*context_, linked, object)[linked.slot];
        return std::bit_cast<float>(ReadCat1Slot(slot, linked));
    }

    float IntrinsicCall::GetFloat(const IntrinsicFieldHandle field,
                                  const VmObjectRef object) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireFloatField(linked);
        const auto actual = FieldObject(*context_, linked, object, true);
        const auto slot = FieldSlots(*context_, linked, actual)[linked.slot];
        return std::bit_cast<float>(ReadCat1Slot(slot, linked));
    }

    void IntrinsicCall::SetFloat(const IntrinsicFieldHandle field,
                                 const float value) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireFloatField(linked);
        const auto object = FieldObject(*context_, linked, VmObjectRef{}, false);
        const auto bits = std::bit_cast<std::uint32_t>(value);
        if (linked.is_static) {
            context_->vm.Linker().MutableClass(linked.owner)
                .static_storage[linked.slot] = bits;
            return;
        }
        FieldSlots(*context_, linked, object)[linked.slot] = {bits, SlotTag::cat1};
    }

    void IntrinsicCall::SetFloat(const IntrinsicFieldHandle field,
                                 const VmObjectRef object,
                                 const float value) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireFloatField(linked);
        const auto actual = FieldObject(*context_, linked, object, true);
        FieldSlots(*context_, linked, actual)[linked.slot] = {
            std::bit_cast<std::uint32_t>(value), SlotTag::cat1};
    }

    double IntrinsicCall::GetDouble(const IntrinsicFieldHandle field) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireDoubleField(linked);
        const auto object = FieldObject(*context_, linked, VmObjectRef{}, false);
        std::uint64_t bits{};
        if (linked.is_static) {
            const auto& storage = context_->vm.Linker().Class(linked.owner).static_storage;
            bits = static_cast<std::uint64_t>(storage[linked.slot]) |
                   (static_cast<std::uint64_t>(storage[linked.slot + 1U]) << 32U);
        } else {
            const auto slots = FieldSlots(*context_, linked, object);
            bits = ReadWideSlots(slots, linked);
        }
        return std::bit_cast<double>(bits);
    }

    double IntrinsicCall::GetDouble(const IntrinsicFieldHandle field,
                                    const VmObjectRef object) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireDoubleField(linked);
        const auto actual = FieldObject(*context_, linked, object, true);
        const auto slots = FieldSlots(*context_, linked, actual);
        return std::bit_cast<double>(ReadWideSlots(slots, linked));
    }

    void IntrinsicCall::SetDouble(const IntrinsicFieldHandle field,
                                  const double value) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireDoubleField(linked);
        const auto object = FieldObject(*context_, linked, VmObjectRef{}, false);
        const auto bits = std::bit_cast<std::uint64_t>(value);
        if (linked.is_static) {
            auto& storage = context_->vm.Linker().MutableClass(linked.owner).static_storage;
            storage[linked.slot] = static_cast<std::uint32_t>(bits);
            storage[linked.slot + 1U] = static_cast<std::uint32_t>(bits >> 32U);
            return;
        }
        auto slots = FieldSlots(*context_, linked, object);
        slots[linked.slot] = {static_cast<std::uint32_t>(bits), SlotTag::wide_lo};
        slots[linked.slot + 1U] = {static_cast<std::uint32_t>(bits >> 32U),
                                  SlotTag::wide_hi};
    }

    void IntrinsicCall::SetDouble(const IntrinsicFieldHandle field,
                                  const VmObjectRef object,
                                  const double value) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireDoubleField(linked);
        const auto actual = FieldObject(*context_, linked, object, true);
        const auto bits = std::bit_cast<std::uint64_t>(value);
        auto slots = FieldSlots(*context_, linked, actual);
        slots[linked.slot] = {static_cast<std::uint32_t>(bits), SlotTag::wide_lo};
        slots[linked.slot + 1U] = {static_cast<std::uint32_t>(bits >> 32U),
                                  SlotTag::wide_hi};
    }

    void IntrinsicCall::SetLong(const IntrinsicFieldHandle field,
                                const VmObjectRef object,
                                const std::int64_t value) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireLongField(linked);
        const auto actual = FieldObject(*context_, linked, object, true);
        const auto bits = static_cast<std::uint64_t>(value);
        auto slots = FieldSlots(*context_, linked, actual);
        slots[linked.slot] = {static_cast<std::uint32_t>(bits), SlotTag::wide_lo};
        slots[linked.slot + 1U] = {static_cast<std::uint32_t>(bits >> 32U),
                                  SlotTag::wide_hi};
    }

    VmObjectRef IntrinsicCall::GetRef(const IntrinsicFieldHandle field) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireRefField(linked);
        const auto object = FieldObject(*context_, linked, VmObjectRef{}, false);
        if (linked.is_static) {
            return VmObjectRef(context_->vm.Linker().Class(linked.owner)
                                   .static_storage[linked.slot]);
        }
        return ReadRefSlot(FieldSlots(*context_, linked, object)[linked.slot], linked);
    }

    VmObjectRef IntrinsicCall::GetRef(const IntrinsicFieldHandle field,
                                      const VmObjectRef object) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireRefField(linked);
        const auto actual = FieldObject(*context_, linked, object, true);
        return ReadRefSlot(FieldSlots(*context_, linked, actual)[linked.slot], linked);
    }

    void IntrinsicCall::SetRef(const IntrinsicFieldHandle field,
                               const VmObjectRef value) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireRefField(linked);
        const auto object = FieldObject(*context_, linked, VmObjectRef{}, false);
        if (value.IsValid()) {
            const auto target = context_->vm.Linker().ResolveDescriptor(linked.descriptor);
            const auto actual = context_->vm.Model().ObjectClass(value);
            if (!context_->vm.Linker().IsAssignable(target, actual)) {
                FailBuild("intrinsic reference field value has an incompatible class");
            }
        }
        if (linked.is_static) {
            context_->vm.Linker().MutableClass(linked.owner)
                .static_storage[linked.slot] = value.Value();
            return;
        }
        FieldSlots(*context_, linked, object)[linked.slot] = {value.Value(), SlotTag::ref};
    }

    void IntrinsicCall::SetRef(const IntrinsicFieldHandle field,
                               const VmObjectRef object,
                               const VmObjectRef value) const {
        const auto& linked = BoundField(*context_, field.token_);
        RequireRefField(linked);
        const auto actual_object = FieldObject(*context_, linked, object, true);
        if (value.IsValid()) {
            const auto target = context_->vm.Linker().ResolveDescriptor(linked.descriptor);
            const auto actual = context_->vm.Model().ObjectClass(value);
            if (!context_->vm.Linker().IsAssignable(target, actual)) {
                FailBuild("intrinsic reference field value has an incompatible class");
            }
        }
        FieldSlots(*context_, linked, actual_object)[linked.slot] = {
            value.Value(), SlotTag::ref};
    }

    IntrinsicClassBuilder IntrinsicClassBuilder::RootClass(
        std::string descriptor, const std::uint32_t access_flags) {
        IntrinsicClassBuilder builder(std::move(descriptor));
        builder.declaration_.superclass = std::nullopt;
        builder.declaration_.access_flags = access_flags;
        return builder;
    }

    IntrinsicClassBuilder IntrinsicClassBuilder::Class(
        std::string descriptor, std::optional<std::string> superclass,
        std::vector<std::string> interfaces,
        const std::uint32_t access_flags) {
        IntrinsicClassBuilder builder(std::move(descriptor));
        builder.declaration_.superclass = std::move(superclass);
        builder.declaration_.interfaces = std::move(interfaces);
        builder.declaration_.access_flags = access_flags;
        return builder;
    }

    IntrinsicClassBuilder IntrinsicClassBuilder::Interface(
        std::string descriptor, std::vector<std::string> super_interfaces,
        const std::uint32_t access_flags) {
        IntrinsicClassBuilder builder(std::move(descriptor));
        builder.declaration_.interfaces = std::move(super_interfaces);
        builder.declaration_.is_interface = true;
        builder.declaration_.access_flags =
            access_flags | kAccInterface | kAccAbstract;
        return builder;
    }

    IntrinsicClassBuilder::IntrinsicClassBuilder(std::string descriptor) {
        declaration_.descriptor = std::move(descriptor);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::Method(
        std::string name, std::string descriptor, const MethodType type,
        IntrinsicHandler handler, const std::uint32_t access_flags) {
        IntrinsicMethodDecl method;
        method.name = std::move(name);
        method.descriptor = std::move(descriptor);
        method.is_static = type == MethodType::static_method;
        method.overridable = type == MethodType::virtual_method ||
                             type == MethodType::override_method;
        method.must_override = type == MethodType::override_method ||
                               type == MethodType::final_override_method;
        method.access_flags = access_flags;
        switch (type) {
        case MethodType::constructor:
            method.access_flags |= kAccConstructor;
            method.invoke_kind = DeclaredInvokeKind::direct;
            break;
        case MethodType::direct_method:
            method.invoke_kind = DeclaredInvokeKind::direct;
            break;
        case MethodType::static_method:
            method.access_flags |= kAccStatic;
            method.invoke_kind = DeclaredInvokeKind::static_call;
            break;
        case MethodType::virtual_method:
        case MethodType::override_method:
            method.invoke_kind = declaration_.is_interface
                                     ? DeclaredInvokeKind::interface_call
                                     : DeclaredInvokeKind::virtual_call;
            break;
        case MethodType::final_override_method:
            method.access_flags |= kAccFinal;
            method.invoke_kind = declaration_.is_interface
                                     ? DeclaredInvokeKind::interface_call
                                     : DeclaredInvokeKind::virtual_call;
            break;
        case MethodType::final_method:
            method.access_flags |= kAccFinal;
            method.invoke_kind = declaration_.is_interface
                                     ? DeclaredInvokeKind::interface_call
                                     : DeclaredInvokeKind::virtual_call;
            break;
        }
        method.implementation = std::move(handler);
        declaration_.methods.push_back(std::move(method));
        return *this;
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::Constructor(
        std::string descriptor, IntrinsicHandler handler,
        const std::uint32_t access_flags) {
        ValidateConstructorDescriptor(descriptor);
        ValidateImplementedHandler(handler, "<init>");
        return Method("<init>", std::move(descriptor), MethodType::constructor,
                      std::move(handler), access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::DirectMethod(
        std::string name, std::string descriptor, IntrinsicHandler handler,
        const std::uint32_t access_flags) {
        ValidateOrdinaryMethodName(name);
        ValidateImplementedHandler(handler, name);
        return Method(std::move(name), std::move(descriptor),
                      MethodType::direct_method, std::move(handler),
                      access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::StaticMethod(
        std::string name, std::string descriptor, IntrinsicHandler handler,
        const std::uint32_t access_flags) {
        ValidateOrdinaryMethodName(name);
        ValidateImplementedHandler(handler, name);
        return Method(std::move(name), std::move(descriptor),
                      MethodType::static_method, std::move(handler),
                      access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::VirtualMethod(
        std::string name, std::string descriptor, IntrinsicHandler handler,
        const std::uint32_t access_flags) {
        ValidateOrdinaryMethodName(name);
        ValidateImplementedHandler(handler, name);
        return Method(std::move(name), std::move(descriptor),
                      MethodType::virtual_method, std::move(handler),
                      access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::OverrideMethod(
        std::string name, std::string descriptor, IntrinsicHandler handler,
        const std::uint32_t access_flags) {
        ValidateOrdinaryMethodName(name);
        ValidateImplementedHandler(handler, name);
        return Method(std::move(name), std::move(descriptor),
                      MethodType::override_method, std::move(handler),
                      access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::FinalOverrideMethod(
        std::string name, std::string descriptor, IntrinsicHandler handler,
        const std::uint32_t access_flags) {
        ValidateOrdinaryMethodName(name);
        ValidateImplementedHandler(handler, name);
        return Method(std::move(name), std::move(descriptor),
                      MethodType::final_override_method,
                      std::move(handler), access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::FinalMethod(
        std::string name, std::string descriptor, IntrinsicHandler handler,
        const std::uint32_t access_flags) {
        ValidateOrdinaryMethodName(name);
        ValidateImplementedHandler(handler, name);
        return Method(std::move(name), std::move(descriptor),
                      MethodType::final_method, std::move(handler),
                      access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::UnimplementedMethod(
        std::string name, std::string descriptor, const MethodType type,
        const std::uint32_t access_flags) {
        return Method(std::move(name), std::move(descriptor), type, {},
                      access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::UnimplementedStatic(
        std::string name, std::string descriptor,
        const std::uint32_t access_flags) {
        ValidateOrdinaryMethodName(name);
        return UnimplementedMethod(std::move(name), std::move(descriptor),
                                   MethodType::static_method, access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::UnimplementedConstructor(
        std::string descriptor, const std::uint32_t access_flags) {
        ValidateConstructorDescriptor(descriptor);
        return UnimplementedMethod("<init>", std::move(descriptor),
                                   MethodType::constructor, access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::UnimplementedDirect(
        std::string name, std::string descriptor,
        const std::uint32_t access_flags) {
        ValidateOrdinaryMethodName(name);
        return UnimplementedMethod(std::move(name), std::move(descriptor),
                                   MethodType::direct_method, access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::UnimplementedVirtual(
        std::string name, std::string descriptor,
        const std::uint32_t access_flags) {
        ValidateOrdinaryMethodName(name);
        return UnimplementedMethod(std::move(name), std::move(descriptor),
                                   MethodType::virtual_method, access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::UnimplementedOverride(
        std::string name, std::string descriptor,
        const std::uint32_t access_flags) {
        ValidateOrdinaryMethodName(name);
        return UnimplementedMethod(std::move(name), std::move(descriptor),
                                   MethodType::override_method, access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::UnimplementedFinal(
        std::string name, std::string descriptor,
        const std::uint32_t access_flags) {
        ValidateOrdinaryMethodName(name);
        return UnimplementedMethod(std::move(name), std::move(descriptor),
                                   MethodType::final_method, access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::Field(
        std::string name, std::string descriptor, const FieldType type,
        const std::uint32_t access_flags) {
        IntrinsicFieldDecl field;
        field.name = std::move(name);
        field.descriptor = std::move(descriptor);
        field.is_static = type == FieldType::static_field;
        field.access_flags =
            access_flags | (field.is_static ? kAccStatic : kAccNone);
        declaration_.fields.push_back(std::move(field));
        return *this;
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::InstanceField(
        std::string name, std::string descriptor,
        const std::uint32_t access_flags) {
        return Field(std::move(name), std::move(descriptor),
                     FieldType::instance, access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::StaticField(
        std::string name, std::string descriptor,
        const std::uint32_t access_flags) {
        return Field(std::move(name), std::move(descriptor),
                     FieldType::static_field, access_flags);
    }

    IntrinsicFieldHandle IntrinsicClassBuilder::BoundField(
        std::string name, std::string descriptor, const FieldType type,
        const std::uint32_t access_flags) {
        const auto token = next_field_binding_token.fetch_add(
            1U, std::memory_order_relaxed);
        if (token == 0U) {
            FailBuild("intrinsic field binding token space exhausted");
        }
        IntrinsicFieldDecl field;
        field.name = std::move(name);
        field.descriptor = std::move(descriptor);
        field.is_static = type == FieldType::static_field;
        field.access_flags =
            access_flags | (field.is_static ? kAccStatic : kAccNone);
        field.binding_token = token;
        declaration_.fields.push_back(std::move(field));
        return IntrinsicFieldHandle(token);
    }

    IntrinsicFieldHandle IntrinsicClassBuilder::BoundInstanceField(
        std::string name, std::string descriptor,
        const std::uint32_t access_flags) {
        return BoundField(std::move(name), std::move(descriptor),
                          FieldType::instance, access_flags);
    }

    IntrinsicFieldHandle IntrinsicClassBuilder::BoundStaticField(
        std::string name, std::string descriptor,
        const std::uint32_t access_flags) {
        return BoundField(std::move(name), std::move(descriptor),
                          FieldType::static_field, access_flags);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::ConstantInt(
        std::string name, std::string descriptor, const std::int64_t value,
        const std::uint32_t access_flags) {
        IntrinsicFieldDecl field;
        field.name = std::move(name);
        field.descriptor = std::move(descriptor);
        field.is_static = true;
        field.has_constant = true;
        field.access_flags = access_flags | kAccStatic | kAccFinal;
        field.integral = value;
        declaration_.fields.push_back(std::move(field));
        return *this;
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::ConstantString(
        std::string name, std::string value,
        const std::uint32_t access_flags) {
        IntrinsicFieldDecl field;
        field.name = std::move(name);
        field.descriptor = "Ljava/lang/String;";
        field.is_static = true;
        field.has_constant = true;
        field.access_flags = access_flags | kAccStatic | kAccFinal;
        field.string_value = std::move(value);
        declaration_.fields.push_back(std::move(field));
        return *this;
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::ClassInitializer(
        IntrinsicHandler handler) {
        ValidateImplementedHandler(handler, "<clinit>");
        declaration_.clinit_implementation = std::move(handler);
        return *this;
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::HostStateDestructor(
        ogplay::runtime::dexvm::HostStateDestructor destructor) {
        if (!destructor) {
            FailBuild("intrinsic host-state destructor is empty");
        }
        declaration_.host_state_destructor = std::move(destructor);
        return *this;
    }

    IntrinsicClassDecl IntrinsicClassBuilder::Build() && {
        ValidateClassDescriptor(declaration_.descriptor);

        if (declaration_.superclass.has_value()) {
            ValidateClassDescriptor(*declaration_.superclass);

            if (*declaration_.superclass == declaration_.descriptor) {
                FailBuild("intrinsic class cannot extend itself: " + declaration_.descriptor);
            }
        }

        for (const auto& interface_descriptor: declaration_.interfaces) {
            ValidateClassDescriptor(interface_descriptor);
        }

        std::unordered_set<std::string> method_keys;
        for (const auto& method: declaration_.methods) {
            ValidateMethodDescriptor(method.descriptor);

            if (!method_keys.insert(MemberKey(method.name, method.descriptor)).second) {
                FailBuild("duplicate intrinsic method: " + method.name + method.descriptor);
            }
        }

        std::unordered_set<std::string> field_keys;
        for (const auto& field: declaration_.fields) {
            ValidateFieldDescriptor(field.descriptor);

            if (!field_keys.insert(MemberKey(field.name, field.descriptor)).second) {
                FailBuild("duplicate intrinsic field: " + field.name + ":" + field.descriptor);
            }

            if (declaration_.is_interface && !field.is_static) {
                FailBuild("intrinsic interface has an instance field: " + field.name);
            }

            if (field.has_constant && field.descriptor != "Ljava/lang/String;") {
                ValidateIntegralConstant(field);
            }
        }

        return std::move(declaration_);
    }
} // namespace ogplay::runtime::dexvm

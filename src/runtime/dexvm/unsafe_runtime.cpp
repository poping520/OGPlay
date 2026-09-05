#include "ogplay/runtime/dexvm/unsafe_runtime.h"

#include <algorithm>
#include <bit>
#include <limits>

#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/reflection.h"

namespace ogplay::runtime::dexvm {
namespace {
constexpr std::int64_t kFieldTokenBase = 1LL << 48;
constexpr std::int32_t kArrayBase = 16;

[[noreturn]] void Invalid(const char* message) {
    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", message};
}

std::int32_t ElementScale(const std::string_view descriptor) {
    switch (descriptor.front()) {
        case 'Z': case 'B': return 1;
        case 'C': case 'S': return 2;
        case 'J': case 'D': return 8;
        default: return 4;  // A32 references and I/F.
    }
}

bool Matches(const std::string_view descriptor, const UnsafeValueKind kind) {
    if (kind == UnsafeValueKind::integer) return descriptor == "I";
    if (kind == UnsafeValueKind::long_integer) return descriptor == "J";
    return descriptor.front() == 'L' || descriptor.front() == '[';
}

std::uint64_t Bits(const VmValue& value, const UnsafeValueKind kind) {
    if (kind == UnsafeValueKind::reference) return value.ref.Value();
    return kind == UnsafeValueKind::integer ? value.cat1 : value.wide;
}
}  // namespace

struct UnsafeRuntime::Location final {
    VmObjectRef object;
    UnsafeValueKind kind;
    bool array{};
    std::int32_t index{};
    DexClassId reference_type;
};

std::int64_t UnsafeRuntime::ObjectFieldOffset(const VmObjectRef wrapper) {
    if (!wrapper.IsValid())
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "field == null"};
    const auto& meta = vm_->Reflection().FieldMetadata(wrapper);
    const auto& field = vm_->Linker().Field(meta.field);
    if (field.is_static) Invalid("valid for instance fields only");
    const auto found = std::find(fields_.begin(), fields_.end(), field.id);
    if (found != fields_.end()) return kFieldTokenBase + (found - fields_.begin());
    fields_.push_back(field.id);
    return kFieldTokenBase + static_cast<std::int64_t>(fields_.size() - 1U);
}

std::int32_t UnsafeRuntime::ArrayBaseOffset(const VmObjectRef java_class) {
    if (!java_class.IsValid())
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "class == null"};
    if (!vm_->Linker().Class(vm_->Model().ClassOfClassObject(java_class)).is_array)
        Invalid("valid for array classes only");
    return kArrayBase;
}

std::int32_t UnsafeRuntime::ArrayIndexScale(const VmObjectRef java_class) {
    static_cast<void>(ArrayBaseOffset(java_class));
    return ElementScale(vm_->Linker().Class(
        vm_->Model().ClassOfClassObject(java_class)).array_element_descriptor);
}

UnsafeRuntime::Location UnsafeRuntime::Resolve(
    const VmObjectRef object, const std::int64_t offset,
    const UnsafeValueKind kind) {
    auto& model = vm_->Model();
    auto& linker = vm_->Linker();
    if (!object.IsValid())
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "object == null"};
    const auto object_class = model.ObjectClass(object);
    const auto& java_class = linker.Class(object_class);
    Location location{object, kind, java_class.is_array, 0, DexClassId{}};
    if (java_class.is_array) {
        const auto& element = java_class.array_element_descriptor;
        if (!Matches(element, kind)) Invalid("Unsafe array element type mismatch");
        const auto scale = ElementScale(element);
        if (offset < kArrayBase || (offset - kArrayBase) % scale != 0)
            Invalid("Unsafe array offset is not an aligned element");
        const auto index = (offset - kArrayBase) / scale;
        if (index >= model.ArrayLength(object))
            throw VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;",
                              "Unsafe array offset outside array"};
        location.index = static_cast<std::int32_t>(index);
        if (kind == UnsafeValueKind::reference)
            location.reference_type = model.ObjectArrayElementClass(object);
        return location;
    }
    if (offset < kFieldTokenBase ||
        static_cast<std::uint64_t>(offset - kFieldTokenBase) >= fields_.size())
        Invalid("unknown Unsafe field offset");
    const auto& field = linker.Field(fields_[
        static_cast<std::size_t>(offset - kFieldTokenBase)]);
    if (!linker.IsAssignable(field.owner, object_class) ||
        !Matches(field.descriptor, kind))
        Invalid("Unsafe field receiver or type mismatch");
    location.index = field.slot;
    if (kind == UnsafeValueKind::reference)
        location.reference_type = linker.ResolveDescriptor(field.descriptor);
    const auto slots = model.InstanceSlots(object);
    const auto width = kind == UnsafeValueKind::long_integer ? 2U : 1U;
    if (static_cast<std::size_t>(location.index) + width > slots.size())
        Invalid("Unsafe field outside object storage");
    return location;
}

void UnsafeRuntime::CheckReference(const Location& location, const VmValue& value) {
    if (location.kind == UnsafeValueKind::reference && value.ref.IsValid() &&
        !vm_->Linker().IsAssignable(location.reference_type,
                                    vm_->Model().ObjectClass(value.ref)))
        Invalid("Unsafe reference value type mismatch");
}

VmValue UnsafeRuntime::Get(const VmObjectRef object, const std::int64_t offset,
                           const UnsafeValueKind kind) {
    const auto location = Resolve(object, offset, kind);
    auto& model = vm_->Model();
    std::uint64_t bits{};
    if (location.array) {
        if (kind == UnsafeValueKind::reference)
            return VmValue::Ref(model.GetObjectElement(object, location.index));
        bits = model.GetPrimitiveElement(object, location.index);
    } else {
        const auto slots = model.InstanceSlots(object);
        bits = slots[static_cast<std::size_t>(location.index)].bits;
        if (kind == UnsafeValueKind::long_integer)
            bits |= static_cast<std::uint64_t>(slots[static_cast<std::size_t>(location.index) + 1U].bits) << 32U;
    }
    if (kind == UnsafeValueKind::reference)
        return VmValue::Ref(VmObjectRef{static_cast<std::uint32_t>(bits)});
    if (kind == UnsafeValueKind::long_integer)
        return VmValue::Long(std::bit_cast<std::int64_t>(bits));
    return VmValue::Int(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits)));
}

void UnsafeRuntime::Put(const VmObjectRef object, const std::int64_t offset,
                        const UnsafeValueKind kind, const VmValue& value) {
    const auto location = Resolve(object, offset, kind);
    CheckReference(location, value);
    auto& model = vm_->Model();
    const auto bits = Bits(value, kind);
    if (location.array) {
        if (kind == UnsafeValueKind::reference)
            model.SetObjectElement(object, location.index, value.ref);
        else model.SetPrimitiveElement(object, location.index, bits);
        return;
    }
    auto slots = model.InstanceSlots(object);
    slots[static_cast<std::size_t>(location.index)] = {static_cast<std::uint32_t>(bits),
        kind == UnsafeValueKind::reference ? SlotTag::ref :
        kind == UnsafeValueKind::long_integer ? SlotTag::wide_lo : SlotTag::cat1};
    if (kind == UnsafeValueKind::long_integer)
        slots[static_cast<std::size_t>(location.index) + 1U] = {static_cast<std::uint32_t>(bits >> 32U),
                                    SlotTag::wide_hi};
}

bool UnsafeRuntime::CompareAndSwap(const VmObjectRef object,
    const std::int64_t offset, const UnsafeValueKind kind,
    const VmValue& expected, const VmValue& value) {
    const auto location = Resolve(object, offset, kind);
    CheckReference(location, value);
    if (Bits(Get(object, offset, kind), kind) != Bits(expected, kind)) return false;
    Put(object, offset, kind, value);
    return true;
}
}  // namespace ogplay::runtime::dexvm

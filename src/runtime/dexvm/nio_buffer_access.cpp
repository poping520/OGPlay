#include "ogplay/runtime/dexvm/nio_buffer_access.h"
#include <array>
#include <bit>
#include <limits>
#include <vector>
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/guest_memory.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace ogplay::runtime::dexvm {
namespace {
constexpr auto absent = std::numeric_limits<std::uint32_t>::max();

std::uint64_t Wide(std::span<Slot> slots, std::uint32_t slot) {
    return slots[slot].bits | (static_cast<std::uint64_t>(slots[slot + 1].bits) << 32U);
}
}  // namespace

class NioBufferAccess::Impl final {
public:
    explicit Impl(Interpreter& value) : vm(value) {}

    Interpreter& vm;

    struct Layout {
        std::uint32_t capacity, position, limit, mark, shift, address, block;
        std::uint32_t map_mode{absent}, array{absent}, offset{absent}, read_only{absent}, bytes{absent}, order{absent};
        bool buffer{}, sequence{}, direct_class{};
        NioElementKind element{NioElementKind::byte};
    };

    std::vector<std::optional<Layout>> layouts;
    DexClassId buffer_class;
    DexClassId direct_class;
    VmMethodId direct_constructor;
    std::vector<std::optional<std::uint32_t>> block_arrays;
    std::uint32_t needs_swap{absent}, block_address{absent}, block_size{absent}, big_order{absent};
    DexClassId order_class;

    Layout Get(DexClassId type) {
        if (type.Value() < layouts.size() && layouts[type.Value()]) return *layouts[type.Value()];
        auto& linker = vm.Linker();
        if (!buffer_class.IsValid()) buffer_class = linker.ResolveDescriptor("Ljava/nio/Buffer;");
        Layout l{};
        l.buffer = linker.IsAssignable(buffer_class, type);
        if (l.buffer) {
            const auto field = [&](const char* name, const char* descriptor, bool required = true) -> std::uint32_t {
                const auto id = linker.FindFieldRecursive(type, name, descriptor);
                if (!id) {
                    if (!required) return absent;
                    throw DexVmError{DexVmErrorReason::internal_invariant,
                                     std::string("ROM Buffer field missing: ") + name};
                }
                return linker.Field(*id).slot;
            };
            l.capacity = field("capacity", "I");
            l.position = field("position", "I");
            l.limit = field("limit", "I");
            l.mark = field("mark", "I");
            l.shift = field("_elementSizeShift", "I");
            l.address = field("effectiveDirectAddress", "J");
            l.block = field("block", "Ljava/nio/MemoryBlock;");
            l.read_only = field("isReadOnly", "Z", false);
            l.offset = field("arrayOffset", "I", false);
            if (l.offset == absent) l.offset = field("offset", "I", false);
            const auto& desc = linker.Class(type).descriptor;
            l.sequence = desc == "Ljava/nio/CharSequenceAdapter;";
            l.direct_class = linker.IsAssignable(linker.ResolveDescriptor("Ljava/nio/DirectByteBuffer;"), type);
            const std::array names{"Byte", "Char", "Short", "Int", "Float", "Long", "Double"};
            const std::array kinds{NioElementKind::byte,        NioElementKind::character,
                                   NioElementKind::short_value, NioElementKind::int_value,
                                   NioElementKind::float_value, NioElementKind::long_value,
                                   NioElementKind::double_value};
            const std::array arrays{"[B", "[C", "[S", "[I", "[F", "[J", "[D"};
            for (std::size_t i = 0; i < names.size(); ++i)
                if (desc.find(std::string(names[i]) + "ArrayBuffer;") != std::string::npos ||
                    desc.find(std::string("As") + names[i] + "Buffer;") != std::string::npos ||
                    desc == std::string("Ljava/nio/") + names[i] + "Buffer;") {
                    l.element = kinds[i];
                    l.array = field("backingArray", arrays[i], false);
                }
            l.bytes = field("byteBuffer", "Ljava/nio/ByteBuffer;", false);
            l.order = field("order", "Ljava/nio/ByteOrder;", false);
            l.map_mode = field("mapMode", "Ljava/nio/channels/FileChannel$MapMode;", false);
        }
        if (layouts.size() <= type.Value()) layouts.resize(type.Value() + 1U);
        layouts[type.Value()] = l;
        return l;
    }
};

NioBufferAccess::NioBufferAccess(Interpreter& vm) : impl_(std::make_unique<Impl>(vm)) {}

NioBufferAccess::~NioBufferAccess() = default;

bool NioBufferAccess::IsBuffer(VmObjectRef buffer) {
    return buffer.IsValid() && impl_->Get(impl_->vm.Model().ObjectClass(buffer)).buffer;
}

NioBufferView NioBufferAccess::Read(VmObjectRef buffer, bool with_current_address) {
    if (!buffer.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
    auto& vm = impl_->vm;
    const auto l = impl_->Get(vm.Model().ObjectClass(buffer));
    if (!l.buffer) throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a Buffer"};
    const auto slots = vm.Model().InstanceSlots(buffer);
    const auto number = [&](std::uint32_t slot) { return std::bit_cast<std::int32_t>(slots[slot].bits); };
    NioBufferView v;
    v.capacity = number(l.capacity);
    v.position = number(l.position);
    v.limit = number(l.limit);
    v.element_size_shift = number(l.shift);
    if (number(l.mark) >= 0) v.mark = number(l.mark);
    v.element = l.element;
    v.read_only = l.sequence || (l.read_only != absent && number(l.read_only) != 0);
    v.direct = l.direct_class;
    const auto address = Wide(slots, l.address);
    if (address != 0 && address <= UINT32_MAX) {
        v.direct = true;
        v.direct_address = memory::GuestAddress(static_cast<std::uint32_t>(address));
    }
    if (l.array != absent) {
        v.array = VmObjectRef(slots[l.array].bits);
        v.array_byte_offset = static_cast<std::uint32_t>(number(l.offset))
                              << static_cast<unsigned>(v.element_size_shift);
    }
    if (v.direct && l.bytes == absent) {
        const auto block = VmObjectRef(slots[l.block].bits);
        if (block.IsValid()) {
            if (impl_->block_address == absent) {
                const auto type = vm.Linker().ResolveDescriptor("Ljava/nio/MemoryBlock;");
                const auto field = vm.Linker().FindFieldRecursive(type, "address", "J");
                if (!field) throw DexVmError{DexVmErrorReason::internal_invariant, "ROM MemoryBlock.address missing"};
                impl_->block_address = vm.Linker().Field(*field).slot;
            }
            const auto block_address = Wide(vm.Model().InstanceSlots(block), impl_->block_address);
            v.scalar_accessible = block_address != 0;
            const auto type = vm.Model().ObjectClass(block);
            if (impl_->block_arrays.size() <= type.Value()) impl_->block_arrays.resize(type.Value() + 1U);
            if (!impl_->block_arrays[type.Value()]) {
                const auto array = vm.Linker().FindFieldRecursive(type, "array", "[B");
                impl_->block_arrays[type.Value()] = array ? vm.Linker().Field(*array).slot : absent;
            }
            const auto array_slot = *impl_->block_arrays[type.Value()];
            if (array_slot != absent) v.array = VmObjectRef(vm.Model().InstanceSlots(block)[array_slot].bits);
            if (l.offset != absent) v.array_byte_offset = static_cast<std::uint32_t>(number(l.offset));
            v.scalar_accessible = v.scalar_accessible && block_address + v.array_byte_offset == address;
        } else {
            v.scalar_accessible = false;
        }
    }
    if (l.bytes != absent) {
        const auto nested = Read(VmObjectRef(slots[l.bytes].bits), false);
        v.array = nested.array;
        v.array_byte_offset = nested.array_byte_offset;
        v.read_only = nested.read_only;
        v.order = nested.order;
        v.direct = nested.direct;
        v.scalar_accessible = nested.scalar_accessible && nested.direct_address == v.direct_address;
    } else if (l.order != absent) {
        const auto order = VmObjectRef(slots[l.order].bits);
        if (order.IsValid()) {
            if (impl_->needs_swap == absent) {
                const auto id = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(order), "needsSwap", "Z");
                if (!id) throw DexVmError{DexVmErrorReason::internal_invariant, "ROM ByteOrder.needsSwap missing"};
                impl_->needs_swap = vm.Linker().Field(*id).slot;
            }
            v.order = vm.Model().InstanceSlots(order)[impl_->needs_swap].bits ? NioByteOrder::big_endian
                                                                              : NioByteOrder::little_endian;
        }
    }
    if (with_current_address) {
        const auto position = static_cast<std::uint64_t>(v.position) << static_cast<unsigned>(v.element_size_shift);
        const auto base = v.direct_address ? v.direct_address
                          : v.array.IsValid()
                              ? std::optional(vm.Model().PrimitiveArrayAddress(v.array).Add(v.array_byte_offset))
                              : std::nullopt;
        if (base && position <= UINT32_MAX - base->Value()) v.current_address = base->Add(position);
    }
    return v;
}

std::span<std::byte> NioBufferAccess::Bytes(const NioBufferView& view, std::uint32_t offset, std::uint32_t count,
                                            bool write) {
    if (write && view.read_only) throw VmJavaThrow{"Ljava/nio/ReadOnlyBufferException;", "read-only buffer"};
    const auto capacity = static_cast<std::uint64_t>(view.capacity) << static_cast<unsigned>(view.element_size_shift);
    if (offset > capacity || count > capacity - offset)
        throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "buffer byte range"};
    if (count == 0) return {};
    if (view.array.IsValid()) {
        const auto bytes = impl_->vm.Model().PrimitiveArrayBytes(view.array);
        const auto start = static_cast<std::uint64_t>(view.array_byte_offset) + offset;
        if (start > bytes.size() || count > bytes.size() - start)
            throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "buffer backing array range"};
        return bytes.subspan(static_cast<std::size_t>(start), count);
    }
    if (view.direct_address)
        return {impl_->vm.GuestMemory().Resolve(view.direct_address->Add(offset), count, write), count};

    throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "buffer has no native payload"};
}

VmObjectRef NioBufferAccess::NewDirect(memory::GuestAddress address, std::int32_t capacity) {
    auto& vm = impl_->vm;
    if (!impl_->direct_constructor.IsValid()) {
        impl_->direct_class = vm.Linker().ResolveDescriptor("Ljava/nio/DirectByteBuffer;");
        const auto ctor = vm.Linker().FindDirectMethod(impl_->direct_class, "<init>", "(JI)V");
        if (!ctor) throw DexVmError{DexVmErrorReason::internal_invariant, "ROM DirectByteBuffer constructor missing"};
        impl_->direct_constructor = *ctor;
    }
    const auto allocation = vm.AllocateJniObject(impl_->direct_class);
    if (allocation.exception.IsValid())
        throw VmJavaThrow{vm.Linker().Class(allocation.exception_class).descriptor, allocation.exception_message,
                          allocation.exception};
    const auto object = allocation.value.ref;
    const auto root = vm.ProtectReferences(std::array{object});
    const auto result =
        vm.Call(impl_->direct_constructor,
                std::array{VmValue::Ref(object), VmValue::Long(address.Value()), VmValue::Int(capacity)});
    if (result.exception.IsValid())
        throw VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                          result.exception};
    return object;
}

memory::GuestAddress NioBufferAccess::DirectAddress(VmObjectRef buffer) {
    if (!buffer.IsValid()) return memory::GuestAddress{};
    const auto l = impl_->Get(impl_->vm.Model().ObjectClass(buffer));
    if (!l.buffer) return memory::GuestAddress{};
    return memory::GuestAddress(static_cast<std::uint32_t>(Wide(impl_->vm.Model().InstanceSlots(buffer), l.address)));
}

std::int64_t NioBufferAccess::Capacity(VmObjectRef buffer) {
    if (!buffer.IsValid()) return -1;
    const auto l = impl_->Get(impl_->vm.Model().ObjectClass(buffer));
    if (!l.buffer) return -1;
    return std::bit_cast<std::int32_t>(impl_->vm.Model().InstanceSlots(buffer)[l.capacity].bits);
}

bool NioBufferAccess::InitializeDirect(VmObjectRef buffer, VmObjectRef block, std::int32_t capacity,
                                       std::int32_t offset, bool read_only, VmObjectRef map_mode) {
    if (capacity < 0 || !block.IsValid()) return false;
    auto& vm = impl_->vm;
    auto& linker = vm.Linker();
    if (!impl_->order_class.IsValid()) {
        impl_->order_class = linker.ResolveDescriptor("Ljava/nio/ByteOrder;");
        impl_->big_order =
            linker.Field(*linker.FindFieldRecursive(impl_->order_class, "BIG_ENDIAN", "Ljava/nio/ByteOrder;")).slot;
        const auto memory_block = linker.ResolveDescriptor("Ljava/nio/MemoryBlock;");
        impl_->block_address = linker.Field(*linker.FindFieldRecursive(memory_block, "address", "J")).slot;
        impl_->block_size = linker.Field(*linker.FindFieldRecursive(memory_block, "size", "J")).slot;
    }
    const auto& order = linker.Class(impl_->order_class);
    if (order.clinit_state != ClinitState::initialized) return false;
    const auto block_slots = vm.Model().InstanceSlots(block);
    const auto size = std::bit_cast<std::int64_t>(Wide(block_slots, impl_->block_size));
    const auto sum =
        std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(capacity) + static_cast<std::uint32_t>(offset));
    if (size >= 0 && sum > size) return false;
    const auto address =
        Wide(block_slots, impl_->block_address) + static_cast<std::uint64_t>(static_cast<std::int64_t>(offset));
    const auto l = impl_->Get(vm.Model().ObjectClass(buffer));
    InitializeBase(buffer, 0, capacity, block);
    const auto slots = vm.Model().InstanceSlots(buffer);
    slots[l.order] = {order.static_storage[impl_->big_order], SlotTag::ref};
    slots[l.map_mode] = {map_mode.Value(), SlotTag::ref};
    slots[l.offset] = {static_cast<std::uint32_t>(offset), SlotTag::cat1};
    slots[l.read_only] = {read_only ? 1U : 0U, SlotTag::cat1};
    slots[l.address] = {static_cast<std::uint32_t>(address), SlotTag::wide_lo};
    slots[l.address + 1] = {static_cast<std::uint32_t>(address >> 32U), SlotTag::wide_hi};
    return true;
}

void NioBufferAccess::InitializeBase(VmObjectRef buffer, std::int32_t shift, std::int32_t capacity, VmObjectRef block) {
    const auto l = impl_->Get(impl_->vm.Model().ObjectClass(buffer));
    const auto slots = impl_->vm.Model().InstanceSlots(buffer);
    slots[l.shift] = {static_cast<std::uint32_t>(shift), SlotTag::cat1};
    slots[l.capacity] = {static_cast<std::uint32_t>(capacity), SlotTag::cat1};
    slots[l.limit] = {static_cast<std::uint32_t>(capacity), SlotTag::cat1};
    slots[l.position] = {0, SlotTag::cat1};
    slots[l.mark] = {UINT32_MAX, SlotTag::cat1};
    slots[l.block] = {block.Value(), SlotTag::ref};
}

void NioBufferAccess::StorePosition(VmObjectRef buffer, std::int32_t position) {
    const auto l = impl_->Get(impl_->vm.Model().ObjectClass(buffer));
    impl_->vm.Model().InstanceSlots(buffer)[l.position] = {static_cast<std::uint32_t>(position), SlotTag::cat1};
}

void NioBufferAccess::StoreCursor(VmObjectRef buffer, std::int32_t position, std::int32_t limit, std::int32_t mark) {
    const auto l = impl_->Get(impl_->vm.Model().ObjectClass(buffer));
    const auto slots = impl_->vm.Model().InstanceSlots(buffer);
    slots[l.position] = {static_cast<std::uint32_t>(position), SlotTag::cat1};
    slots[l.limit] = {static_cast<std::uint32_t>(limit), SlotTag::cat1};
    slots[l.mark] = {static_cast<std::uint32_t>(mark), SlotTag::cat1};
}

std::uint32_t NioBufferAccess::ElementSize(NioElementKind kind) {
    switch (kind) {
    case NioElementKind::byte: return 1;
    case NioElementKind::character:
    case NioElementKind::short_value: return 2;
    case NioElementKind::int_value:
    case NioElementKind::float_value: return 4;
    case NioElementKind::long_value:
    case NioElementKind::double_value: return 8;
    }
    return 1;
}
}  // namespace ogplay::runtime::dexvm

#include "ogplay/runtime/dexvm/nio_runtime.h"

#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace ogplay::runtime::dexvm {
namespace {

struct IdentityHash final {
    [[nodiscard]] std::size_t operator()(const JniObjectIdentity id) const noexcept {
        const auto domain = id.domain == JniObjectDomain::dex_vm
                                ? UINT64_C(0x9e3779b97f4a7c15)
                                : UINT64_C(0);
        return std::hash<std::uint64_t>{}(id.value ^ domain);
    }
};

[[noreturn]] void Throw(std::string_view descriptor, std::string_view message) {
    throw VmJavaThrow{std::string(descriptor), std::string(message)};
}

struct Storage final {
    std::vector<std::byte> heap;
    VmObjectRef array;
    NioElementKind array_element{NioElementKind::byte};
    std::int32_t array_offset{};
    std::optional<memory::GuestAddress> address;
    std::uint32_t byte_capacity{};
    bool owned{};
    NioDirectMemoryAccess* access{};

    ~Storage() {
        if (owned && address.has_value() && access != nullptr && access->release) {
            try {
                access->release(*address, byte_capacity);
            } catch (const std::exception&) {
            }
        }
    }
};

struct BufferState final {
    std::shared_ptr<Storage> storage;
    std::int32_t capacity{};
    std::int32_t position{};
    std::int32_t limit{};
    std::optional<std::int32_t> mark;
    std::uint32_t byte_offset{};
    std::int32_t array_offset{};
    NioElementKind element{NioElementKind::byte};
    NioByteOrder order{NioByteOrder::big_endian};
    bool read_only{};
};

[[nodiscard]] std::uint64_t Decode(std::span<const std::byte> bytes,
                                   const NioByteOrder order) {
    std::uint64_t result{};
    if (order == NioByteOrder::big_endian) {
        for (const auto value : bytes) {
            result = (result << 8U) | std::to_integer<std::uint8_t>(value);
        }
    } else {
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            result |= static_cast<std::uint64_t>(
                          std::to_integer<std::uint8_t>(bytes[index]))
                      << (index * 8U);
        }
    }
    return result;
}

void Encode(const std::uint64_t bits, std::span<std::byte> bytes,
            const NioByteOrder order) {
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto shift = order == NioByteOrder::big_endian
                               ? (bytes.size() - 1U - index) * 8U
                               : index * 8U;
        bytes[index] = static_cast<std::byte>((bits >> shift) & 0xffU);
    }
}

}  // namespace

class NioRuntime::Impl final {
public:
    NioDirectMemoryAccess direct;
    JavaObjectModel* model{};
    std::unordered_map<JniObjectIdentity, BufferState, IdentityHash> buffers;

    [[nodiscard]] BufferState& State(const JniObjectIdentity owner) {
        const auto found = buffers.find(owner);
        if (found == buffers.end()) {
            Throw("Ljava/lang/IllegalStateException;", "buffer has no NIO state");
        }
        return found->second;
    }
    [[nodiscard]] const BufferState& State(const JniObjectIdentity owner) const {
        const auto found = buffers.find(owner);
        if (found == buffers.end()) {
            Throw("Ljava/lang/IllegalStateException;", "buffer has no NIO state");
        }
        return found->second;
    }

    void Read(const BufferState& state, const std::uint32_t offset,
              const std::span<std::byte> destination) const {
        if (offset > state.storage->byte_capacity || destination.size() >
                state.storage->byte_capacity - offset) {
            Throw("Ljava/lang/IndexOutOfBoundsException;", "buffer byte range");
        }
        if (state.storage->address.has_value()) {
            if (!direct.read) Throw("Ljava/lang/UnsupportedOperationException;",
                                    "direct buffer memory is unavailable");
            direct.read(state.storage->address->Add(offset), destination);
        } else if (state.storage->array.IsValid() && model != nullptr) {
            const auto array_size = NioRuntime::ElementSize(
                state.storage->array_element);
            for (std::size_t index = 0; index < destination.size(); ++index) {
                const auto absolute = offset + static_cast<std::uint32_t>(index);
                const auto element_index = state.storage->array_offset +
                    static_cast<std::int32_t>(absolute / array_size);
                const auto shift = (absolute % array_size) * 8U;
                destination[index] = static_cast<std::byte>(
                    (model->GetPrimitiveElement(state.storage->array,
                                                element_index) >> shift) & 0xffU);
            }
        } else {
            std::copy_n(state.storage->heap.begin() + offset,
                        destination.size(), destination.begin());
        }
    }

    void Write(BufferState& state, const std::uint32_t offset,
               const std::span<const std::byte> source) {
        if (state.read_only) {
            Throw("Ljava/nio/ReadOnlyBufferException;", "read-only buffer");
        }
        if (offset > state.storage->byte_capacity || source.size() >
                state.storage->byte_capacity - offset) {
            Throw("Ljava/lang/IndexOutOfBoundsException;", "buffer byte range");
        }
        if (state.storage->address.has_value()) {
            if (!direct.write) Throw("Ljava/lang/UnsupportedOperationException;",
                                     "direct buffer memory is unavailable");
            direct.write(state.storage->address->Add(offset), source);
        } else if (state.storage->array.IsValid() && model != nullptr) {
            const auto array_size = NioRuntime::ElementSize(
                state.storage->array_element);
            for (std::size_t index = 0; index < source.size(); ++index) {
                const auto absolute = offset + static_cast<std::uint32_t>(index);
                const auto element_index = state.storage->array_offset +
                    static_cast<std::int32_t>(absolute / array_size);
                const auto shift = (absolute % array_size) * 8U;
                auto bits = model->GetPrimitiveElement(state.storage->array,
                                                       element_index);
                bits &= ~(UINT64_C(0xff) << shift);
                bits |= static_cast<std::uint64_t>(
                            std::to_integer<std::uint8_t>(source[index])) << shift;
                model->SetPrimitiveElement(state.storage->array, element_index, bits);
            }
        } else {
            std::copy(source.begin(), source.end(),
                      state.storage->heap.begin() + offset);
        }
    }
};

NioRuntime::NioRuntime() : impl_(std::make_unique<Impl>()) {}
NioRuntime::~NioRuntime() = default;

std::uint32_t NioRuntime::ElementSize(const NioElementKind kind) {
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

void NioRuntime::SetDirectMemoryAccess(NioDirectMemoryAccess access) {
    impl_->direct = std::move(access);
}

void NioRuntime::SetObjectModel(JavaObjectModel* const model) noexcept {
    impl_->model = model;
}

void NioRuntime::CreateHeap(const JniObjectIdentity owner, const VmObjectRef array,
                            const std::int32_t array_offset,
                            const std::int32_t capacity,
                            const NioElementKind element) {
    if (capacity < 0 || array_offset < 0) {
        Throw("Ljava/lang/IllegalArgumentException;", "negative buffer range");
    }
    const auto bytes = static_cast<std::uint64_t>(capacity) * ElementSize(element);
    if (bytes > std::numeric_limits<std::uint32_t>::max()) {
        Throw("Ljava/lang/OutOfMemoryError;", "buffer is too large");
    }
    auto storage = std::make_shared<Storage>();
    storage->heap.resize(static_cast<std::size_t>(bytes));
    storage->byte_capacity = static_cast<std::uint32_t>(bytes);
    storage->array = array;
    storage->array_element = element;
    storage->array_offset = array_offset;
    const auto order = element == NioElementKind::byte ||
                               std::endian::native == std::endian::big
                           ? NioByteOrder::big_endian
                           : NioByteOrder::little_endian;
    impl_->buffers[owner] = {std::move(storage), capacity, 0, capacity,
                                  {}, 0, array_offset, element, order};
}

void NioRuntime::CreateDirect(const JniObjectIdentity owner,
                              const std::int32_t capacity) {
    if (capacity < 0) Throw("Ljava/lang/IllegalArgumentException;", "negative capacity");
    if (!impl_->direct.allocate || !impl_->direct.release || !impl_->direct.read ||
        !impl_->direct.write) {
        Throw("Ljava/lang/UnsupportedOperationException;",
              "direct buffer allocator is unavailable");
    }
    auto storage = std::make_shared<Storage>();
    try {
        storage->address =
            impl_->direct.allocate(static_cast<std::uint32_t>(capacity));
    } catch (const std::bad_alloc&) {
        Throw("Ljava/lang/OutOfMemoryError;", "direct buffer arena is exhausted");
    }
    storage->byte_capacity = static_cast<std::uint32_t>(capacity);
    storage->owned = true;
    storage->access = &impl_->direct;
    impl_->buffers[owner] = {std::move(storage), capacity, 0, capacity};
}

void NioRuntime::WrapDirect(const JniObjectIdentity owner,
                            const memory::GuestAddress address,
                            const std::int32_t capacity) {
    if (capacity < 0 || (capacity != 0 && address.IsNull()) ||
        !impl_->direct.validate ||
        !impl_->direct.validate(address, static_cast<std::uint32_t>(capacity))) {
        Throw("Ljava/lang/IllegalArgumentException;", "invalid direct buffer range");
    }
    auto storage = std::make_shared<Storage>();
    storage->address = address;
    storage->byte_capacity = static_cast<std::uint32_t>(capacity);
    storage->access = &impl_->direct;
    impl_->buffers[owner] = {std::move(storage), capacity, 0, capacity};
}

void NioRuntime::CreateView(const JniObjectIdentity owner,
                            const JniObjectIdentity source,
                            const NioElementKind element,
                            const std::int32_t byte_offset,
                            const std::int32_t capacity,
                            const bool read_only) {
    const auto& parent = impl_->State(source);
    if (byte_offset < 0 || capacity < 0 ||
        static_cast<std::uint64_t>(byte_offset) +
                static_cast<std::uint64_t>(capacity) * ElementSize(element) >
            static_cast<std::uint64_t>(parent.capacity) * ElementSize(parent.element)) {
        Throw("Ljava/lang/IndexOutOfBoundsException;", "invalid buffer view");
    }
    impl_->buffers[owner] = {
        parent.storage, capacity, 0, capacity, {},
        parent.byte_offset + static_cast<std::uint32_t>(byte_offset),
        parent.storage->array_offset + static_cast<std::int32_t>(
            (parent.byte_offset + static_cast<std::uint32_t>(byte_offset)) /
            ElementSize(parent.storage->array_element)),
        element, parent.order, parent.read_only || read_only};
}

void NioRuntime::Duplicate(const JniObjectIdentity owner,
                           const JniObjectIdentity source,
                           const bool read_only) {
    auto copy = impl_->State(source);
    copy.read_only = copy.read_only || read_only;
    impl_->buffers[owner] = std::move(copy);
}

NioBufferSnapshot NioRuntime::Snapshot(const JniObjectIdentity owner) const {
    const auto& state = impl_->State(owner);
    std::optional<memory::GuestAddress> address;
    if (state.storage->address.has_value()) {
        address = state.storage->address->Add(state.byte_offset);
    }
    const auto visible_array = state.storage->array_element == state.element
                                   ? state.storage->array
                                   : VmObjectRef(0);
    return {state.capacity, state.position, state.limit, state.mark,
            state.array_offset, state.element, state.order, state.read_only,
            state.storage->address.has_value(), visible_array, address};
}

bool NioRuntime::Contains(const JniObjectIdentity owner) const noexcept {
    return impl_->buffers.contains(owner);
}

void NioRuntime::SetPosition(const JniObjectIdentity owner,
                             const std::int32_t position) {
    auto& state = impl_->State(owner);
    if (position < 0 || position > state.limit) {
        Throw("Ljava/lang/IllegalArgumentException;", "position outside limit");
    }
    state.position = position;
    if (state.mark.has_value() && *state.mark > position) state.mark.reset();
}

void NioRuntime::SetLimit(const JniObjectIdentity owner, const std::int32_t limit) {
    auto& state = impl_->State(owner);
    if (limit < 0 || limit > state.capacity) {
        Throw("Ljava/lang/IllegalArgumentException;", "limit outside capacity");
    }
    state.limit = limit;
    state.position = std::min(state.position, limit);
    if (state.mark.has_value() && *state.mark > limit) state.mark.reset();
}

void NioRuntime::Mark(const JniObjectIdentity owner) {
    auto& state = impl_->State(owner);
    state.mark = state.position;
}
void NioRuntime::Reset(const JniObjectIdentity owner) {
    auto& state = impl_->State(owner);
    if (!state.mark.has_value()) {
        Throw("Ljava/nio/InvalidMarkException;", "mark is not set");
    }
    state.position = *state.mark;
}
void NioRuntime::Clear(const JniObjectIdentity owner) {
    auto& state = impl_->State(owner);
    state.position = 0; state.limit = state.capacity; state.mark.reset();
}
void NioRuntime::Flip(const JniObjectIdentity owner) {
    auto& state = impl_->State(owner);
    state.limit = state.position; state.position = 0; state.mark.reset();
}
void NioRuntime::Rewind(const JniObjectIdentity owner) {
    auto& state = impl_->State(owner);
    state.position = 0; state.mark.reset();
}
void NioRuntime::SetOrder(const JniObjectIdentity owner, const NioByteOrder order) {
    impl_->State(owner).order = order;
}

std::uint64_t NioRuntime::Get(const JniObjectIdentity owner,
                              const std::optional<std::int32_t> index) {
    auto& state = impl_->State(owner);
    const auto at = index.value_or(state.position);
    if (at < 0 || at >= (index.has_value() ? state.limit : state.limit)) {
        Throw(index.has_value() ? "Ljava/lang/IndexOutOfBoundsException;"
                                : "Ljava/nio/BufferUnderflowException;",
              "buffer get outside limit");
    }
    const auto size = ElementSize(state.element);
    std::byte bytes[8]{};
    impl_->Read(state, state.byte_offset + static_cast<std::uint32_t>(at) * size,
                std::span<std::byte>(bytes, size));
    if (!index.has_value()) ++state.position;
    return Decode(std::span<const std::byte>(bytes, size), state.order);
}

void NioRuntime::Put(const JniObjectIdentity owner,
                     const std::optional<std::int32_t> index,
                     const std::uint64_t bits) {
    auto& state = impl_->State(owner);
    const auto at = index.value_or(state.position);
    if (at < 0 || at >= state.limit) {
        Throw(index.has_value() ? "Ljava/lang/IndexOutOfBoundsException;"
                                : "Ljava/nio/BufferOverflowException;",
              "buffer put outside limit");
    }
    const auto size = ElementSize(state.element);
    std::byte bytes[8]{};
    Encode(bits, std::span<std::byte>(bytes, size), state.order);
    impl_->Write(state, state.byte_offset + static_cast<std::uint32_t>(at) * size,
                 std::span<const std::byte>(bytes, size));
    if (!index.has_value()) ++state.position;
}

std::uint64_t NioRuntime::GetScalar(
    const JniObjectIdentity owner, const NioElementKind kind,
    const std::optional<std::int32_t> byte_index) {
    auto& state = impl_->State(owner);
    const auto size = ElementSize(kind);
    const auto at = byte_index.value_or(state.position);
    if (at < 0 || static_cast<std::uint64_t>(at) + size >
                      static_cast<std::uint64_t>(state.limit)) {
        Throw(byte_index.has_value() ? "Ljava/lang/IndexOutOfBoundsException;"
                                     : "Ljava/nio/BufferUnderflowException;",
              "scalar get outside limit");
    }
    std::byte bytes[8]{};
    impl_->Read(state, state.byte_offset + static_cast<std::uint32_t>(at),
                std::span<std::byte>(bytes, size));
    if (!byte_index.has_value()) state.position += static_cast<std::int32_t>(size);
    return Decode(std::span<const std::byte>(bytes, size), state.order);
}

void NioRuntime::PutScalar(const JniObjectIdentity owner,
                           const NioElementKind kind,
                           const std::optional<std::int32_t> byte_index,
                           const std::uint64_t bits) {
    auto& state = impl_->State(owner);
    const auto size = ElementSize(kind);
    const auto at = byte_index.value_or(state.position);
    if (at < 0 || static_cast<std::uint64_t>(at) + size >
                      static_cast<std::uint64_t>(state.limit)) {
        Throw(byte_index.has_value() ? "Ljava/lang/IndexOutOfBoundsException;"
                                     : "Ljava/nio/BufferOverflowException;",
              "scalar put outside limit");
    }
    std::byte bytes[8]{};
    Encode(bits, std::span<std::byte>(bytes, size), state.order);
    impl_->Write(state, state.byte_offset + static_cast<std::uint32_t>(at),
                 std::span<const std::byte>(bytes, size));
    if (!byte_index.has_value()) state.position += static_cast<std::int32_t>(size);
}

void NioRuntime::Copy(const JniObjectIdentity destination,
                      const JniObjectIdentity source) {
    auto& target = impl_->State(destination);
    auto& input = impl_->State(source);
    if (target.read_only) {
        Throw("Ljava/nio/ReadOnlyBufferException;", "read-only buffer");
    }
    if (destination == source) {
        Throw("Ljava/lang/IllegalArgumentException;", "source is destination");
    }
    const auto count = input.limit - input.position;
    if (count > target.limit - target.position) {
        Throw("Ljava/nio/BufferOverflowException;", "destination has insufficient remaining");
    }
    std::vector<std::uint64_t> values;
    values.reserve(static_cast<std::size_t>(count));
    while (input.position < input.limit) values.push_back(Get(source, {}));
    for (const auto value : values) Put(destination, {}, value);
}

void NioRuntime::Compact(const JniObjectIdentity owner) {
    auto& state = impl_->State(owner);
    if (state.read_only) Throw("Ljava/nio/ReadOnlyBufferException;", "read-only buffer");
    const auto size = ElementSize(state.element);
    const auto count = state.limit - state.position;
    std::vector<std::byte> bytes(static_cast<std::size_t>(count) * size);
    impl_->Read(state, state.byte_offset + static_cast<std::uint32_t>(state.position) * size,
                bytes);
    impl_->Write(state, state.byte_offset, bytes);
    state.position = count; state.limit = state.capacity; state.mark.reset();
}

std::vector<std::byte> NioRuntime::ReadRemainingBytes(
    const JniObjectIdentity owner) const {
    const auto& state = impl_->State(owner);
    const auto size = ElementSize(state.element);
    std::vector<std::byte> bytes(
        static_cast<std::size_t>(state.limit - state.position) * size);
    impl_->Read(state,
                state.byte_offset + static_cast<std::uint32_t>(state.position) * size,
                bytes);
    return bytes;
}

std::uint32_t NioRuntime::WithTemporaryGuestMemory(
    const std::span<const std::byte> bytes, const bool copy_back,
    const GuestMemoryOperation& operation,
    std::vector<std::byte>* const copied_back) {
    if (!operation) {
        throw std::invalid_argument("NIO guest memory operation is absent");
    }
    if (bytes.empty()) {
        if (copied_back != nullptr) copied_back->clear();
        return operation(memory::GuestAddress{});
    }
    if (!impl_->direct.allocate || !impl_->direct.release ||
        !impl_->direct.write || (copy_back && !impl_->direct.read)) {
        Throw("Ljava/lang/UnsupportedOperationException;",
              "temporary guest memory is unavailable");
    }
    const auto size = static_cast<std::uint32_t>(bytes.size());
    const auto address = impl_->direct.allocate(size);
    struct Release final {
        NioDirectMemoryAccess* access;
        memory::GuestAddress address;
        std::uint32_t size;
        ~Release() {
            try {
                access->release(address, size);
            } catch (const std::exception&) {
            }
        }
    } release{&impl_->direct, address, size};
    impl_->direct.write(address, bytes);
    const auto result = operation(address);
    if (copy_back) {
        std::vector<std::byte> output(bytes.size());
        impl_->direct.read(address, output);
        if (copied_back != nullptr) *copied_back = std::move(output);
    }
    return result;
}

std::uint32_t NioRuntime::WithBufferGuestMemory(
    const JniObjectIdentity owner, const bool copy_back,
    const GuestMemoryOperation& operation) {
    auto& state = impl_->State(owner);
    if (copy_back && state.read_only) {
        Throw("Ljava/nio/ReadOnlyBufferException;", "read-only buffer");
    }
    const auto element_size = ElementSize(state.element);
    const auto byte_offset = state.byte_offset +
        static_cast<std::uint32_t>(state.position) * element_size;
    if (state.storage->address.has_value()) {
        return operation(state.storage->address->Add(byte_offset));
    }
    const auto input = ReadRemainingBytes(owner);
    std::vector<std::byte> output;
    const auto result = WithTemporaryGuestMemory(input, copy_back, operation,
                                                 &output);
    if (copy_back && !output.empty()) {
        impl_->Write(state, byte_offset, output);
    }
    return result;
}

void NioRuntime::Trace(const JniObjectIdentity owner,
                       const std::function<void(VmObjectRef)>& visit) const {
    const auto found = impl_->buffers.find(owner);
    if (found != impl_->buffers.end() && found->second.storage->array.IsValid()) {
        visit(found->second.storage->array);
    }
}
void NioRuntime::Sweep(const JniObjectIdentity owner) {
    impl_->buffers.erase(owner);
}

void NioRuntime::SweepDomain(const JniObjectDomain domain) {
    std::erase_if(impl_->buffers, [domain](const auto& entry) {
        return entry.first.domain == domain;
    });
}

}  // namespace ogplay::runtime::dexvm

#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include "ogplay/memory/address.h"
#include "ogplay/runtime/dexvm/dexvm_types.h"

namespace ogplay::runtime::dexvm {
class Interpreter;
enum class NioElementKind : std::uint8_t {
    byte,
    character,
    short_value,
    int_value,
    float_value,
    long_value,
    double_value
};
enum class NioByteOrder : std::uint8_t { big_endian, little_endian };

struct NioBufferView final {
    std::int32_t capacity{}, position{}, limit{}, element_size_shift{};
    std::optional<std::int32_t> mark;
    NioElementKind element{NioElementKind::byte};
    NioByteOrder order{NioByteOrder::little_endian};
    bool read_only{}, direct{};
    bool scalar_accessible{true};
    VmObjectRef array;
    std::uint32_t array_byte_offset{};
    std::optional<memory::GuestAddress> direct_address;
    std::optional<memory::GuestAddress> current_address;
};

// Caches linked field slots only. All Buffer state and ownership remain in ROM objects.
class NioBufferAccess final {
public:
    explicit NioBufferAccess(Interpreter& vm);
    ~NioBufferAccess();
    [[nodiscard]] bool IsBuffer(VmObjectRef buffer);
    [[nodiscard]] NioBufferView Read(VmObjectRef buffer, bool with_current_address = true);
    [[nodiscard]] std::span<std::byte> Bytes(const NioBufferView& view, std::uint32_t byte_offset,
                                             std::uint32_t byte_count, bool write);
    [[nodiscard]] VmObjectRef NewDirect(memory::GuestAddress address, std::int32_t capacity);
    [[nodiscard]] memory::GuestAddress DirectAddress(VmObjectRef buffer);
    [[nodiscard]] std::int64_t Capacity(VmObjectRef buffer);
    // Cursor mutations by the threaded successful-call fast path only.
    [[nodiscard]] bool InitializeDirect(VmObjectRef buffer, VmObjectRef block, std::int32_t capacity,
                                        std::int32_t offset, bool read_only, VmObjectRef map_mode);
    void InitializeBase(VmObjectRef buffer, std::int32_t shift, std::int32_t capacity, VmObjectRef block);
    void StorePosition(VmObjectRef buffer, std::int32_t position);
    void StoreCursor(VmObjectRef buffer, std::int32_t position, std::int32_t limit, std::int32_t mark);
    [[nodiscard]] static std::uint32_t ElementSize(NioElementKind kind);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace ogplay::runtime::dexvm

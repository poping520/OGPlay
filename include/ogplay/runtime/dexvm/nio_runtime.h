#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ogplay/memory/address.h"
#include "ogplay/runtime/dexvm/dexvm_types.h"
#include "ogplay/runtime/jni/jni.h"

namespace ogplay::runtime::dexvm {

class JavaObjectModel;

enum class NioElementKind : std::uint8_t {
    byte, character, short_value, int_value, float_value, long_value,
    double_value,
};

enum class NioByteOrder : std::uint8_t { big_endian, little_endian };

struct NioDirectMemoryAccess final {
    std::function<memory::GuestAddress(std::uint32_t)> allocate;
    std::function<void(memory::GuestAddress, std::uint32_t)> release;
    std::function<bool(memory::GuestAddress, std::uint32_t)> validate;
    std::function<void(memory::GuestAddress, std::span<std::byte>)> read;
    std::function<void(memory::GuestAddress, std::span<const std::byte>)> write;
};

struct NioBufferSnapshot final {
    std::int32_t capacity{};
    std::int32_t position{};
    std::int32_t limit{};
    std::optional<std::int32_t> mark;
    std::int32_t array_offset{};
    NioElementKind element{NioElementKind::byte};
    NioByteOrder order{NioByteOrder::big_endian};
    bool read_only{};
    bool direct{};
    VmObjectRef array;
    std::optional<memory::GuestAddress> direct_address;
};

class NioRuntime final {
public:
    using GuestMemoryOperation =
        std::function<std::uint32_t(memory::GuestAddress)>;
    NioRuntime();
    ~NioRuntime();
    NioRuntime(const NioRuntime&) = delete;
    NioRuntime& operator=(const NioRuntime&) = delete;

    void SetDirectMemoryAccess(NioDirectMemoryAccess access);
    void SetObjectModel(JavaObjectModel* model) noexcept;
    void CreateHeap(JniObjectIdentity owner, VmObjectRef array,
                    std::int32_t array_offset, std::int32_t capacity,
                    NioElementKind element);
    void CreateDirect(JniObjectIdentity owner, std::int32_t capacity);
    void WrapDirect(JniObjectIdentity owner, memory::GuestAddress address,
                    std::int32_t capacity);
    void CreateView(JniObjectIdentity owner, JniObjectIdentity source,
                    NioElementKind element, std::int32_t byte_offset,
                    std::int32_t capacity, bool read_only);
    void Duplicate(JniObjectIdentity owner, JniObjectIdentity source,
                   bool read_only);

    [[nodiscard]] NioBufferSnapshot Snapshot(JniObjectIdentity owner) const;
    [[nodiscard]] bool Contains(JniObjectIdentity owner) const noexcept;
    void SetPosition(JniObjectIdentity owner, std::int32_t position);
    void SetLimit(JniObjectIdentity owner, std::int32_t limit);
    void Mark(JniObjectIdentity owner);
    void Reset(JniObjectIdentity owner);
    void Clear(JniObjectIdentity owner);
    void Flip(JniObjectIdentity owner);
    void Rewind(JniObjectIdentity owner);
    void SetOrder(JniObjectIdentity owner, NioByteOrder order);

    [[nodiscard]] std::uint64_t Get(JniObjectIdentity owner,
                                    std::optional<std::int32_t> index);
    void Put(JniObjectIdentity owner, std::optional<std::int32_t> index,
             std::uint64_t bits);
    [[nodiscard]] std::uint64_t GetScalar(
        JniObjectIdentity owner, NioElementKind kind,
        std::optional<std::int32_t> byte_index);
    void PutScalar(JniObjectIdentity owner, NioElementKind kind,
                   std::optional<std::int32_t> byte_index, std::uint64_t bits);
    void Copy(JniObjectIdentity destination, JniObjectIdentity source);
    void Compact(JniObjectIdentity owner);
    [[nodiscard]] std::vector<std::byte> ReadRemainingBytes(
        JniObjectIdentity owner) const;
    [[nodiscard]] std::uint32_t WithBufferGuestMemory(
        JniObjectIdentity owner, bool copy_back,
        const GuestMemoryOperation& operation);
    [[nodiscard]] std::uint32_t WithTemporaryGuestMemory(
        std::span<const std::byte> bytes, bool copy_back,
        const GuestMemoryOperation& operation,
        std::vector<std::byte>* copied_back = nullptr);

    void Trace(JniObjectIdentity owner,
               const std::function<void(VmObjectRef)>& visit) const;
    void Sweep(JniObjectIdentity owner);
    void SweepDomain(JniObjectDomain domain);

    [[nodiscard]] static std::uint32_t ElementSize(NioElementKind kind);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime::dexvm

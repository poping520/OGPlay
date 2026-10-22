#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits>

#include "ogplay/memory/address.h"

namespace ogplay::runtime {

class JavaPayloadProvider;

namespace payload_detail {
struct Block final {
    std::atomic<bool> alive{true};
    std::atomic<std::size_t> accessors{};
    std::weak_ptr<void> owner_lifetime;
    std::byte* base{};
    memory::GuestAddress address;
    std::size_t bytes{};
    std::uint64_t generation{};
    const void* owner{};
};
}  // namespace payload_detail

class JavaPayloadHandle final {
public:
    [[nodiscard]] memory::GuestAddress Address() const { return Require().address; }

    [[nodiscard]] std::size_t Bytes() const { return Require().bytes; }

    [[nodiscard]] std::uint64_t Generation() const { return Require().generation; }

private:
    [[nodiscard]] const payload_detail::Block& Require() const {
        if (!block_ || !block_->alive.load(std::memory_order_relaxed))
            throw std::invalid_argument("retired Java payload handle");
        return *block_;
    }

    std::shared_ptr<payload_detail::Block> block_;
    friend class JavaPayloadProvider;
};

// No public host pointer/span. Release/teardown must be exclusive with access;
// guest/host publication follows the ordinary guest memory happens-before rules.
template <typename T>
class JavaPayloadAccessor final {
    static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool> && sizeof(T) <= 8);
    static_assert(std::endian::native == std::endian::little);

public:
    JavaPayloadAccessor(const JavaPayloadAccessor&) = default;
    JavaPayloadAccessor& operator=(const JavaPayloadAccessor&) = default;

    [[nodiscard]] std::size_t Size() const noexcept { return count_; }

    [[nodiscard]] T Read(std::size_t index) const {
        Check(index);
        T value;
        std::memcpy(&value, base_ + index * sizeof(T), sizeof(T));
        return value;
    }

    void Write(std::size_t index, T value) const {
        Check(index);
        std::memcpy(base_ + index * sizeof(T), &value, sizeof(T));
    }

private:
    struct Lifetime final {
        explicit Lifetime(std::shared_ptr<payload_detail::Block> value)
            : owner(value->owner_lifetime.lock()), block(std::move(value)) {
            if (!owner) throw std::invalid_argument("retired payload provider");
            block->accessors.fetch_add(1, std::memory_order_relaxed);
        }

        ~Lifetime() { block->accessors.fetch_sub(1, std::memory_order_relaxed); }

        std::shared_ptr<void> owner;
        std::shared_ptr<payload_detail::Block> block;
    };

    JavaPayloadAccessor(std::shared_ptr<payload_detail::Block> block, std::size_t offset, std::size_t count)
        : lifetime_(std::make_shared<Lifetime>(block)), base_(block->base + offset), count_(count) {}

    void Check(std::size_t index) const {
        if (index >= count_) throw std::out_of_range("Java payload element exceeds block view");
    }

    // Owning view prevents release/reuse; no atomic lifetime probe per element.
    std::shared_ptr<Lifetime> lifetime_;
    std::byte* base_{};
    std::size_t count_{};
    friend class JavaPayloadProvider;
};

class JavaPayloadBorrow final {
private:
    JavaPayloadHandle payload_;
    std::uint64_t id_{};
    friend class JavaPayloadProvider;
};

struct JavaPayloadStatistics final {
    std::uint64_t allocated_bytes{}, committed_bytes{}, slab_free_bytes{};
    std::uint64_t pinned_bytes{}, pin_count{}, allocation_count{}, heap_target_bytes{};
};

// Interface only: no AddressSpace implementation, VM or platform dependencies.
// Integration owns the provider; all dependencies/charge context are explicit.
class JavaPayloadProvider {
public:
    virtual ~JavaPayloadProvider() = default;
    [[nodiscard]] virtual JavaPayloadHandle Allocate(std::size_t bytes) = 0;
    virtual void Release(const JavaPayloadHandle& handle) = 0;
    [[nodiscard]] virtual JavaPayloadBorrow Pin(const JavaPayloadHandle& handle) = 0;
    virtual void Unpin(const JavaPayloadBorrow& borrow) = 0;
    [[nodiscard]] virtual JavaPayloadStatistics Statistics() const = 0;

    template <typename T>
    [[nodiscard]] JavaPayloadAccessor<T> Access(const JavaPayloadHandle& handle) const {
        return Access<T>(handle, 0, handle.Bytes() / sizeof(T));
    }

    template <typename T>
    [[nodiscard]] JavaPayloadAccessor<T> Access(const JavaPayloadHandle& handle, std::size_t offset,
                                                std::size_t count) const {
        ValidateOwner(handle);
        const auto& b = handle.Require();
        if (offset % alignof(T) != 0 || offset > b.bytes || count > (b.bytes - offset) / sizeof(T))
            throw std::out_of_range("Java payload typed view exceeds owned block");
        return JavaPayloadAccessor<T>{handle.block_, offset, count};
    }

protected:
    [[nodiscard]] virtual const void* OwnerIdentity() const noexcept = 0;

    void ValidateOwner(const JavaPayloadHandle& h) const {
        if (h.Require().owner != OwnerIdentity()) throw std::invalid_argument("foreign Java payload owner");
    }

    [[nodiscard]] JavaPayloadHandle MakeHandle(std::shared_ptr<payload_detail::Block> block) const {
        JavaPayloadHandle result;
        result.block_ = std::move(block);
        return result;
    }

    [[nodiscard]] const std::shared_ptr<payload_detail::Block>& Block(const JavaPayloadHandle& h) const {
        ValidateOwner(h);
        return h.block_;
    }

    [[nodiscard]] JavaPayloadBorrow MakeBorrow(const JavaPayloadHandle& h, std::uint64_t id) const {
        JavaPayloadBorrow result;
        result.payload_ = h;
        result.id_ = id;
        return result;
    }

    [[nodiscard]] const JavaPayloadHandle& BorrowPayload(const JavaPayloadBorrow& b) const { return b.payload_; }

    [[nodiscard]] std::uint64_t BorrowId(const JavaPayloadBorrow& b) const { return b.id_; }
};

}  // namespace ogplay::runtime

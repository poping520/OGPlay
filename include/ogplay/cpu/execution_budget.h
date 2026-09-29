#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace ogplay::cpu {

struct ExecutionBudgetConfig final {
    std::uint32_t maximum_parallelism{}; // 0 = unrestricted
    std::uint64_t ticks_per_second{};    // aggregate across all process CPUs
};

struct ExecutionBudgetSnapshot final {
    std::uint32_t active{}, peak_active{}, waiting{};
    std::uint64_t consumed_ticks{};
    bool draining{};
};

// Admission applies only while executing guest instructions. Callers must
// leave before invoking host services, guest reentry or blocking operations.
class ExecutionBudget final {
public:
    using NowFunction = std::function<std::uint64_t()>; // monotonic nanoseconds
    class Lease final {
    public:
        Lease() = default;
        Lease(Lease&& other) noexcept;
        ~Lease();
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        [[nodiscard]] explicit operator bool() const noexcept { return owner_ != nullptr; }
        [[nodiscard]] std::uint64_t Ticks() const noexcept { return ticks_; }
        void Complete(std::uint64_t consumed);
    private:
        friend class ExecutionBudget;
        Lease(ExecutionBudget& owner, std::uint64_t ticks) : owner_(&owner), ticks_(ticks) {}
        ExecutionBudget* owner_{};
        std::uint64_t ticks_{};
    };

    explicit ExecutionBudget(ExecutionBudgetConfig config, NowFunction now = {});
    ~ExecutionBudget();
    ExecutionBudget(const ExecutionBudget&) = delete;
    ExecutionBudget& operator=(const ExecutionBudget&) = delete;
    [[nodiscard]] Lease Acquire(std::uint64_t requested, const std::atomic_bool& canceled);
    void Notify() noexcept;
    // Teardown releases admission/rate waits, retaining bounded slices so
    // lifecycle exit checks and guest finalizers can complete normally.
    void BeginDrain() noexcept;
    [[nodiscard]] ExecutionBudgetSnapshot Snapshot() const;

private:
    void Release(std::uint64_t reserved, std::uint64_t consumed) noexcept;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::cpu

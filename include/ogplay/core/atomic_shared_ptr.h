#pragma once

#include <atomic>
#include <memory>
#include <utility>
#include <version>

namespace ogplay::core {

// Atomic shared_ptr publication. MSVC deprecates the std::atomic_load/store
// shared_ptr overloads in C++20, while libc++ lacks std::atomic<std::shared_ptr>.
template <typename T>
class AtomicSharedPtr final {
public:
    AtomicSharedPtr() = default;
    AtomicSharedPtr(const AtomicSharedPtr&) = delete;
    AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;

#ifdef __cpp_lib_atomic_shared_ptr
    [[nodiscard]] std::shared_ptr<T> Load() const { return value_.load(); }
    void Store(std::shared_ptr<T> value) { value_.store(std::move(value)); }

private:
    std::atomic<std::shared_ptr<T>> value_;
#else
    [[nodiscard]] std::shared_ptr<T> Load() const { return std::atomic_load(&value_); }
    void Store(std::shared_ptr<T> value) { std::atomic_store(&value_, std::move(value)); }

private:
    std::shared_ptr<T> value_;
#endif
};

}  // namespace ogplay::core

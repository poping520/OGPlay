#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "ogplay/cpu/cpu.h"

namespace ogplay::cpu {

struct GuestThreadStart final {
    std::uint64_t thread_id{};
    memory::GuestAddress tls_base{0};
    A32State cpu_state;
};

struct GuestThreadExit final {
    std::uint64_t thread_id{};
    memory::GuestAddress tls_base{0};
    A32State cpu_state;
};

using CpuFactory = std::function<std::unique_ptr<Cpu>()>;
using GuestThreadEntry = std::function<void(Cpu&)>;

class GuestThreadGroup final {
public:
    explicit GuestThreadGroup(CpuFactory cpu_factory);
    ~GuestThreadGroup();

    GuestThreadGroup(const GuestThreadGroup&) = delete;
    GuestThreadGroup& operator=(const GuestThreadGroup&) = delete;

    void Spawn(GuestThreadStart start, GuestThreadEntry entry);
    [[nodiscard]] GuestThreadExit Join(std::uint64_t thread_id);
    [[nodiscard]] std::size_t ThreadCount() const;
    [[nodiscard]] std::size_t ActiveCount() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::cpu

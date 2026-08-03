#pragma once

#include <functional>
#include <memory>
#include <thread>

namespace ogplay::hal {

using HostThreadEntry = std::function<void()>;

class HostThread {
public:
    virtual ~HostThread() = default;
    [[nodiscard]] virtual bool Joinable() const noexcept = 0;
    [[nodiscard]] virtual std::thread::id Id() const noexcept = 0;
    virtual void Join() = 0;
};

[[nodiscard]] std::unique_ptr<HostThread> StartHostThread(HostThreadEntry entry);

}  // namespace ogplay::hal

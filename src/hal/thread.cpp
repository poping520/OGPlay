#include "ogplay/hal/thread.h"

#include <stdexcept>
#include <utility>

namespace ogplay::hal {
namespace {

class StandardHostThread final : public HostThread {
public:
    explicit StandardHostThread(HostThreadEntry entry)
        : thread_(std::move(entry)) {}

    [[nodiscard]] bool Joinable() const noexcept override {
        return thread_.joinable();
    }

    [[nodiscard]] std::thread::id Id() const noexcept override {
        return thread_.get_id();
    }

    void Join() override {
        if (!thread_.joinable()) {
            throw std::logic_error("host thread is not joinable");
        }
        thread_.join();
    }

private:
    std::jthread thread_;
};

}  // namespace

std::unique_ptr<HostThread> StartHostThread(HostThreadEntry entry) {
    if (!entry) throw std::invalid_argument("host thread entry is empty");
    return std::make_unique<StandardHostThread>(std::move(entry));
}

}  // namespace ogplay::hal

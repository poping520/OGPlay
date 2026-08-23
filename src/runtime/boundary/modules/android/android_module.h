#pragma once

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/core/boundary_binding.h"
#include "runtime/boundary/modules/android/android_exports.h"
#include "runtime/boundary/services/android_boundary_services.h"

namespace ogplay::runtime {

class AndroidModule final {
public:
    AndroidModule(BoundaryCallServices& calls,
                  AndroidBoundaryServices& services) noexcept;
    [[nodiscard]] BoundaryCallServices& CallServices() noexcept;
    void NotifyFileWrite();
    void PushInput(const AndroidBoundaryInput& input);

#define OGPLAY_DECLARE_ANDROID(name, id, count, method) \
    std::uint32_t method(const A32CallFrame& call);
    OGPLAY_ANDROID_BOUNDARY_EXPORTS(OGPLAY_DECLARE_ANDROID)
#undef OGPLAY_DECLARE_ANDROID

private:
    std::uint32_t PollAll(const std::array<std::uint32_t, 4>& args,
                          std::uint64_t thread_id);
    template <std::uint16_t FunctionId>
    std::uint32_t ExecuteExport(const A32CallFrame& call);

    BoundaryCallServices& calls_;
    AndroidBoundaryServices& services_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::uint64_t pending_command_writes_{};
    std::uint32_t command_ident_{};
    std::uint32_t command_data_{};
    std::uint32_t input_ident_{};
    std::uint32_t input_data_{};
    std::deque<AndroidBoundaryInput> inputs_;
    std::optional<AndroidBoundaryInput> active_input_;
};

}  // namespace ogplay::runtime

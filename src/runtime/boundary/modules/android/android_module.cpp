#include "runtime/boundary/modules/android/android_module.h"

#include <bit>
#include <chrono>
#include <cstddef>
#include <stdexcept>

namespace ogplay::runtime {
namespace {
constexpr std::uint32_t kFakeConfiguration = 0x6e003000U;
constexpr std::uint32_t kFakeLooper = 0x6e003100U;
constexpr std::uint32_t kFakeInputEvent = 0x6e003200U;

std::uint32_t SignedResult(const std::int32_t value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}
}  // namespace

AndroidModule::AndroidModule(BoundaryCallServices& calls,
                             AndroidBoundaryServices& services) noexcept
    : calls_(calls), services_(services) {}

BoundaryCallServices& AndroidModule::CallServices() noexcept { return calls_; }

void AndroidModule::NotifyFileWrite() {
    {
        std::scoped_lock lock(mutex_);
        ++pending_command_writes_;
    }
    ready_.notify_all();
}

void AndroidModule::PushInput(const AndroidBoundaryInput& input) {
    {
        std::scoped_lock lock(mutex_);
        inputs_.push_back(input);
    }
    ready_.notify_all();
}

std::uint32_t AndroidModule::PollAll(
    const std::array<std::uint32_t, 4>& args,
    const std::uint64_t thread_id) {
    const auto timeout = std::bit_cast<std::int32_t>(args[0]);
    std::unique_lock lock(mutex_);
    const auto has_source = [this] {
        return pending_command_writes_ != 0 || !inputs_.empty();
    };
    if (!has_source()) {
        if (timeout < 0) {
            ready_.wait(lock, has_source);
        } else if (timeout > 0) {
            ready_.wait_for(lock, std::chrono::milliseconds(timeout), has_source);
        }
    }
    std::uint32_t ident{};
    std::uint32_t data{};
    if (pending_command_writes_ != 0) {
        --pending_command_writes_;
        ident = command_ident_;
        data = command_data_;
    } else if (!inputs_.empty()) {
        ident = input_ident_;
        data = input_data_;
    } else {
        return SignedResult(-1);
    }
    lock.unlock();
    services_.Write32(args[1], 0, thread_id);
    services_.Write32(args[2], 1, thread_id);
    services_.Write32(args[3], data, thread_id);
    return ident;
}

template <std::uint16_t FunctionId>
std::uint32_t AndroidModule::ExecuteExport(const A32CallFrame& call) {
    const auto args = call.RegisterArguments();
    const auto tid = call.ThreadId();
    if constexpr (FunctionId == 0U) return kFakeConfiguration;
    if constexpr (FunctionId == 1U || FunctionId == 2U || FunctionId == 9U ||
                  FunctionId == 11U || FunctionId == 19U) return 0;
    if constexpr (FunctionId == 3U || FunctionId == 4U) {
        const auto output = call.Pointer<std::byte>(1);
        if (!output.IsNull()) {
            const std::array bytes{std::byte{'e'}, std::byte{'n'}};
            services_.address_space.Write(output.Address(), bytes, tid);
        }
        return 0;
    }
    if constexpr (FunctionId == 5U) return kFakeLooper;
    if constexpr (FunctionId == 6U) {
        std::scoped_lock lock(mutex_);
        command_ident_ = args[2];
        command_data_ = call.Argument(5);
        return 1;
    }
    if constexpr (FunctionId == 7U) return PollAll(args, tid);
    if constexpr (FunctionId == 8U) {
        std::scoped_lock lock(mutex_);
        input_ident_ = args[2];
        input_data_ = call.Argument(4);
        return 0;
    }
    if constexpr (FunctionId == 10U) {
        std::scoped_lock lock(mutex_);
        if (inputs_.empty()) return SignedResult(-1);
        active_input_ = inputs_.front();
        inputs_.pop_front();
        services_.Write32(args[1], kFakeInputEvent, tid);
        return 0;
    }
    if constexpr (FunctionId == 12U) {
        std::scoped_lock lock(mutex_);
        active_input_.reset();
        return 0;
    }
    if constexpr (FunctionId == 13U) {
        std::scoped_lock lock(mutex_);
        return active_input_.has_value() &&
                       active_input_->type == AndroidBoundaryInputType::key
                   ? 1U : 2U;
    }
    if constexpr (FunctionId == 14U) {
        std::scoped_lock lock(mutex_);
        return active_input_.has_value() && active_input_->pressed ? 0U : 1U;
    }
    if constexpr (FunctionId == 15U) {
        std::scoped_lock lock(mutex_);
        return active_input_.has_value()
                   ? static_cast<std::uint32_t>(active_input_->code) : 0U;
    }
    if constexpr (FunctionId == 16U) {
        std::scoped_lock lock(mutex_);
        if (!active_input_.has_value() ||
            active_input_->type == AndroidBoundaryInputType::pointer_motion) {
            return 2U;
        }
        return active_input_->pressed ? 0U : 1U;
    }
    if constexpr (FunctionId == 17U || FunctionId == 18U) {
        std::scoped_lock lock(mutex_);
        const auto value = !active_input_.has_value()
                               ? 0.0F
                               : FunctionId == 17U ? active_input_->x
                                                   : active_input_->y;
        return std::bit_cast<std::uint32_t>(value);
    }
    throw std::logic_error("unbound concrete libandroid export");
}

#define OGPLAY_DEFINE_ANDROID(name, id, count, method) \
    std::uint32_t AndroidModule::method(const A32CallFrame& call) { \
        return ExecuteExport<id>(call); \
    }
OGPLAY_ANDROID_BOUNDARY_EXPORTS(OGPLAY_DEFINE_ANDROID)
#undef OGPLAY_DEFINE_ANDROID

}  // namespace ogplay::runtime

#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ogplay::gles {

using GlesThunkId = std::uint16_t;
using GlesGuestValue = std::uint32_t;
class GlesHandler final {
public:
    GlesHandler() = default;

    template <typename Handler>
        requires (!std::is_same_v<std::remove_cvref_t<Handler>, GlesHandler>)
    GlesHandler(Handler&& handler) {
        using Stored = std::decay_t<Handler>;
        auto stored = std::make_shared<Stored>(std::forward<Handler>(handler));
        state_ = stored;
        invoke_ = +[](const void* state,
                      const std::span<const GlesGuestValue> arguments,
                      const std::uint64_t thread_id) -> GlesGuestValue {
            return (*static_cast<const Stored*>(state))(arguments, thread_id);
        };
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return invoke_ != nullptr;
    }

    [[nodiscard]] GlesGuestValue operator()(
        const std::span<const GlesGuestValue> arguments,
        const std::uint64_t thread_id) const {
        return invoke_(state_.get(), arguments, thread_id);
    }

private:
    using InvokeFn = GlesGuestValue (*)(
        const void*, std::span<const GlesGuestValue>, std::uint64_t);
    std::shared_ptr<const void> state_;
    InvokeFn invoke_{};
};

enum class GlesApi : std::uint8_t { gles1, gles1_extensions, gles2 };

struct GlesFunctionInfo final {
    GlesThunkId id{};
    std::string_view name;
    std::string_view return_type;
    std::size_t parameter_count{};
    std::size_t pointer_parameter_count{};
};

struct GlesUnimplementedCall final {
    GlesThunkId id{};
    std::string_view name;
    std::uint64_t hits{};
    std::uint64_t first_thread{};
    std::uint64_t last_thread{};
};

class GlesDispatchError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class GlesUnimplementedError final : public GlesDispatchError {
public:
    GlesUnimplementedError(GlesThunkId id, std::string_view name,
                           std::uint64_t thread_id);

    [[nodiscard]] GlesThunkId Id() const noexcept;
    [[nodiscard]] std::string_view Name() const noexcept;
    [[nodiscard]] std::uint64_t ThreadId() const noexcept;

private:
    GlesThunkId id_{};
    std::string name_;
    std::uint64_t thread_id_{};
};

class GlesDispatchTable final {
public:
    explicit GlesDispatchTable(GlesApi api = GlesApi::gles2);

    [[nodiscard]] static std::size_t FunctionCount() noexcept;
    [[nodiscard]] static std::optional<GlesThunkId> Find(
        std::string_view name) noexcept;
    [[nodiscard]] static GlesFunctionInfo Describe(GlesThunkId id);

    void Bind(std::string_view name, GlesHandler handler);
    void Seal();
    [[nodiscard]] bool IsSealed() const noexcept;
    [[nodiscard]] bool IsBound(GlesThunkId id) const;
    [[nodiscard]] GlesGuestValue Invoke(
        GlesThunkId id, std::span<const GlesGuestValue> arguments,
        std::uint64_t thread_id = 0);
    [[nodiscard]] std::vector<GlesUnimplementedCall> UnimplementedCalls() const;

private:
    struct UnimplementedState final {
        std::uint64_t hits{};
        std::uint64_t first_thread{};
        std::uint64_t last_thread{};
    };

    GlesApi api_{GlesApi::gles2};
    mutable std::mutex mutex_;
    std::atomic_bool sealed_{false};
    std::vector<GlesHandler> handlers_;
    std::vector<UnimplementedState> unimplemented_;
};

[[nodiscard]] std::size_t GlesFunctionCount(GlesApi api) noexcept;
[[nodiscard]] std::optional<GlesThunkId> FindGlesFunction(
    GlesApi api, std::string_view name) noexcept;
[[nodiscard]] GlesFunctionInfo DescribeGlesFunction(GlesApi api,
                                                    GlesThunkId id);

}  // namespace ogplay::gles

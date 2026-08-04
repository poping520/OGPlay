#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::gles {

using GlesThunkId = std::uint16_t;
using GlesGuestValue = std::uint32_t;
using GlesHandler = std::function<GlesGuestValue(
    std::span<const GlesGuestValue> arguments, std::uint64_t thread_id)>;

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
    GlesDispatchTable();

    [[nodiscard]] static std::size_t FunctionCount() noexcept;
    [[nodiscard]] static std::optional<GlesThunkId> Find(
        std::string_view name) noexcept;
    [[nodiscard]] static GlesFunctionInfo Describe(GlesThunkId id);

    void Bind(std::string_view name, GlesHandler handler);
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

    mutable std::mutex mutex_;
    std::vector<GlesHandler> handlers_;
    std::vector<UnimplementedState> unimplemented_;
};

}  // namespace ogplay::gles

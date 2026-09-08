#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::runtime {

struct GuestProcessEnvironmentEntry final {
    std::string name;
    std::string value;

    bool operator==(const GuestProcessEnvironmentEntry&) const = default;
};

// Immutable, deterministic initial environment for one guest process. It is
// deliberately independent from the host process environment.
class GuestProcessEnvironment final {
public:
    static constexpr std::size_t kMaximumEntries = 128U;
    static constexpr std::size_t kMaximumNameBytes = 255U;
    static constexpr std::size_t kMaximumValueBytes = 4095U;
    static constexpr std::size_t kMaximumSerializedBytes = 4096U;

    explicit GuestProcessEnvironment(
        std::vector<GuestProcessEnvironmentEntry> entries);

    [[nodiscard]] static GuestProcessEnvironment Api19(
        std::string external_storage_root = "/sdcard");
    [[nodiscard]] std::span<const GuestProcessEnvironmentEntry> Entries()
        const noexcept;
    [[nodiscard]] std::optional<std::string_view> Find(
        std::string_view name) const noexcept;
    [[nodiscard]] std::size_t SerializedBytes() const noexcept;

private:
    std::vector<GuestProcessEnvironmentEntry> entries_;
    std::size_t serialized_bytes_{};
};

}  // namespace ogplay::runtime

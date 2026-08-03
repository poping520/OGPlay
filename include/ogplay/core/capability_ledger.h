#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ogplay::core {

enum class CapabilityStatus : std::uint8_t { unimplemented, stub, partial, complete };

struct Capability {
    std::string id;
    CapabilityStatus status{CapabilityStatus::unimplemented};
    std::string test;
    std::string note;
};

struct UnimplementedHit {
    std::string id;
    std::uint64_t count{};
    std::uint64_t first_lr{};
    std::uint64_t last_lr{};
};

struct NullCallHit {
    std::uint64_t link_register{};
    std::string symbol;
    std::uint64_t count{};
};

class CapabilityLedger final {
public:
    static CapabilityLedger Load(const std::filesystem::path& path);

    CapabilityLedger() = default;
    CapabilityLedger(const CapabilityLedger&) = delete;
    CapabilityLedger& operator=(const CapabilityLedger&) = delete;
    CapabilityLedger(CapabilityLedger&& other) noexcept;
    CapabilityLedger& operator=(CapabilityLedger&& other) noexcept;

    void Register(Capability capability);
    [[nodiscard]] std::optional<Capability> Find(std::string_view id) const;
    [[nodiscard]] std::vector<Capability> All() const;

    void RecordUnimplemented(std::string_view id, std::uint64_t link_register);
    [[nodiscard]] std::vector<UnimplementedHit> Unimplemented() const;
    void RecordNullCall(std::uint64_t link_register, std::string_view symbol);
    [[nodiscard]] std::vector<NullCallHit> NullCalls() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, Capability, std::less<>> capabilities_;
    std::map<std::string, UnimplementedHit, std::less<>> hits_;
    std::map<std::pair<std::uint64_t, std::string>, NullCallHit> null_calls_;
};

[[nodiscard]] std::string_view ToString(CapabilityStatus status) noexcept;
[[nodiscard]] CapabilityStatus ParseCapabilityStatus(std::string_view value);

}  // namespace ogplay::core

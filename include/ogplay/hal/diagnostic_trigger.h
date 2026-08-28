#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ogplay::hal {

enum class DiagnosticWaitResult : std::uint8_t { triggered, stopped, failed };

class DiagnosticTrigger final {
public:
    class Backend;

    explicit DiagnosticTrigger(std::unique_ptr<Backend> backend) noexcept;
    ~DiagnosticTrigger();
    DiagnosticTrigger(const DiagnosticTrigger&) = delete;
    DiagnosticTrigger& operator=(const DiagnosticTrigger&) = delete;

    [[nodiscard]] std::string Name() const;
    [[nodiscard]] DiagnosticWaitResult Wait() noexcept;
    void Stop() noexcept;

private:
    std::unique_ptr<Backend> backend_;
};

[[nodiscard]] std::unique_ptr<DiagnosticTrigger> CreateDiagnosticTrigger(
    std::uint64_t process_id, std::string_view nonce);
void SignalDiagnosticTrigger(std::uint64_t process_id,
                             std::string_view trigger_name);
void RestrictDiagnosticFile(const std::filesystem::path& path);
[[nodiscard]] std::uint64_t HostProcessId() noexcept;
[[nodiscard]] std::uint64_t CurrentNativeThreadId() noexcept;

}  // namespace ogplay::hal

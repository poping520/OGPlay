#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "ogplay/cpu/cpu.h"

namespace ogplay::core {
class Logger;
}

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

class A32CallFrame;

struct LogBoundaryContext final {
    memory::AddressSpace& address_space;
    core::Logger* logger{};
    void* owner{};
    void (*record_fast_fault)(void*,
                              const cpu::A32HostCallContext&) noexcept{};
    void* guest_file_owner{};
    bool (*read_guest_file)(void*, std::string_view,
                            std::vector<std::byte>&){};

    void RecordFastFault(
        const cpu::A32HostCallContext& context) const noexcept {
        record_fast_fault(owner, context);
    }
};

class LogModule final {
public:
    explicit LogModule(LogBoundaryContext& context);
    ~LogModule();
    LogModule(const LogModule&) = delete;
    LogModule& operator=(const LogModule&) = delete;
    [[nodiscard]] LogBoundaryContext& CallServices() noexcept;

    std::uint32_t DevAvailable(const A32CallFrame& call);
    std::uint32_t Write(const A32CallFrame& call);
    std::uint32_t BufWrite(const A32CallFrame& call);
    std::uint32_t VPrint(const A32CallFrame& call);
    std::uint32_t Print(const A32CallFrame& call);
    std::uint32_t BufPrint(const A32CallFrame& call);
    std::uint32_t Assert(const A32CallFrame& call);
    std::uint32_t BinaryWrite(const A32CallFrame& call);
    std::uint32_t BinaryTypedWrite(const A32CallFrame& call);
    std::uint32_t FormatNew(const A32CallFrame& call);
    std::uint32_t FormatFree(const A32CallFrame& call);
    std::uint32_t SetPrintFormat(const A32CallFrame& call);
    std::uint32_t FormatFromString(const A32CallFrame& call);
    std::uint32_t AddFilterRule(const A32CallFrame& call);
    std::uint32_t AddFilterString(const A32CallFrame& call);
    std::uint32_t ShouldPrintLine(const A32CallFrame& call);
    std::uint32_t ProcessLogBuffer(const A32CallFrame& call);
    std::uint32_t ProcessBinaryLogBuffer(const A32CallFrame& call);
    std::uint32_t FormatLogLine(const A32CallFrame& call);
    std::uint32_t PrintLogLine(const A32CallFrame& call);
    std::uint32_t OpenEventTagMap(const A32CallFrame& call);
    std::uint32_t CloseEventTagMap(const A32CallFrame& call);
    std::uint32_t LookupEventTag(const A32CallFrame& call);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime

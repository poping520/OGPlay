#include "ogplay/runtime/common/guest_memory_facts.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include <string_view>

namespace ogplay::runtime {
GuestMemorySnapshot MakeGuestMemorySnapshot(const GuestProcFacts& facts) {
    GuestMemorySnapshot result{facts.memory_total_kb, facts.memory_free_kb,
        facts.memory_cached_kb.value_or(facts.memory_total_kb / 4U), facts.memory_buffers_kb,
        facts.memory_pressure};
    const auto& policy = result.pressure;
    if (result.total_kb == 0 || result.free_kb + result.cached_kb + result.buffers_kb > result.total_kb ||
        policy.foreground_kb > policy.visible_kb || policy.visible_kb > policy.service_kb ||
        policy.service_kb > policy.home_kb || policy.home_kb > policy.cached_kb)
        throw AndroidGuestProcessError("Android guest proc facts are invalid");
    return result;
}

std::string GuestMemorySnapshot::ProcText() const {
    std::string result;
    const auto append = [&result](const char* name, const std::uint64_t value) {
        const auto digits = std::to_string(value);
        const std::string_view label{name};
        const auto width = label.size() + digits.size();
        result += label; result.append(width < 24U ? 24U - width : 1U, ' ');
        result += digits; result += " kB\n";
    };
    append("MemTotal:", total_kb); append("MemFree:", free_kb);
    append("Buffers:", buffers_kb); append("Cached:", cached_kb);
    append("SwapCached:", 0); append("SwapTotal:", 0); append("SwapFree:", 0);
    return result;
}
} // namespace ogplay::runtime

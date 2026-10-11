#include "ogplay/runtime/common/guest_memory_facts.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include <string_view>
#include <limits>

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

void ValidateGuestProcessMemorySnapshot(const GuestProcessMemorySnapshot& snapshot) {
    if (snapshot.basis != GuestProcessMemoryBasis::injected_api19 &&
        snapshot.basis != GuestProcessMemoryBasis::virtual_private_committed)
        throw AndroidGuestProcessError("unknown guest process memory basis");
    std::array<std::int64_t, 7> totals{};
    for (const auto& heap : snapshot.heaps) {
        for (auto value : heap) if (value < 0)
            throw AndroidGuestProcessError("negative guest process memory statistic");
        if (heap[1] > heap[0] || static_cast<std::int64_t>(heap[2]) + heap[4] > heap[0])
            throw AndroidGuestProcessError("guest process PSS is smaller than private/swappable memory");
        for (std::size_t column = 0; column < totals.size(); ++column)
            totals[column] += heap[column];
    }
    for (auto total : totals) if (total > std::numeric_limits<std::int32_t>::max())
        throw AndroidGuestProcessError("guest process total exceeds API19 int range");
    for (auto value : snapshot.other_stats) if (value < 0)
        throw AndroidGuestProcessError("negative guest process memory subcategory");
    std::int64_t categorized_pss{};
    // The last five rows are Dalvik detail, not additional "other" memory.
    for (std::size_t i = 0; i < 16U * 7U; i += 7)
        categorized_pss += snapshot.other_stats[i];
    if (categorized_pss > snapshot.heaps[2][0])
        throw AndroidGuestProcessError("guest process subcategories exceed other PSS");
}

GuestProcessMemorySnapshot MakeVirtualPrivateProcessMemorySnapshot(
    std::uint64_t managed_bytes, std::uint64_t committed_arm_bytes) {
    const auto kib = [](std::uint64_t bytes) {
        const auto value = bytes / 1024U + (bytes % 1024U != 0 ? 1U : 0U);
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
            throw AndroidGuestProcessError("guest process memory exceeds API19 int range");
        return static_cast<std::int32_t>(value);
    };
    GuestProcessMemorySnapshot result;
    result.basis = GuestProcessMemoryBasis::virtual_private_committed;
    // The bounded model has private anonymous committed RAM, no guest paging
    // or cross-process sharing. Untagged ARM mappings belong to unknown other;
    // a zero native bucket does not claim an unused native allocator.
    result.heaps[0][0] = result.heaps[0][2] = kib(managed_bytes);
    result.heaps[2][0] = result.heaps[2][2] = kib(committed_arm_bytes);
    ValidateGuestProcessMemorySnapshot(result);
    return result;
}
} // namespace ogplay::runtime

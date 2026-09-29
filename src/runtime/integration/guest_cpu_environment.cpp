#include "guest_cpu_environment.h"
#include "ogplay/cpu/execution_budget.h"

#include <array>
#include <stdexcept>
#include <string>

namespace ogplay::runtime {

GuestCpuEnvironment::GuestCpuEnvironment(const GuestCpuConfig config) : config_(config) {
    if (config.cores < 1 || config.cores > 32 ||
        config.frequency_mhz < 1 || config.frequency_mhz > 10000) {
        throw std::invalid_argument("guest CPU requires 1..32 cores and 1..10000 MHz");
    }
    if (config.execution.max_mticks_per_second > 10000)
        throw std::invalid_argument("guest CPU rate requires 0..10000 million ticks/s");
}

std::shared_ptr<cpu::ExecutionBudget> GuestCpuEnvironment::MakeExecutionBudget() const {
    if (!config_.execution.limit_parallelism && config_.execution.max_mticks_per_second == 0) return {};
    return std::make_shared<cpu::ExecutionBudget>(cpu::ExecutionBudgetConfig{
        config_.execution.limit_parallelism ? config_.cores : 0,
        UINT64_C(1000000) * config_.execution.max_mticks_per_second});
}

void GuestCpuEnvironment::Publish(VirtualFileSystem& filesystem) {
    // Roll back registrations on any collision; never silently reuse stale facts.
    std::vector<std::unique_ptr<VfsGeneratedFileRegistration>> files;
    const auto publish = [&](const std::string& path, const std::string& text) {
        const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
        files.push_back(filesystem.RegisterGeneratedReadOnly(path, bytes.size(),
            [contents = std::vector<std::byte>(bytes.begin(), bytes.end())] { return contents; }));
    };
    std::string cpuinfo = "Processor\t: ARMv7 Processor (v7l)\n";
    for (std::uint32_t core = 0; core < config_.cores; ++core) {
        cpuinfo += "processor\t: " + std::to_string(core) + "\n\n";
    }
    // Fixed Dynarmic ARMv7 execution surface, not configurable ROM/host flags.
    cpuinfo += "Features\t: half thumb fastmult vfp edsp neon vfpv3 tls\n"
               "CPU architecture: 7\nHardware\t: OGPlay virtual ARMv7\n";
    publish("/proc/cpuinfo", cpuinfo);
    const std::string root = "/sys/devices/system/cpu/";
    const auto range = config_.cores == 1 ? "0\n" : "0-" + std::to_string(config_.cores - 1) + "\n";
    for (const auto* name : {"possible", "present", "online"}) publish(root + name, range);
    publish(root + "offline", "\n");
    const auto khz = std::to_string(config_.frequency_mhz * 1000U) + "\n";
    for (std::uint32_t core = 0; core < config_.cores; ++core) {
        const auto path = root + "cpu" + std::to_string(core) + "/";
        publish(path + "online", "1\n");
        // Fixed virtual clock, not a sample of host frequency or a throttle.
        for (const auto* name : {"cpuinfo_min_freq", "cpuinfo_max_freq", "scaling_min_freq",
                                 "scaling_max_freq", "scaling_cur_freq"}) {
            publish(path + "cpufreq/" + name, khz);
        }
    }
    files_ = std::move(files);
}

void GuestCpuEnvironment::BindSysconf(loader::Elf32LinkNamespace& symbols,
                                     memory::AddressSpace& memory) const {
    for (auto& module : symbols.modules) {
        if (module.dynamic.soname.value_or(module.name) != "libc.so") continue;
        for (auto& symbol : module.symbols.symbols) {
            if (symbol.name != "sysconf" || !symbol.IsExported()) continue;
            constexpr std::uint16_t absolute = 0xfff1;
            const auto original = symbol.section_index == absolute ? symbol.value
                : module.load_bias.Add(symbol.value.Value());
            const auto code = memory.MapAnywhere({memory::GuestAddress{0x7d000000}, 0x01000000},
                memory.PageSize(), memory::PageProtection::read | memory::PageProtection::write);
            // API 19 Bionic selectors 96/97. Preserve LR/stack/arguments on the
            // fallback; BX supports either ARM or Thumb original libc exports.
            const std::array words{
                0xe3500060U,                 // cmp r0, #96
                0x13500061U,                 // cmpne r0, #97
                0x03a00000U | config_.cores, // moveq r0, #cores
                0x012fff1eU,                 // bxeq lr
                0xe59fc000U,                 // ldr ip, [pc]
                0xe12fff1cU,                 // bx ip
                original.Value()};
            for (std::size_t i = 0; i < words.size(); ++i) memory.Write32(code.Add(i * 4), words[i]);
            memory.Protect({code, memory.PageSize()},
                memory::PageProtection::read | memory::PageProtection::execute);
            symbol.value = code;
            symbol.section_index = absolute;
            symbol.size = static_cast<std::uint32_t>(words.size() * 4);
            return;
        }
    }
    // Minimal loader fixtures may omit sysconf; no synthetic export is invented.
}

}  // namespace ogplay::runtime

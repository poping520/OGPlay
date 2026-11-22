#include "ogplay/runtime/process/virtual_process.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ogplay::runtime {
namespace {

[[nodiscard]] std::string UserName(const std::uint32_t uid) {
    // android_filesystem_config.h names; applications are u<user>_a<app>.
    if (uid == 0) return "root";
    if (uid == 1000) return "system";
    if (uid >= 10000 && uid < 20000) return "u0_a" + std::to_string(uid - 10000);
    return std::to_string(uid);
}

[[nodiscard]] bool IsNumber(const std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](const char c) { return c >= '0' && c <= '9'; });
}

[[nodiscard]] char StateOf(const VirtualProcessInfo& process, const std::int32_t self) {
    if (process.phase == VirtualProcessPhase::zombie) return 'Z';
    return process.pid == self ? 'R' : 'S';
}

int RunToolbox(VirtualCommandContext& context) {
    const auto& arguments = context.Arguments();
    if (arguments.size() < 2U) {
        context.Write(1, "Toolbox!\n");
        return 0;
    }
    // "toolbox ps ..." behaves as "ps ...".
    std::vector<std::string> shifted(arguments.begin() + 1, arguments.end());
    if (shifted.front() == "ps" || shifted.front() == "getprop") {
        const auto run = shifted.front() == "ps" ? RunPsCommand : RunGetpropCommand;
        VirtualCommandContext tool(context.Kernel(), context.Vfs(), context.Pid(), std::move(shifted),
                                   context.Environment(), context.WorkingDirectory(), context.StopToken());
        return run(tool);
    }
    context.Write(2, shifted.front() + ": no such tool\n");
    return 255;
}

}  // namespace

int RunPsCommand(VirtualCommandContext& context) {
    // Android 4.4 toolbox ps: optional flags, then a pid or a name prefix.
    std::optional<std::int32_t> pid_filter;
    std::optional<std::string> name_filter;
    const auto& arguments = context.Arguments();
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (argument.starts_with('-')) {
            context.Kernel().RecordUnsupported("process.ps.option");
        } else if (IsNumber(argument)) {
            pid_filter = std::stoi(argument);
        } else {
            name_filter = argument;
        }
    }
    std::string output = "USER     PID   PPID  VSIZE  RSS     WCHAN    PC         NAME\n";
    for (const auto& process : context.Kernel().Processes()) {
        if (pid_filter && process.pid != *pid_filter) continue;
        if (name_filter && !process.name.starts_with(*name_filter)) continue;
        std::array<char, 128> fields{};
        std::snprintf(fields.data(), fields.size(), "%-9s %-5d %-5d %-6llu %-5llu %08x %08x %c ",
                      UserName(process.uid).c_str(), process.pid, process.parent_pid,
                      static_cast<unsigned long long>(process.virtual_size_bytes / 1024U),
                      static_cast<unsigned long long>(process.resident_size_bytes / 1024U), 0U, 0U,
                      StateOf(process, context.Pid()));
        output += fields.data();
        output += process.name;
        output += '\n';
    }
    return context.Write(1, output) ? 0 : 1;
}

int RunGetpropCommand(VirtualCommandContext& context) {
    // Android 4.4 toolbox getprop: no argument lists every property as
    // "[key]: [value]"; "getprop KEY [DEFAULT]" prints one value, the default
    // when the property is unset or empty.
    const auto& arguments = context.Arguments();
    const auto properties = context.Kernel().SystemProperties();
    if (arguments.size() < 2U) {
        std::string output;
        for (const auto& [key, value] : properties)
            output += "[" + key + "]: [" + value + "]\n";
        return context.Write(1, output) ? 0 : 1;
    }
    std::string value = arguments.size() > 2U ? arguments[2] : std::string();
    for (const auto& [key, found] : properties)
        if (key == arguments[1] && !found.empty()) value = found;
    return context.Write(1, value + "\n") ? 0 : 1;
}

void RegisterBuiltinCommands(VirtualProcessKernel& kernel) {
    kernel.RegisterCommand("/system/bin/sh", RunShellCommand);
    kernel.RegisterCommand("/system/bin/ps", RunPsCommand);
    kernel.RegisterCommand("/system/bin/getprop", RunGetpropCommand);
    kernel.RegisterCommand("/system/bin/toolbox", RunToolbox);
}

}  // namespace ogplay::runtime

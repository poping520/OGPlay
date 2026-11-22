#pragma once

// In-process virtual processes: a process table, wait/kill semantics and
// built-in commands that run as host code on their own host threads. No host
// process is ever created; to the guest these are ordinary child processes.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/runtime/common/api19_identity.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::core {
class CapabilityLedger;
}

namespace ogplay::runtime {

inline constexpr std::int32_t kVirtualInitProcessId = 1;
// Linux signal numbers the kernel reports in wait statuses.
inline constexpr std::int32_t kVirtualSigkill = 9;
inline constexpr std::int32_t kVirtualSigpipe = 13;
inline constexpr std::int32_t kVirtualSigterm = 15;
// PATH an Android 4.4 shell starts with.
inline constexpr std::string_view kVirtualDefaultPath = "/sbin:/vendor/bin:/system/sbin:/system/bin:/system/xbin";

class VirtualProcessError final : public std::runtime_error {
public:
    VirtualProcessError(std::int32_t error_number, const std::string& message)
        : std::runtime_error(message), error_number_(error_number) {}

    [[nodiscard]] std::int32_t ErrorNumber() const noexcept { return error_number_; }

private:
    std::int32_t error_number_{};
};

enum class VirtualProcessPhase : std::uint8_t {
    // Created by fork/vfork; runs guest code of its parent until exec or exit.
    forked,
    // Runs a built-in command on its own host thread.
    running,
    // Exited, status not collected yet.
    zombie,
};

struct VirtualProcessInfo final {
    std::int32_t pid{};
    std::int32_t parent_pid{};
    std::uint32_t uid{};
    // ps NAME: argv[0] after exec, otherwise the inherited name.
    std::string name;
    VirtualProcessPhase phase{VirtualProcessPhase::running};
    bool application{};
    std::uint64_t virtual_size_bytes{};
    std::uint64_t resident_size_bytes{};
};

struct VirtualProcessMemory final {
    std::uint64_t virtual_size_bytes{};
    std::uint64_t resident_size_bytes{};
};

struct VirtualProcessWaitResult final {
    // 0 for WNOHANG with running children only.
    std::int32_t pid{};
    // Linux wait status: (code & 0xff) << 8 for exits, the signal for kills.
    std::int32_t status{};
};

class VirtualProcessKernel;

// What a built-in command sees: arguments, environment, working directory and
// its own descriptor table, which the command thread is bound to.
class VirtualCommandContext final {
public:
    VirtualCommandContext(VirtualProcessKernel& kernel, VirtualFileSystem& vfs, std::int32_t pid,
                          std::vector<std::string> arguments, std::vector<std::string> environment,
                          std::string working_directory, std::stop_token stop);

    [[nodiscard]] VirtualProcessKernel& Kernel() const noexcept { return kernel_; }

    [[nodiscard]] VirtualFileSystem& Vfs() const noexcept { return vfs_; }

    [[nodiscard]] std::int32_t Pid() const noexcept { return pid_; }

    [[nodiscard]] const std::vector<std::string>& Arguments() const noexcept { return arguments_; }

    [[nodiscard]] const std::vector<std::string>& Environment() const noexcept { return environment_; }

    [[nodiscard]] const std::string& WorkingDirectory() const noexcept { return working_directory_; }

    [[nodiscard]] std::stop_token StopToken() const noexcept { return stop_; }

    [[nodiscard]] std::optional<std::string> Variable(std::string_view name) const;
    // Absolute form of `path` against the working directory.
    [[nodiscard]] std::string Absolute(std::string_view path) const;
    // Writes everything to `descriptor`; false when the descriptor is closed or
    // the reader went away.
    bool Write(std::int32_t descriptor, std::string_view text);
    // Reads up to `destination.size()` bytes; 0 at EOF or on error.
    [[nodiscard]] std::size_t Read(std::int32_t descriptor, std::span<std::byte> destination);

private:
    VirtualProcessKernel& kernel_;
    VirtualFileSystem& vfs_;
    std::int32_t pid_{};
    std::vector<std::string> arguments_;
    std::vector<std::string> environment_;
    std::string working_directory_;
    std::stop_token stop_;
};

// Returns the exit code (0..255).
using VirtualCommand = std::function<int(VirtualCommandContext&)>;

struct VirtualProcessKernelConfig final {
    VirtualFileSystem* vfs{};
    core::CapabilityLedger* ledger{};
    std::int32_t application_pid{kGuestProcessId};
    std::int32_t application_parent_pid{kGuestParentProcessId};
    std::uint32_t application_uid{10000};
    std::string application_name;
    std::function<VirtualProcessMemory()> application_memory;
    // Numbers the kernel must not hand out, such as live guest thread ids.
    std::function<bool(std::int32_t)> reserved_id;
    // Called after a child of the application exits (SIGCHLD source).
    std::function<void(std::int32_t pid)> application_child_exited;
};

class VirtualProcessKernel final {
public:
    explicit VirtualProcessKernel(VirtualProcessKernelConfig config);
    // Stops running commands, joins their threads and releases their tables.
    ~VirtualProcessKernel();
    VirtualProcessKernel(const VirtualProcessKernel&) = delete;
    VirtualProcessKernel& operator=(const VirtualProcessKernel&) = delete;

    [[nodiscard]] std::int32_t ApplicationPid() const noexcept;
    // ps NAME of the application, normally its package (process) name.
    void SetApplicationName(std::string name);
    // System properties of the simulated device, as SystemProperties.get
    // answers them (key, value); getprop reads them. Replacing or clearing
    // the source waits for calls in flight, so its owner can retire safely.
    using SystemPropertySource = std::function<std::vector<std::pair<std::string, std::string>>()>;
    void SetSystemPropertySource(SystemPropertySource source);
    // Sorted by key; empty without a source.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> SystemProperties() const;
    [[nodiscard]] VirtualFileSystem& Vfs() const noexcept;

    // Exact executable path, such as "/system/bin/ps".
    void RegisterCommand(std::string path, VirtualCommand command);
    [[nodiscard]] bool HasCommand(std::string_view path) const;
    // execvp-style lookup: names with a slash are taken as paths, others are
    // searched along `search_path`. Empty when nothing matches.
    [[nodiscard]] std::optional<std::string> ResolveExecutable(std::string_view name,
                                                               std::string_view search_path) const;

    // fork/vfork: a child of `parent` whose descriptor table copies the table
    // `parent_table`. It stays `forked` until Exec or Exit.
    [[nodiscard]] std::int32_t Fork(std::int32_t parent, VfsDescriptorTableId parent_table);
    [[nodiscard]] VfsDescriptorTableId DescriptorTable(std::int32_t pid) const;
    [[nodiscard]] std::int32_t ParentOf(std::int32_t pid) const;
    // execve of a forked process: ENOENT for unknown paths, E2BIG/EINVAL for
    // bad arguments. On success close-on-exec descriptors are closed and the
    // command starts on its own thread.
    void Exec(std::int32_t pid, std::string_view path, std::vector<std::string> arguments,
              std::vector<std::string> environment, std::string working_directory);
    // _exit of a forked process before exec.
    void Exit(std::int32_t pid, std::int32_t exit_code);
    // posix_spawn: Fork + Exec of `command` without guest code. `arrange` runs
    // with the child's table bound to set up its descriptors; an exception
    // from it aborts the spawn.
    [[nodiscard]] std::int32_t Spawn(std::int32_t parent, VfsDescriptorTableId parent_table, std::string_view path,
                                     std::vector<std::string> arguments, std::vector<std::string> environment,
                                     std::string working_directory, const std::function<void()>& arrange = {});
    // As Spawn, but runs an anonymous command (shell builtins and subshells).
    [[nodiscard]] std::int32_t SpawnCommand(std::int32_t parent, VfsDescriptorTableId parent_table,
                                            VirtualCommand command, std::vector<std::string> arguments,
                                            std::vector<std::string> environment, std::string working_directory,
                                            const std::function<void()>& arrange = {});

    // wait4: selector > 0 waits for that child, otherwise for any child.
    // ECHILD without matching children, EINTR when `interrupted` reports.
    [[nodiscard]] VirtualProcessWaitResult Wait(std::int32_t caller, std::int32_t selector, bool no_hang,
                                                const std::function<bool()>& interrupted = {});
    // kill: signal 0 only checks existence. ESRCH for unknown pids, EPERM for
    // init. Built-in commands stop cooperatively and report the signal.
    void Kill(std::int32_t pid, std::int32_t signal);
    [[nodiscard]] bool Exists(std::int32_t pid) const;
    // Accounts a recognised but unsupported command feature.
    void RecordUnsupported(std::string_view id) const;
    [[nodiscard]] std::vector<VirtualProcessInfo> Processes() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Registers sh, ps, getprop and the toolbox multi-call entry under /system/bin.
void RegisterBuiltinCommands(VirtualProcessKernel& kernel);

[[nodiscard]] int RunShellCommand(VirtualCommandContext& context);
[[nodiscard]] int RunPsCommand(VirtualCommandContext& context);
[[nodiscard]] int RunGetpropCommand(VirtualCommandContext& context);

}  // namespace ogplay::runtime

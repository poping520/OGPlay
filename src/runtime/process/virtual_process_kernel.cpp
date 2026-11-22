#include "ogplay/runtime/process/virtual_process.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

#include "ogplay/core/capability_ledger.h"

namespace ogplay::runtime {
namespace {

constexpr std::int32_t kEsrch = 3;
constexpr std::int32_t kEintr = 4;
constexpr std::int32_t kE2big = 7;
constexpr std::int32_t kEnoent = 2;
constexpr std::int32_t kEchild = 10;
constexpr std::int32_t kEagain = 11;
constexpr std::int32_t kEperm = 1;
constexpr std::int32_t kEinval = 22;
constexpr std::int32_t kEpipe = 32;

constexpr std::int32_t kFirstVirtualPid = 2000;
constexpr std::int32_t kLastVirtualPid = 32767;
// ARG_MAX of Android 4.4 kernels is far larger; this bounds host memory.
constexpr std::size_t kMaximumArguments = 1024;
constexpr std::size_t kMaximumArgumentBytes = 128U * 1024U;

// Sizes ps reports for processes without a guest address space, in bytes.
constexpr VirtualProcessMemory kInitMemory{708U * 1024U, 360U * 1024U};
constexpr VirtualProcessMemory kCommandMemory{1252U * 1024U, 484U * 1024U};

[[nodiscard]] std::int32_t ExitStatus(const std::int32_t code) {
    return (code & 0xff) << 8;
}

[[nodiscard]] bool IgnoredByDefault(const std::int32_t signal) {
    // SIGCHLD, SIGCONT, SIGURG, SIGWINCH are ignored; job-control stops are
    // outside the supported surface and are ignored too.
    return signal == 17 || signal == 18 || signal == 23 || signal == 28 || (signal >= 19 && signal <= 22);
}

[[nodiscard]] std::string BaseName(const std::string_view path) {
    const auto slash = path.rfind('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

void CheckArguments(const std::vector<std::string>& arguments, const std::vector<std::string>& environment) {
    if (arguments.size() + environment.size() > kMaximumArguments)
        throw VirtualProcessError(kE2big, "too many exec arguments");
    std::size_t bytes{};
    for (const auto* list : {&arguments, &environment})
        for (const auto& value : *list)
            bytes += value.size() + 1U;
    if (bytes > kMaximumArgumentBytes) throw VirtualProcessError(kE2big, "exec arguments are too long");
}

}  // namespace

class VirtualProcessKernel::Impl final {
public:
    struct Process final {
        std::int32_t pid{};
        std::int32_t parent{};
        std::uint32_t uid{};
        std::string name;
        VirtualProcessPhase phase{VirtualProcessPhase::running};
        bool application{};
        bool init{};
        std::optional<VfsDescriptorTableId> table;
        std::int32_t status{};
        std::int32_t kill_signal{};
        std::stop_source stop;
        std::thread thread;
    };

    explicit Impl(VirtualProcessKernelConfig initial) : config(std::move(initial)) {
        if (config.vfs == nullptr) throw VirtualProcessError(kEinval, "virtual process kernel needs a VFS");
        Process init_process;
        init_process.pid = kVirtualInitProcessId;
        init_process.uid = 0;
        init_process.name = "/init";
        init_process.init = true;
        processes.emplace(init_process.pid, std::move(init_process));
        Process application;
        application.pid = config.application_pid;
        application.parent = config.application_parent_pid;
        application.uid = config.application_uid;
        application.name = config.application_name;
        application.application = true;
        application.table = kVfsApplicationDescriptorTable;
        processes.emplace(application.pid, std::move(application));
    }

    void Record(const std::string_view id) const {
        if (config.ledger != nullptr) config.ledger->RecordUnimplemented(id, 0);
    }

    [[nodiscard]] Process& FindLocked(const std::int32_t pid) {
        const auto found = processes.find(pid);
        if (found == processes.end()) throw VirtualProcessError(kEsrch, "no such virtual process");
        return found->second;
    }

    [[nodiscard]] std::int32_t AllocatePidLocked() {
        for (std::int32_t attempt = kFirstVirtualPid; attempt <= kLastVirtualPid; ++attempt) {
            const auto candidate = next_pid;
            next_pid = next_pid == kLastVirtualPid ? kFirstVirtualPid : next_pid + 1;
            if (processes.contains(candidate)) continue;
            if (config.reserved_id && config.reserved_id(candidate)) continue;
            return candidate;
        }
        throw VirtualProcessError(kEagain, "virtual process ids are exhausted");
    }

    std::int32_t Fork(const std::int32_t parent, const VfsDescriptorTableId parent_table) {
        std::uint32_t uid{};
        std::string name;
        {
            std::scoped_lock lock(mutex);
            const auto& owner = FindLocked(parent);
            if (owner.phase == VirtualProcessPhase::zombie) throw VirtualProcessError(kEsrch, "parent has exited");
            uid = owner.uid;
            name = owner.name;
        }
        const auto table = config.vfs->ForkDescriptorTable(parent_table);
        std::scoped_lock lock(mutex);
        Process child;
        try {
            child.pid = AllocatePidLocked();
        } catch (...) {
            config.vfs->ReleaseDescriptorTable(table);
            throw;
        }
        child.parent = parent;
        child.uid = uid;
        child.name = std::move(name);
        child.phase = VirtualProcessPhase::forked;
        child.table = table;
        const auto pid = child.pid;
        processes.emplace(pid, std::move(child));
        return pid;
    }

    void Start(const std::int32_t pid, VirtualCommand command, std::string name, std::vector<std::string> arguments,
               std::vector<std::string> environment, std::string working_directory) {
        std::scoped_lock lock(mutex);
        auto& process = FindLocked(pid);
        process.phase = VirtualProcessPhase::running;
        process.name = std::move(name);
        const auto table = *process.table;
        process.thread = std::thread([this, pid, table, command = std::move(command), stop = process.stop.get_token(),
                                      arguments = std::move(arguments), environment = std::move(environment),
                                      working_directory = std::move(working_directory)]() mutable {
            std::int32_t status{};
            {
                VfsThreadBinding binding(*config.vfs, table, [stop] { return stop.stop_requested(); });
                VirtualCommandContext context(*owner, *config.vfs, pid, std::move(arguments), std::move(environment),
                                              std::move(working_directory), stop);
                try {
                    status = ExitStatus(command(context));
                } catch (const VfsError& error) {
                    status = error.ErrorNumber() == kEpipe ? kVirtualSigpipe : ExitStatus(1);
                } catch (...) {
                    status = ExitStatus(1);
                }
            }
            Finish(pid, status);
        });
    }

    // Moves `pid` to zombie with `status`, releasing its descriptor table
    // first so readers of its pipes see EOF before the parent's wait returns.
    void Finish(const std::int32_t pid, std::int32_t status) {
        std::optional<VfsDescriptorTableId> table;
        {
            std::scoped_lock lock(mutex);
            auto& process = FindLocked(pid);
            table = std::exchange(process.table, std::nullopt);
        }
        if (table) config.vfs->ReleaseDescriptorTable(*table);
        std::int32_t parent{};
        {
            std::scoped_lock lock(mutex);
            auto& process = FindLocked(pid);
            if (process.kill_signal != 0) status = process.kill_signal;
            process.status = status;
            process.phase = VirtualProcessPhase::zombie;
            parent = process.parent;
            for (auto& [child_pid, child] : processes) {
                static_cast<void>(child_pid);
                if (child.parent == pid) child.parent = kVirtualInitProcessId;
            }
            // init reaps its children immediately.
            std::vector<std::int32_t> orphans;
            for (auto& [child_pid, child] : processes)
                if (child.parent == kVirtualInitProcessId && child.phase == VirtualProcessPhase::zombie &&
                    !child.init && !child.application)
                    orphans.push_back(child_pid);
            // A command thread cannot join itself; the destructor joins these.
            for (const auto orphan : orphans) {
                auto node = processes.extract(orphan);
                if (node.mapped().thread.joinable()) finished_threads.push_back(std::move(node.mapped().thread));
            }
        }
        changed.notify_all();
        if (parent == config.application_pid && config.application_child_exited) config.application_child_exited(pid);
    }

    VirtualProcessWaitResult Wait(const std::int32_t caller, const std::int32_t selector, const bool no_hang,
                                  const std::function<bool()>& interrupted) {
        std::unique_lock lock(mutex);
        for (;;) {
            bool matched{};
            for (auto it = processes.begin(); it != processes.end(); ++it) {
                auto& process = it->second;
                if (process.parent != caller || (selector > 0 && process.pid != selector)) continue;
                matched = true;
                if (process.phase != VirtualProcessPhase::zombie) continue;
                const VirtualProcessWaitResult result{process.pid, process.status};
                auto thread = std::move(process.thread);
                processes.erase(it);
                lock.unlock();
                if (thread.joinable()) thread.join();
                return result;
            }
            if (!matched) throw VirtualProcessError(kEchild, "no child processes");
            if (no_hang) return {};
            if (interrupted && interrupted()) throw VirtualProcessError(kEintr, "wait interrupted");
            changed.wait_for(lock, std::chrono::milliseconds(10));
        }
    }

    VirtualProcessKernelConfig config;
    VirtualProcessKernel* owner{};
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::map<std::int32_t, Process> processes;
    std::map<std::string, VirtualCommand, std::less<>> commands;
    std::vector<std::thread> finished_threads;
    // Held while the source runs, so replacing it waits for calls in flight.
    mutable std::mutex property_mutex;
    SystemPropertySource property_source;
    std::int32_t next_pid{kFirstVirtualPid};
};

VirtualCommandContext::VirtualCommandContext(VirtualProcessKernel& kernel, VirtualFileSystem& vfs,
                                             const std::int32_t pid, std::vector<std::string> arguments,
                                             std::vector<std::string> environment, std::string working_directory,
                                             std::stop_token stop)
    : kernel_(kernel), vfs_(vfs), pid_(pid), arguments_(std::move(arguments)), environment_(std::move(environment)),
      working_directory_(std::move(working_directory)), stop_(std::move(stop)) {
    if (working_directory_.empty()) working_directory_ = "/";
}

std::optional<std::string> VirtualCommandContext::Variable(const std::string_view name) const {
    for (const auto& entry : environment_) {
        if (entry.size() > name.size() && entry.starts_with(name) && entry[name.size()] == '=')
            return entry.substr(name.size() + 1U);
    }
    return std::nullopt;
}

std::string VirtualCommandContext::Absolute(const std::string_view path) const {
    if (path.starts_with('/')) return std::string(path);
    return (working_directory_.ends_with('/') ? working_directory_ : working_directory_ + "/") + std::string(path);
}

bool VirtualCommandContext::Write(const std::int32_t descriptor, const std::string_view text) {
    auto remaining = std::as_bytes(std::span{text.data(), text.size()});
    try {
        while (!remaining.empty()) {
            const auto written = vfs_.Write(descriptor, remaining);
            if (written == 0) return false;
            remaining = remaining.subspan(written);
        }
        return true;
    } catch (const VfsError& error) {
        // A vanished reader ends the command as SIGPIPE does on Linux.
        if (error.ErrorNumber() == kEpipe) throw;
        return false;
    }
}

std::size_t VirtualCommandContext::Read(const std::int32_t descriptor, const std::span<std::byte> destination) {
    try {
        return vfs_.Read(descriptor, destination);
    } catch (const VfsError&) {
        return 0;
    }
}

VirtualProcessKernel::VirtualProcessKernel(VirtualProcessKernelConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {
    impl_->owner = this;
}

VirtualProcessKernel::~VirtualProcessKernel() {
    std::vector<std::thread> threads;
    {
        std::scoped_lock lock(impl_->mutex);
        for (auto& [pid, process] : impl_->processes) {
            static_cast<void>(pid);
            if (process.phase == VirtualProcessPhase::running && !process.application) {
                if (process.kill_signal == 0) process.kill_signal = kVirtualSigkill;
                process.stop.request_stop();
            }
        }
    }
    // Command threads finish through Finish(), which needs the kernel alive.
    for (;;) {
        {
            std::scoped_lock lock(impl_->mutex);
            for (auto& [pid, process] : impl_->processes) {
                static_cast<void>(pid);
                if (process.thread.joinable()) threads.push_back(std::move(process.thread));
            }
            for (auto& thread : impl_->finished_threads)
                threads.push_back(std::move(thread));
            impl_->finished_threads.clear();
        }
        if (threads.empty()) break;
        for (auto& thread : threads)
            if (thread.joinable()) thread.join();
        threads.clear();
    }
    std::vector<VfsDescriptorTableId> tables;
    {
        std::scoped_lock lock(impl_->mutex);
        for (auto& [pid, process] : impl_->processes) {
            static_cast<void>(pid);
            if (!process.application && process.table) tables.push_back(*std::exchange(process.table, std::nullopt));
        }
    }
    for (const auto table : tables) {
        try {
            impl_->config.vfs->ReleaseDescriptorTable(table);
        } catch (const VfsError&) {
        }
    }
}

std::int32_t VirtualProcessKernel::ApplicationPid() const noexcept {
    return impl_->config.application_pid;
}

void VirtualProcessKernel::SetApplicationName(std::string name) {
    std::scoped_lock lock(impl_->mutex);
    impl_->FindLocked(impl_->config.application_pid).name = std::move(name);
}

void VirtualProcessKernel::SetSystemPropertySource(SystemPropertySource source) {
    std::scoped_lock lock(impl_->property_mutex);
    impl_->property_source = std::move(source);
}

std::vector<std::pair<std::string, std::string>> VirtualProcessKernel::SystemProperties() const {
    std::vector<std::pair<std::string, std::string>> properties;
    {
        std::scoped_lock lock(impl_->property_mutex);
        if (impl_->property_source) properties = impl_->property_source();
    }
    std::ranges::sort(properties);
    return properties;
}

VirtualFileSystem& VirtualProcessKernel::Vfs() const noexcept {
    return *impl_->config.vfs;
}

void VirtualProcessKernel::RegisterCommand(std::string path, VirtualCommand command) {
    if (!path.starts_with('/') || !command) throw VirtualProcessError(kEinval, "invalid virtual command");
    std::scoped_lock lock(impl_->mutex);
    impl_->commands.insert_or_assign(std::move(path), std::move(command));
}

bool VirtualProcessKernel::HasCommand(const std::string_view path) const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->commands.find(path) != impl_->commands.end();
}

std::optional<std::string> VirtualProcessKernel::ResolveExecutable(const std::string_view name,
                                                                   const std::string_view search_path) const {
    if (name.empty()) return std::nullopt;
    if (name.find('/') != std::string_view::npos) {
        return HasCommand(name) ? std::optional<std::string>(name) : std::nullopt;
    }
    std::size_t begin{};
    while (begin <= search_path.size()) {
        const auto end = std::min(search_path.find(':', begin), search_path.size());
        auto directory = std::string(search_path.substr(begin, end - begin));
        if (directory.empty()) directory = ".";
        const auto candidate = directory + "/" + std::string(name);
        if (HasCommand(candidate)) return candidate;
        begin = end + 1U;
    }
    return std::nullopt;
}

std::int32_t VirtualProcessKernel::Fork(const std::int32_t parent, const VfsDescriptorTableId parent_table) {
    return impl_->Fork(parent, parent_table);
}

VfsDescriptorTableId VirtualProcessKernel::DescriptorTable(const std::int32_t pid) const {
    std::scoped_lock lock(impl_->mutex);
    const auto& process = impl_->FindLocked(pid);
    if (!process.table) throw VirtualProcessError(kEsrch, "virtual process has no descriptor table");
    return *process.table;
}

std::int32_t VirtualProcessKernel::ParentOf(const std::int32_t pid) const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->FindLocked(pid).parent;
}

void VirtualProcessKernel::Exec(const std::int32_t pid, const std::string_view path, std::vector<std::string> arguments,
                                std::vector<std::string> environment, std::string working_directory) {
    CheckArguments(arguments, environment);
    VirtualCommand command;
    VfsDescriptorTableId table{};
    {
        std::scoped_lock lock(impl_->mutex);
        const auto& process = impl_->FindLocked(pid);
        if (process.phase != VirtualProcessPhase::forked || !process.table)
            throw VirtualProcessError(kEinval, "only a forked virtual process can exec");
        const auto found = impl_->commands.find(path);
        if (found == impl_->commands.end()) {
            impl_->Record("process.exec.missing_command");
            throw VirtualProcessError(kEnoent, "no such executable");
        }
        command = found->second;
        table = *process.table;
    }
    {
        VfsThreadBinding binding(*impl_->config.vfs, table);
        impl_->config.vfs->CloseOnExecDescriptors();
    }
    auto name = arguments.empty() ? BaseName(path) : arguments.front();
    impl_->Start(pid, std::move(command), std::move(name), std::move(arguments), std::move(environment),
                 std::move(working_directory));
}

void VirtualProcessKernel::Exit(const std::int32_t pid, const std::int32_t exit_code) {
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->FindLocked(pid).phase != VirtualProcessPhase::forked)
            throw VirtualProcessError(kEinval, "only a forked virtual process exits through guest code");
    }
    impl_->Finish(pid, ExitStatus(exit_code));
}

std::int32_t VirtualProcessKernel::Spawn(const std::int32_t parent, const VfsDescriptorTableId parent_table,
                                         const std::string_view path, std::vector<std::string> arguments,
                                         std::vector<std::string> environment, std::string working_directory,
                                         const std::function<void()>& arrange) {
    if (!HasCommand(path)) throw VirtualProcessError(kEnoent, "no such executable");
    const auto pid = Fork(parent, parent_table);
    try {
        if (arrange) {
            VfsThreadBinding binding(*impl_->config.vfs, DescriptorTable(pid));
            arrange();
        }
        Exec(pid, path, std::move(arguments), std::move(environment), std::move(working_directory));
    } catch (...) {
        Exit(pid, 127);
        static_cast<void>(Wait(parent, pid, false));
        throw;
    }
    return pid;
}

std::int32_t VirtualProcessKernel::SpawnCommand(const std::int32_t parent, const VfsDescriptorTableId parent_table,
                                                VirtualCommand command, std::vector<std::string> arguments,
                                                std::vector<std::string> environment, std::string working_directory,
                                                const std::function<void()>& arrange) {
    if (!command) throw VirtualProcessError(kEinval, "virtual command is empty");
    CheckArguments(arguments, environment);
    const auto pid = Fork(parent, parent_table);
    try {
        const auto table = DescriptorTable(pid);
        VfsThreadBinding binding(*impl_->config.vfs, table);
        if (arrange) arrange();
        impl_->config.vfs->CloseOnExecDescriptors();
    } catch (...) {
        Exit(pid, 127);
        static_cast<void>(Wait(parent, pid, false));
        throw;
    }
    auto name = arguments.empty() ? std::string("sh") : arguments.front();
    impl_->Start(pid, std::move(command), std::move(name), std::move(arguments), std::move(environment),
                 std::move(working_directory));
    return pid;
}

VirtualProcessWaitResult VirtualProcessKernel::Wait(const std::int32_t caller, const std::int32_t selector,
                                                    const bool no_hang, const std::function<bool()>& interrupted) {
    return impl_->Wait(caller, selector, no_hang, interrupted);
}

void VirtualProcessKernel::Kill(const std::int32_t pid, const std::int32_t signal) {
    if (signal < 0 || signal > 64) throw VirtualProcessError(kEinval, "invalid signal");
    std::scoped_lock lock(impl_->mutex);
    auto& process = impl_->FindLocked(pid);
    if (process.init) throw VirtualProcessError(kEperm, "init cannot be signalled");
    if (process.application) throw VirtualProcessError(kEperm, "the application handles its own signals");
    if (signal == 0 || process.phase == VirtualProcessPhase::zombie || IgnoredByDefault(signal)) return;
    if (process.phase == VirtualProcessPhase::forked) {
        // A vfork child runs guest code on its parent's thread; it cannot be
        // stopped from here. Exec or _exit still completes it.
        impl_->Record("process.kill.forked_child");
        return;
    }
    if (process.kill_signal == 0) process.kill_signal = signal;
    process.stop.request_stop();
}

bool VirtualProcessKernel::Exists(const std::int32_t pid) const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->processes.contains(pid);
}

void VirtualProcessKernel::RecordUnsupported(const std::string_view id) const {
    impl_->Record(id);
}

std::vector<VirtualProcessInfo> VirtualProcessKernel::Processes() const {
    std::vector<VirtualProcessInfo> result;
    {
        std::scoped_lock lock(impl_->mutex);
        for (const auto& [pid, process] : impl_->processes) {
            VirtualProcessInfo info;
            info.pid = pid;
            info.parent_pid = process.parent;
            info.uid = process.uid;
            info.name = process.name;
            info.phase = process.phase;
            info.application = process.application;
            const auto memory = process.init          ? kInitMemory
                                : process.application ? VirtualProcessMemory{}
                                                      : kCommandMemory;
            info.virtual_size_bytes = memory.virtual_size_bytes;
            info.resident_size_bytes = memory.resident_size_bytes;
            result.push_back(std::move(info));
        }
    }
    if (impl_->config.application_memory) {
        const auto memory = impl_->config.application_memory();
        for (auto& info : result) {
            if (!info.application) continue;
            info.virtual_size_bytes = memory.virtual_size_bytes;
            info.resident_size_bytes = memory.resident_size_bytes;
        }
    }
    return result;
}

}  // namespace ogplay::runtime

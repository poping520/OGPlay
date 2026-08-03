#include "ogplay/runtime/syscall.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace ogplay::runtime {
namespace {

struct Declaration final {
    std::uint32_t number{};
    const char* name{};
    SyscallGroup group{};
};

constexpr std::array kAndroidArmBaseline{
    Declaration{1, "exit", SyscallGroup::process},
    Declaration{3, "read", SyscallGroup::file},
    Declaration{4, "write", SyscallGroup::file},
    Declaration{5, "open", SyscallGroup::file},
    Declaration{6, "close", SyscallGroup::file},
    Declaration{10, "unlink", SyscallGroup::file},
    Declaration{19, "lseek", SyscallGroup::file},
    Declaration{20, "getpid", SyscallGroup::process},
    Declaration{33, "access", SyscallGroup::file},
    Declaration{39, "mkdir", SyscallGroup::file},
    Declaration{41, "dup", SyscallGroup::file},
    Declaration{42, "pipe", SyscallGroup::poll},
    Declaration{45, "brk", SyscallGroup::memory},
    Declaration{54, "ioctl", SyscallGroup::file},
    Declaration{55, "fcntl", SyscallGroup::file},
    Declaration{78, "gettimeofday", SyscallGroup::time},
    Declaration{85, "readlink", SyscallGroup::file},
    Declaration{91, "munmap", SyscallGroup::memory},
    Declaration{94, "fchmod", SyscallGroup::file},
    Declaration{95, "fchown", SyscallGroup::file},
    Declaration{108, "fstat", SyscallGroup::file},
    Declaration{122, "uname", SyscallGroup::process},
    Declaration{125, "mprotect", SyscallGroup::memory},
    Declaration{132, "getpgid", SyscallGroup::process},
    Declaration{140, "llseek", SyscallGroup::file},
    Declaration{141, "getdents", SyscallGroup::file},
    Declaration{145, "readv", SyscallGroup::file},
    Declaration{146, "writev", SyscallGroup::file},
    Declaration{158, "sched_yield", SyscallGroup::thread},
    Declaration{162, "nanosleep", SyscallGroup::time},
    Declaration{163, "mremap", SyscallGroup::memory},
    Declaration{168, "poll", SyscallGroup::poll},
    Declaration{172, "prctl", SyscallGroup::process},
    Declaration{174, "rt_sigaction", SyscallGroup::signal},
    Declaration{175, "rt_sigprocmask", SyscallGroup::signal},
    Declaration{180, "pread64", SyscallGroup::file},
    Declaration{181, "pwrite64", SyscallGroup::file},
    Declaration{183, "getcwd", SyscallGroup::file},
    Declaration{186, "sigaltstack", SyscallGroup::signal},
    Declaration{192, "mmap2", SyscallGroup::memory},
    Declaration{195, "stat64", SyscallGroup::file},
    Declaration{196, "lstat64", SyscallGroup::file},
    Declaration{197, "fstat64", SyscallGroup::file},
    Declaration{199, "getuid32", SyscallGroup::process},
    Declaration{200, "getgid32", SyscallGroup::process},
    Declaration{201, "geteuid32", SyscallGroup::process},
    Declaration{202, "getegid32", SyscallGroup::process},
    Declaration{221, "fcntl64", SyscallGroup::file},
    Declaration{224, "gettid", SyscallGroup::thread},
    Declaration{240, "futex", SyscallGroup::thread},
    Declaration{241, "sched_setaffinity", SyscallGroup::thread},
    Declaration{242, "sched_getaffinity", SyscallGroup::thread},
    Declaration{248, "exit_group", SyscallGroup::process},
    Declaration{250, "epoll_create", SyscallGroup::poll},
    Declaration{251, "epoll_ctl", SyscallGroup::poll},
    Declaration{252, "epoll_wait", SyscallGroup::poll},
    Declaration{256, "set_tid_address", SyscallGroup::thread},
    Declaration{263, "clock_gettime", SyscallGroup::time},
    Declaration{264, "clock_getres", SyscallGroup::time},
    Declaration{268, "tgkill", SyscallGroup::signal},
    Declaration{281, "socket", SyscallGroup::network},
    Declaration{283, "connect", SyscallGroup::network},
    Declaration{289, "send", SyscallGroup::network},
    Declaration{291, "recv", SyscallGroup::network},
    Declaration{322, "openat", SyscallGroup::file},
    Declaration{323, "mkdirat", SyscallGroup::file},
    Declaration{327, "fstatat64", SyscallGroup::file},
    Declaration{328, "unlinkat", SyscallGroup::file},
    Declaration{332, "readlinkat", SyscallGroup::file},
    Declaration{334, "faccessat", SyscallGroup::file},
    Declaration{336, "ppoll", SyscallGroup::poll},
    Declaration{346, "epoll_pwait", SyscallGroup::poll},
    Declaration{351, "eventfd", SyscallGroup::poll},
    Declaration{356, "eventfd2", SyscallGroup::poll},
    Declaration{357, "epoll_create1", SyscallGroup::poll},
    Declaration{358, "dup3", SyscallGroup::file},
    Declaration{359, "pipe2", SyscallGroup::poll},
    Declaration{384, "getrandom", SyscallGroup::process},
    Declaration{0x0f0002, "cacheflush", SyscallGroup::arm_private},
    Declaration{0x0f0005, "set_tls", SyscallGroup::arm_private},
};

}  // namespace

A32SyscallDispatcher::A32SyscallDispatcher(core::CapabilityLedger& ledger)
    : ledger_(ledger) {}

void A32SyscallDispatcher::Declare(const std::uint32_t number, std::string name,
                                   const SyscallGroup group) {
    if (name.empty()) throw SyscallError("syscall name is empty");
    const auto [iterator, inserted] = entries_.emplace(
        number, Entry{std::move(name), group, {}});
    static_cast<void>(iterator);
    if (!inserted) throw SyscallError("syscall number is already declared");
}

void A32SyscallDispatcher::Register(const std::uint32_t number, std::string name,
                                    const SyscallGroup group, Handler handler) {
    if (!handler) throw SyscallError("syscall handler is empty");
    Declare(number, std::move(name), group);
    entries_.at(number).handler = std::move(handler);
}

void A32SyscallDispatcher::Implement(const std::uint32_t number,
                                     Handler handler) {
    if (!handler) throw SyscallError("syscall handler is empty");
    const auto found = entries_.find(number);
    if (found == entries_.end()) {
        throw SyscallError("cannot implement an undeclared syscall");
    }
    if (found->second.handler) {
        throw SyscallError("syscall already has an implementation");
    }
    found->second.handler = std::move(handler);
}

std::int32_t A32SyscallDispatcher::Dispatch(const A32SyscallFrame& frame) {
    const auto found = entries_.find(frame.number);
    if (found == entries_.end()) {
        ledger_.RecordUnimplemented("syscall.arm." +
                                        std::to_string(frame.number),
                                    frame.link_register);
        return -kLinuxEnosys;
    }
    if (!found->second.handler) {
        ledger_.RecordUnimplemented("syscall." + found->second.name,
                                    frame.link_register);
        return -kLinuxEnosys;
    }
    return found->second.handler(frame);
}

SyscallCoverage A32SyscallDispatcher::Coverage() const {
    SyscallCoverage result;
    result.declared = entries_.size();
    for (const auto& [number, entry] : entries_) {
        static_cast<void>(number);
        ++result.declared_by_group[entry.group];
        if (entry.handler) {
            ++result.implemented;
            ++result.implemented_by_group[entry.group];
        }
    }
    return result;
}

A32SyscallDispatcher CreateAndroidArmSyscallDispatcher(
    core::CapabilityLedger& ledger, const AndroidProcessIdentity identity) {
    A32SyscallDispatcher result{ledger};
    for (const auto& declaration : kAndroidArmBaseline) {
        const auto constant = [value = identity.process_id](const A32SyscallFrame&) {
            return static_cast<std::int32_t>(value);
        };
        if (declaration.number == 20) {
            result.Register(declaration.number, declaration.name,
                            declaration.group, constant);
        } else if (declaration.number == 199 || declaration.number == 201) {
            result.Register(declaration.number, declaration.name,
                            declaration.group,
                            [value = identity.user_id](const A32SyscallFrame&) {
                                return static_cast<std::int32_t>(value);
                            });
        } else if (declaration.number == 200 || declaration.number == 202) {
            result.Register(declaration.number, declaration.name,
                            declaration.group,
                            [value = identity.group_id](const A32SyscallFrame&) {
                                return static_cast<std::int32_t>(value);
                            });
        } else if (declaration.number == 224) {
            result.Register(declaration.number, declaration.name,
                            declaration.group, [](const A32SyscallFrame& frame) {
                                return static_cast<std::int32_t>(frame.thread_id);
                            });
        } else {
            result.Declare(declaration.number, declaration.name,
                           declaration.group);
        }
    }
    return result;
}

void BindAndroidTimeSyscalls(A32SyscallDispatcher& dispatcher,
                             hal::Clock& clock,
                             memory::AddressSpace& address_space) {
    constexpr std::int32_t kEfault = 14;
    constexpr std::int32_t kEinval = 22;
    constexpr std::int32_t kEoverflow = 75;
    const auto frequency = clock.TicksPerSecond();
    if (frequency == 0 || frequency > UINT64_C(1000000000)) {
        throw SyscallError("clock frequency cannot be represented as nanoseconds");
    }
    const auto write32 = [&address_space](const memory::GuestAddress address,
                                          const std::uint32_t value) {
        std::array<std::byte, 4> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(
                (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
        }
        address_space.Write(address, bytes);
    };
    const auto current = [&clock, frequency]() {
        const auto ticks = clock.Ticks();
        const auto seconds = ticks / frequency;
        const auto remainder = ticks % frequency;
        const auto nanoseconds = remainder * UINT64_C(1000000000) / frequency;
        return std::pair{seconds, nanoseconds};
    };
    dispatcher.Implement(
        263, [&address_space, write32, current](const A32SyscallFrame& frame) {
            if (frame.arguments[0] != 0 && frame.arguments[0] != 1 &&
                frame.arguments[0] != 7) {
                return -kEinval;
            }
            const auto [seconds, nanoseconds] = current();
            if (seconds > static_cast<std::uint64_t>(
                              std::numeric_limits<std::int32_t>::max())) {
                return -kEoverflow;
            }
            try {
                const memory::GuestAddress output{frame.arguments[1]};
                address_space.Validate({output, 8}, memory::AccessType::write,
                                       frame.thread_id);
                write32(output, static_cast<std::uint32_t>(seconds));
                write32(output.Add(4), static_cast<std::uint32_t>(nanoseconds));
                return 0;
            } catch (const memory::MemoryFault&) {
                return -kEfault;
            }
        });
    dispatcher.Implement(
        78, [&address_space, write32, current](const A32SyscallFrame& frame) {
            const auto [seconds, nanoseconds] = current();
            if (seconds > static_cast<std::uint64_t>(
                              std::numeric_limits<std::int32_t>::max())) {
                return -kEoverflow;
            }
            try {
                const memory::GuestAddress output{frame.arguments[0]};
                address_space.Validate({output, 8}, memory::AccessType::write,
                                       frame.thread_id);
                write32(output, static_cast<std::uint32_t>(seconds));
                write32(output.Add(4),
                        static_cast<std::uint32_t>(nanoseconds / 1000U));
                if (frame.arguments[1] != 0) {
                    const memory::GuestAddress timezone{frame.arguments[1]};
                    address_space.Validate({timezone, 8},
                                           memory::AccessType::write,
                                           frame.thread_id);
                    write32(timezone, 0);
                    write32(timezone.Add(4), 0);
                }
                return 0;
            } catch (const memory::MemoryFault&) {
                return -kEfault;
            }
        });
}

}  // namespace ogplay::runtime

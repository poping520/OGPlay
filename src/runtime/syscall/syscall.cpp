#include "ogplay/runtime/syscall/syscall.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <thread>
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
    Declaration{77, "getrusage", SyscallGroup::process},
    Declaration{82, "select", SyscallGroup::poll},
    Declaration{83, "symlink", SyscallGroup::file},
    Declaration{85, "readlink", SyscallGroup::file},
    Declaration{86, "uselib", SyscallGroup::process},
    Declaration{91, "munmap", SyscallGroup::memory},
    Declaration{93, "ftruncate", SyscallGroup::file},
    Declaration{94, "fchmod", SyscallGroup::file},
    Declaration{95, "fchown", SyscallGroup::file},
    Declaration{96, "getpriority", SyscallGroup::process},
    Declaration{97, "setpriority", SyscallGroup::process},
    Declaration{99, "statfs", SyscallGroup::file},
    Declaration{100, "fstatfs", SyscallGroup::file},
    Declaration{104, "setitimer", SyscallGroup::time},
    Declaration{105, "getitimer", SyscallGroup::time},
    Declaration{108, "fstat", SyscallGroup::file},
    Declaration{114, "wait4", SyscallGroup::process},
    Declaration{116, "sysinfo", SyscallGroup::process},
    Declaration{120, "clone", SyscallGroup::thread},
    Declaration{121, "setdomainname", SyscallGroup::process},
    Declaration{122, "uname", SyscallGroup::process},
    Declaration{125, "mprotect", SyscallGroup::memory},
    Declaration{126, "sigprocmask", SyscallGroup::signal},
    Declaration{132, "getpgid", SyscallGroup::process},
    Declaration{133, "fchdir", SyscallGroup::file},
    Declaration{140, "llseek", SyscallGroup::file},
    Declaration{141, "getdents", SyscallGroup::file},
    Declaration{143, "flock", SyscallGroup::file},
    Declaration{145, "readv", SyscallGroup::file},
    Declaration{146, "writev", SyscallGroup::file},
    Declaration{148, "fdatasync", SyscallGroup::file},
    Declaration{150, "mlock", SyscallGroup::memory},
    Declaration{151, "munlock", SyscallGroup::memory},
    Declaration{152, "mlockall", SyscallGroup::memory},
    Declaration{153, "munlockall", SyscallGroup::memory},
    Declaration{158, "sched_yield", SyscallGroup::thread},
    Declaration{162, "nanosleep", SyscallGroup::time},
    Declaration{163, "mremap", SyscallGroup::memory},
    Declaration{168, "poll", SyscallGroup::poll},
    Declaration{172, "prctl", SyscallGroup::process},
    Declaration{174, "rt_sigaction", SyscallGroup::signal},
    Declaration{175, "rt_sigprocmask", SyscallGroup::signal},
    Declaration{176, "rt_sigpending", SyscallGroup::signal},
    Declaration{177, "rt_sigtimedwait", SyscallGroup::signal},
    Declaration{178, "rt_sigqueueinfo", SyscallGroup::signal},
    Declaration{179, "rt_sigsuspend", SyscallGroup::signal},
    Declaration{180, "pread64", SyscallGroup::file},
    Declaration{181, "pwrite64", SyscallGroup::file},
    Declaration{182, "chown", SyscallGroup::file},
    Declaration{183, "getcwd", SyscallGroup::file},
    Declaration{184, "capget", SyscallGroup::process},
    Declaration{185, "capset", SyscallGroup::process},
    Declaration{186, "sigaltstack", SyscallGroup::signal},
    Declaration{187, "sendfile", SyscallGroup::file},
    Declaration{190, "vfork", SyscallGroup::process},
    Declaration{191, "ugetrlimit", SyscallGroup::process},
    Declaration{192, "mmap2", SyscallGroup::memory},
    Declaration{195, "stat64", SyscallGroup::file},
    Declaration{196, "lstat64", SyscallGroup::file},
    Declaration{197, "fstat64", SyscallGroup::file},
    Declaration{199, "getuid32", SyscallGroup::process},
    Declaration{200, "getgid32", SyscallGroup::process},
    Declaration{201, "geteuid32", SyscallGroup::process},
    Declaration{202, "getegid32", SyscallGroup::process},
    Declaration{203, "setreuid32", SyscallGroup::process},
    Declaration{204, "setregid32", SyscallGroup::process},
    Declaration{205, "getgroups32", SyscallGroup::process},
    Declaration{209, "getresuid32", SyscallGroup::process},
    Declaration{211, "getresgid32", SyscallGroup::process},
    Declaration{217, "getdents64", SyscallGroup::file},
    Declaration{220, "madvise", SyscallGroup::memory},
    Declaration{221, "fcntl64", SyscallGroup::file},
    Declaration{224, "gettid", SyscallGroup::thread},
    Declaration{239, "sendfile64", SyscallGroup::file},
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

void A32SyscallDispatcher::SetObserver(Observer observer) {
    observer_ = std::move(observer);
}

std::int32_t A32SyscallDispatcher::Dispatch(const A32SyscallFrame& frame) {
    const auto found = entries_.find(frame.number);
    std::int32_t result{};
    if (found == entries_.end()) {
        ledger_.RecordUnimplemented("syscall.arm." +
                                        std::to_string(frame.number),
                                    frame.link_register);
        result = -kLinuxEnosys;
    } else if (!found->second.handler) {
        ledger_.RecordUnimplemented("syscall." + found->second.name,
                                    frame.link_register);
        result = -kLinuxEnosys;
    } else {
        result = found->second.handler(frame);
    }
    if (observer_) {
        std::scoped_lock lock(*observer_mutex_);
        observer_(frame, result);
    }
    return result;
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

void BindAndroidMemorySyscalls(A32SyscallDispatcher& dispatcher,
                               memory::AddressSpace& address_space) {
    constexpr std::int32_t kEperm = 1;
    constexpr std::int32_t kEnomem = 12;
    constexpr std::int32_t kEinval = 22;
    constexpr std::uint32_t kMapPrivate = 0x02;
    constexpr std::uint32_t kMapFixed = 0x10;
    constexpr std::uint32_t kMapAnonymous = 0x20;
    struct State final {
        std::uint32_t next_mapping{0x60000000};
        std::uint32_t current_break{0x50000000};
    };
    const auto state = std::make_shared<State>();
    const auto page_size = address_space.PageSize();
    const auto aligned_size = [page_size](const std::uint32_t size) {
        if (size == 0) throw std::invalid_argument("zero mapping size");
        const auto result = (static_cast<std::uint64_t>(size) + page_size - 1U) &
                            ~(page_size - 1U);
        if (result > std::numeric_limits<std::uint32_t>::max()) {
            throw std::overflow_error("mapping size overflows");
        }
        return static_cast<std::uint32_t>(result);
    };
    const auto protection = [](const std::uint32_t linux_protection) {
        if ((linux_protection & ~7U) != 0) {
            throw std::invalid_argument("unsupported Linux page protection");
        }
        auto result = memory::PageProtection::none;
        if ((linux_protection & 1U) != 0) {
            result = result | memory::PageProtection::read;
        }
        if ((linux_protection & 2U) != 0) {
            result = result | memory::PageProtection::read |
                     memory::PageProtection::write;
        }
        if ((linux_protection & 4U) != 0) {
            result = result | memory::PageProtection::execute;
        }
        if ((linux_protection & 2U) != 0 && (linux_protection & 4U) != 0) {
            throw std::domain_error("writable executable mapping");
        }
        return result;
    };

    dispatcher.Implement(
        192, [&address_space, state, page_size, aligned_size,
              protection](const A32SyscallFrame& frame) {
            try {
                const auto size = aligned_size(frame.arguments[1]);
                const auto flags = frame.arguments[3];
                if ((flags & kMapAnonymous) == 0 ||
                    (flags & kMapPrivate) == 0 || frame.arguments[5] != 0) {
                    return -kEinval;
                }
                std::uint32_t address{};
                if ((flags & kMapFixed) != 0) {
                    address = frame.arguments[0];
                    if (address % page_size != 0) return -kEinval;
                } else {
                    address = state->next_mapping;
                    const auto end = static_cast<std::uint64_t>(address) + size;
                    if (end > UINT32_MAX) return -kEnomem;
                    state->next_mapping = static_cast<std::uint32_t>(end);
                }
                address_space.Map({memory::GuestAddress{address}, size},
                                  protection(frame.arguments[2]));
                return std::bit_cast<std::int32_t>(address);
            } catch (const std::domain_error&) {
                return -kEperm;
            } catch (const std::invalid_argument&) {
                return -kEinval;
            } catch (const std::overflow_error&) {
                return -kEinval;
            } catch (const std::exception&) {
                return -kEnomem;
            }
        });
    dispatcher.Implement(
        91, [&address_space, page_size,
             aligned_size](const A32SyscallFrame& frame) {
            try {
                if (frame.arguments[0] % page_size != 0) return -kEinval;
                address_space.Unmap(
                    {memory::GuestAddress{frame.arguments[0]},
                     aligned_size(frame.arguments[1])});
                return 0;
            } catch (const std::exception&) {
                return -kEinval;
            }
        });
    dispatcher.Implement(
        125, [&address_space, page_size, aligned_size,
              protection](const A32SyscallFrame& frame) {
            try {
                if (frame.arguments[0] % page_size != 0) return -kEinval;
                address_space.Protect(
                    {memory::GuestAddress{frame.arguments[0]},
                     aligned_size(frame.arguments[1])},
                    protection(frame.arguments[2]));
                return 0;
            } catch (const std::domain_error&) {
                return -kEperm;
            } catch (const std::exception&) {
                return -kEinval;
            }
        });
    dispatcher.Implement(
        220, [&address_space, page_size,
              aligned_size](const A32SyscallFrame& frame) {
            constexpr std::uint32_t kMadvDontneed = 4;
            const auto supported_advice = [](const std::uint32_t advice) {
                return advice <= kMadvDontneed ||
                       (advice >= 12U && advice <= 15U);
            };
            if (frame.arguments[0] % page_size != 0 ||
                !supported_advice(frame.arguments[2])) {
                return -kEinval;
            }
            try {
                const auto size = aligned_size(frame.arguments[1]);
                const memory::GuestRange range{
                    memory::GuestAddress{frame.arguments[0]}, size};
                const auto access = frame.arguments[2] == kMadvDontneed
                                        ? memory::AccessType::write
                                        : memory::AccessType::read;
                address_space.Validate(range, access, frame.thread_id);
                if (frame.arguments[2] != kMadvDontneed) return 0;

                const std::array<std::byte, 4096> zeroes{};
                std::uint64_t offset{};
                while (offset < size) {
                    const auto count = static_cast<std::size_t>(
                        std::min<std::uint64_t>(zeroes.size(), size - offset));
                    address_space.Write(range.Start().Add(offset),
                                        std::span{zeroes}.first(count),
                                        frame.thread_id);
                    offset += count;
                }
                return 0;
            } catch (const memory::MemoryFault&) {
                return -kEnomem;
            } catch (const std::exception&) {
                return -kEinval;
            }
        });
    dispatcher.Implement(
        45, [&address_space, state, page_size](const A32SyscallFrame& frame) {
            const auto requested = frame.arguments[0];
            if (requested == 0) {
                return std::bit_cast<std::int32_t>(state->current_break);
            }
            if (requested < 0x50000000 || requested >= 0x58000000) {
                return std::bit_cast<std::int32_t>(state->current_break);
            }
            const auto old_page = (static_cast<std::uint64_t>(state->current_break) +
                                   page_size - 1U) & ~(page_size - 1U);
            const auto new_page = (static_cast<std::uint64_t>(requested) +
                                   page_size - 1U) & ~(page_size - 1U);
            try {
                if (new_page > old_page) {
                    address_space.Map(
                        {memory::GuestAddress{static_cast<std::uint32_t>(old_page)},
                         new_page - old_page},
                        memory::PageProtection::read |
                            memory::PageProtection::write);
                } else if (new_page < old_page) {
                    address_space.Unmap(
                        {memory::GuestAddress{static_cast<std::uint32_t>(new_page)},
                         old_page - new_page});
                }
                state->current_break = requested;
            } catch (const std::exception&) {
            }
            return std::bit_cast<std::int32_t>(state->current_break);
        });
}

void BindAndroidThreadSyscalls(A32SyscallDispatcher& dispatcher,
                               cpu::FutexTable& futex_table,
                               memory::MemoryBus& memory_bus) {
    constexpr std::int32_t kEagain = 11;
    constexpr std::int32_t kEfault = 14;
    constexpr std::int32_t kEinval = 22;
    constexpr std::int32_t kEnotsup = 95;
    constexpr std::uint32_t kFutexWait = 0;
    constexpr std::uint32_t kFutexWake = 1;
    constexpr std::uint32_t kFutexPrivateFlag = 128;
    constexpr std::uint32_t kFutexClockRealtime = 256;
    dispatcher.Implement(
        240, [&futex_table, &memory_bus](const A32SyscallFrame& frame) {
            const memory::GuestAddress address{frame.arguments[0]};
            if (!address.IsAligned(4)) return -kEinval;
            const auto operation = frame.arguments[1];
            if ((operation & kFutexClockRealtime) != 0) return -kEnotsup;
            const auto command = operation & ~kFutexPrivateFlag;
            try {
                if (command == kFutexWait) {
                    if (frame.arguments[3] != 0) return -kEnotsup;
                    const auto result = futex_table.Wait(
                        memory_bus, address, frame.arguments[2], frame.thread_id);
                    return result == cpu::FutexWaitResult::awoken ? 0 : -kEagain;
                }
                if (command == kFutexWake) {
                    const auto count = futex_table.Wake(address, frame.arguments[2]);
                    if (count > static_cast<std::size_t>(
                                    std::numeric_limits<std::int32_t>::max())) {
                        return std::numeric_limits<std::int32_t>::max();
                    }
                    return static_cast<std::int32_t>(count);
                }
                return -kLinuxEnosys;
            } catch (const memory::MemoryFault&) {
                return -kEfault;
            } catch (const std::invalid_argument&) {
                return -kEinval;
            }
        });
    dispatcher.Implement(158, [](const A32SyscallFrame&) {
        std::this_thread::yield();
        return 0;
    });
}

void BindAndroidFileSyscalls(A32SyscallDispatcher& dispatcher,
                             VirtualFileSystem& vfs,
                             memory::AddressSpace& address_space) {
    constexpr std::int32_t kEfault = 14;
    constexpr std::int32_t kEinval = 22;
    constexpr std::int32_t kEnametoolong = 36;
    constexpr std::int32_t kEoverflow = 75;
    constexpr std::int32_t kEnotsup = 95;
    constexpr std::uint32_t kMaxIoSize = 16U * 1024U * 1024U;
    const auto read_path = [&address_space](const std::uint32_t raw_address) {
        std::string path;
        path.reserve(128);
        auto address = memory::GuestAddress{raw_address};
        for (std::size_t index = 0; index < 4096; ++index) {
            std::array<std::byte, 1> byte{};
            address_space.Read(address, byte);
            const auto value = std::to_integer<std::uint8_t>(byte[0]);
            if (value == 0) return path;
            path.push_back(static_cast<char>(value));
            address = address.Add(1);
        }
        throw VfsError(kEnametoolong, "guest path is not null-terminated");
    };
    const auto options = [](const std::uint32_t flags) {
        constexpr std::uint32_t kCreate = 0x40;
        constexpr std::uint32_t kTruncate = 0x200;
        constexpr std::uint32_t kNoFollow = 0x8000;
        constexpr std::uint32_t kLargeFile = 0x20000;
        constexpr std::uint32_t kCloseOnExec = 0x80000;
        constexpr std::uint32_t kKnown = 3 | kCreate | kTruncate |
                                         kNoFollow | kLargeFile | kCloseOnExec;
        if ((flags & ~kKnown) != 0 || (flags & 3U) == 3U) {
            throw VfsError(kEinval, "unsupported Android open flags");
        }
        const auto access = flags & 3U;
        return VfsOpenOptions{access == 0 || access == 2,
                              access == 1 || access == 2,
                              (flags & kCreate) != 0,
                              (flags & kTruncate) != 0};
    };
    const auto open = [&vfs, read_path, options](const std::uint32_t path,
                                                 const std::uint32_t flags) {
        try {
            const auto guest_path = read_path(path);
            const auto open_options = options(flags);
            return vfs.Open(guest_path, open_options);
        } catch (const memory::MemoryFault&) {
            return -kEfault;
        } catch (const std::overflow_error&) {
            return -kEfault;
        } catch (const VfsError& error) {
            return -error.ErrorNumber();
        }
    };
    dispatcher.Implement(5, [open](const A32SyscallFrame& frame) {
        return open(frame.arguments[0], frame.arguments[1]);
    });
    dispatcher.Implement(
        322, [open, read_path](const A32SyscallFrame& frame) {
            try {
                const auto path = read_path(frame.arguments[1]);
                const auto absolute = !path.empty() && path.front() == '/';
                if (!absolute && frame.arguments[0] !=
                                     std::bit_cast<std::uint32_t>(-100)) {
                    return -kEnotsup;
                }
                if (!absolute) return -kEnotsup;
            } catch (const memory::MemoryFault&) {
                return -kEfault;
            } catch (const std::overflow_error&) {
                return -kEfault;
            } catch (const VfsError& error) {
                return -error.ErrorNumber();
            }
            return open(frame.arguments[1], frame.arguments[2]);
        });
    dispatcher.Implement(
        3, [&vfs, &address_space](const A32SyscallFrame& frame) {
            const auto count = frame.arguments[2];
            if (count > kMaxIoSize) return -kEinval;
            try {
                if (count == 0) return 0;
                const memory::GuestAddress destination{frame.arguments[1]};
                address_space.Validate({destination, count},
                                       memory::AccessType::write,
                                       frame.thread_id);
                std::vector<std::byte> bytes(count);
                const auto actual =
                    vfs.Read(std::bit_cast<std::int32_t>(frame.arguments[0]), bytes);
                address_space.Write(destination,
                                    std::span<const std::byte>(bytes).first(actual),
                                    frame.thread_id);
                return static_cast<std::int32_t>(actual);
            } catch (const memory::MemoryFault&) {
                return -kEfault;
            } catch (const VfsError& error) {
                return -error.ErrorNumber();
            }
        });
    dispatcher.Implement(
        4, [&vfs, &address_space](const A32SyscallFrame& frame) {
            const auto count = frame.arguments[2];
            if (count > kMaxIoSize) return -kEinval;
            try {
                if (count == 0) return 0;
                const memory::GuestAddress source{frame.arguments[1]};
                std::vector<std::byte> bytes(count);
                address_space.Read(source, bytes, frame.thread_id);
                const auto actual = vfs.Write(
                    std::bit_cast<std::int32_t>(frame.arguments[0]), bytes);
                return static_cast<std::int32_t>(actual);
            } catch (const memory::MemoryFault&) {
                return -kEfault;
            } catch (const VfsError& error) {
                return -error.ErrorNumber();
            }
        });
    dispatcher.Implement(
        42, [&vfs, &address_space](const A32SyscallFrame& frame) {
            const memory::GuestAddress output{frame.arguments[0]};
            try {
                address_space.Validate({output, 8}, memory::AccessType::write,
                                       frame.thread_id);
                const auto descriptors = vfs.CreatePipe();
                std::array<std::byte, 8> bytes{};
                const std::array values{
                    static_cast<std::uint32_t>(descriptors.read_descriptor),
                    static_cast<std::uint32_t>(descriptors.write_descriptor)};
                for (std::size_t word = 0; word < values.size(); ++word) {
                    for (std::size_t byte = 0; byte < 4; ++byte) {
                        bytes[word * 4U + byte] = static_cast<std::byte>(
                            (values[word] >> static_cast<unsigned>(byte * 8U)) &
                            0xffU);
                    }
                }
                try {
                    address_space.Write(output, bytes, frame.thread_id);
                } catch (...) {
                    vfs.Close(descriptors.read_descriptor);
                    vfs.Close(descriptors.write_descriptor);
                    throw;
                }
                return 0;
            } catch (const memory::MemoryFault&) {
                return -kEfault;
            } catch (const VfsError& error) {
                return -error.ErrorNumber();
            }
        });
    dispatcher.Implement(6, [&vfs](const A32SyscallFrame& frame) {
        try {
            vfs.Close(std::bit_cast<std::int32_t>(frame.arguments[0]));
            return 0;
        } catch (const VfsError& error) {
            return -error.ErrorNumber();
        }
    });
    dispatcher.Implement(19, [&vfs](const A32SyscallFrame& frame) {
        try {
            if (frame.arguments[2] > 2) return -kEinval;
            const auto result = vfs.Seek(
                std::bit_cast<std::int32_t>(frame.arguments[0]),
                std::bit_cast<std::int32_t>(frame.arguments[1]),
                static_cast<VfsSeekWhence>(frame.arguments[2]));
            if (result > static_cast<std::uint64_t>(
                             std::numeric_limits<std::int32_t>::max())) {
                return -kEoverflow;
            }
            return static_cast<std::int32_t>(result);
        } catch (const VfsError& error) {
            return -error.ErrorNumber();
        }
    });
}

}  // namespace ogplay::runtime

// File metadata and directory syscalls (SBX-4, ADR-0020 design 04 §2).
// These were the ones a native save flow reaches for and found -ENOSYS:
// mkdir before writing, stat to check a slot, getdents to list saves,
// unlink/rename to rotate them, fsync to commit.
//
// struct stat64 and linux_dirent64 follow the Android ARM EABI layout; the
// offsets are locked by machine test rather than trusted to a comment.

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/syscall/syscall.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {
namespace {

constexpr std::int32_t kEfault = 14;
constexpr std::int32_t kEinval = 22;
constexpr std::int32_t kEnametoolong = 36;
constexpr std::int32_t kEnotsup = 95;
constexpr std::int32_t kEacces = 13;
constexpr std::int32_t kEnoent = 2;
constexpr std::uint32_t kAtFdCwd = std::bit_cast<std::uint32_t>(-100);
constexpr std::uint32_t kAtRemoveDir = 0x200;
constexpr std::uint32_t kMaxIoSize = 16U * 1024U * 1024U;

// Android ARM stat64 is *not* packed: the ARM EABI aligns every 64-bit member
// to eight bytes, which pads the struct out to 104 bytes and pushes st_size
// past the __pad3 field. The guest libc proves it: __swhatbuf reads st_mode
// at 16 and st_blksize at 56 out of a 104-byte frame. Using the packed x86
// offsets here hands the guest a 64-bit st_size whose high word is our
// st_blksize, so every fopen()+fread() sees a terabyte-sized file.
constexpr std::size_t kStat64Size = 104;
constexpr std::size_t kStat64LegacyInoOffset = 12;
constexpr std::size_t kStat64ModeOffset = 16;
constexpr std::size_t kStat64LinkCountOffset = 20;
constexpr std::size_t kStat64SizeOffset = 48;
constexpr std::size_t kStat64BlockSizeOffset = 56;
constexpr std::size_t kStat64BlocksOffset = 64;
constexpr std::size_t kStat64InoOffset = 96;
constexpr std::uint32_t kModeDirectory = 0040000;
constexpr std::uint32_t kModeRegular = 0100000;

// linux_dirent64: u64 ino, s64 off, u16 reclen, u8 type, char name[].
constexpr std::size_t kDirentHeaderSize = 19;
constexpr std::uint8_t kDirentTypeDirectory = 4;
constexpr std::uint8_t kDirentTypeRegular = 8;

void PutLittleEndian(const std::span<std::byte> out, const std::size_t offset,
                     const std::uint64_t value, const std::size_t width) {
    for (std::size_t index = 0; index < width; ++index) {
        out[offset + index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
}

[[nodiscard]] std::array<std::byte, kStat64Size> EncodeStat64(
    const VfsFileInfo& info) {
    std::array<std::byte, kStat64Size> bytes{};
    // Permission bits come from the VFS writable fact rather than being
    // invented; timestamps stay 0 because the only time source is the
    // unified Clock and the VFS does not carry one (04 §2).
    std::uint32_t mode = info.is_directory ? kModeDirectory : kModeRegular;
    mode |= 0444U;
    if (info.writable) mode |= 0222U;
    if (info.is_directory) mode |= 0111U;
    PutLittleEndian(bytes, kStat64ModeOffset, mode, 4);
    PutLittleEndian(bytes, kStat64LinkCountOffset, 1, 4);
    PutLittleEndian(bytes, kStat64SizeOffset, info.size, 8);
    PutLittleEndian(bytes, kStat64BlockSizeOffset, 4096, 4);
    PutLittleEndian(bytes, kStat64BlocksOffset, (info.size + 511U) / 512U, 8);
    // Inode numbers are not modelled; a stable non-zero value keeps callers
    // that only test for "has an inode" honest without inventing identity.
    PutLittleEndian(bytes, kStat64InoOffset, 1, 8);
    PutLittleEndian(bytes, kStat64LegacyInoOffset, 1, 4);
    return bytes;
}

}  // namespace

void BindAndroidFileMetadataSyscalls(A32SyscallDispatcher& dispatcher,
                                     VirtualFileSystem& vfs,
                                     memory::AddressSpace& address_space) {
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

    // Every binding funnels its failures through one mapping, so guests see
    // the same errno whichever call they used.
    const auto guarded = [](auto action) {
        return [action](const A32SyscallFrame& frame) {
            try {
                return action(frame);
            } catch (const memory::MemoryFault&) {
                return -kEfault;
            } catch (const std::overflow_error&) {
                return -kEfault;
            } catch (const VfsError& error) {
                return -error.ErrorNumber();
            }
        };
    };

    // Relative paths would need a real per-process cwd for *at() calls;
    // saying so beats resolving them against the wrong directory.
    const auto absolute_at = [read_path](const std::uint32_t directory_fd,
                                         const std::uint32_t raw_path) {
        auto path = read_path(raw_path);
        if (path.empty() || path.front() != '/') {
            if (directory_fd != kAtFdCwd) {
                throw VfsError(kEnotsup,
                               "relative *at() paths are not implemented");
            }
            throw VfsError(kEnotsup,
                           "relative *at() paths are not implemented");
        }
        return path;
    };

    const auto write_stat = [&vfs, &address_space](
                                const VfsFileInfo& info,
                                const std::uint32_t output,
                                const std::uint64_t thread_id) {
        static_cast<void>(vfs);
        const memory::GuestAddress destination{output};
        address_space.Validate({destination, kStat64Size},
                               memory::AccessType::write, thread_id);
        const auto bytes = EncodeStat64(info);
        address_space.Write(destination, bytes, thread_id);
        return 0;
    };

    // mkdir(path, mode) / mkdirat(dirfd, path, mode); mode has no meaning
    // without permission semantics.
    dispatcher.Implement(39, guarded([&vfs, read_path](
                                         const A32SyscallFrame& frame) {
        vfs.CreateDirectory(read_path(frame.arguments[0]));
        return 0;
    }));
    dispatcher.Implement(323, guarded([&vfs, absolute_at](
                                          const A32SyscallFrame& frame) {
        vfs.CreateDirectory(absolute_at(frame.arguments[0],
                                        frame.arguments[1]));
        return 0;
    }));
    dispatcher.Implement(40, guarded([&vfs, read_path](
                                         const A32SyscallFrame& frame) {
        vfs.RemoveDirectory(read_path(frame.arguments[0]));
        return 0;
    }));
    dispatcher.Implement(10, guarded([&vfs, read_path](
                                         const A32SyscallFrame& frame) {
        vfs.RemoveFile(read_path(frame.arguments[0]));
        return 0;
    }));
    dispatcher.Implement(328, guarded([&vfs, absolute_at](
                                          const A32SyscallFrame& frame) {
        const auto path = absolute_at(frame.arguments[0], frame.arguments[1]);
        if ((frame.arguments[2] & kAtRemoveDir) != 0) {
            vfs.RemoveDirectory(path);
        } else {
            vfs.RemoveFile(path);
        }
        return 0;
    }));
    dispatcher.Implement(38, guarded([&vfs, read_path](
                                         const A32SyscallFrame& frame) {
        vfs.Rename(read_path(frame.arguments[0]),
                   read_path(frame.arguments[1]));
        return 0;
    }));
    dispatcher.Implement(329, guarded([&vfs, absolute_at](
                                          const A32SyscallFrame& frame) {
        vfs.Rename(absolute_at(frame.arguments[0], frame.arguments[1]),
                   absolute_at(frame.arguments[2], frame.arguments[3]));
        return 0;
    }));

    // stat64 / lstat64: no symbolic links exist in this filesystem, so the
    // two are the same answer rather than a pretend distinction.
    const auto stat_path = guarded([&vfs, read_path, write_stat](
                                       const A32SyscallFrame& frame) {
        const auto info = vfs.Stat(read_path(frame.arguments[0]));
        return write_stat(info, frame.arguments[1], frame.thread_id);
    });
    dispatcher.Implement(195, stat_path);
    dispatcher.Implement(196, stat_path);
    dispatcher.Implement(327, guarded([&vfs, absolute_at, write_stat](
                                          const A32SyscallFrame& frame) {
        const auto info =
            vfs.Stat(absolute_at(frame.arguments[0], frame.arguments[1]));
        return write_stat(info, frame.arguments[2], frame.thread_id);
    }));
    dispatcher.Implement(197, guarded([&vfs, write_stat](
                                          const A32SyscallFrame& frame) {
        const auto descriptor = std::bit_cast<std::int32_t>(
            frame.arguments[0]);
        return write_stat(vfs.DescriptorInfo(descriptor), frame.arguments[1],
                          frame.thread_id);
    }));

    // access(path, mode) / faccessat: existence plus the real writable fact.
    const auto access_check = [&vfs](const std::string& path,
                                     const std::uint32_t mode) {
        constexpr std::uint32_t kWriteOk = 2;
        constexpr std::uint32_t kExecuteOk = 1;
        const auto info = vfs.Stat(path);
        if ((mode & kWriteOk) != 0 && !info.writable) return -kEacces;
        if ((mode & kExecuteOk) != 0 && !info.is_directory) return -kEacces;
        return 0;
    };
    dispatcher.Implement(33, guarded([read_path, access_check](
                                         const A32SyscallFrame& frame) {
        return access_check(read_path(frame.arguments[0]),
                            frame.arguments[1]);
    }));
    dispatcher.Implement(334, guarded([absolute_at, access_check](
                                          const A32SyscallFrame& frame) {
        return access_check(absolute_at(frame.arguments[0],
                                        frame.arguments[1]),
                            frame.arguments[2]);
    }));

    const auto truncate = guarded([&vfs](const A32SyscallFrame& frame) {
        vfs.Truncate(std::bit_cast<std::int32_t>(frame.arguments[0]),
                     frame.arguments[1]);
        return 0;
    });
    dispatcher.Implement(93, truncate);
    // ftruncate64 takes the length as a 64-bit pair in r2:r3 (r1 is padding
    // for the even-register alignment rule).
    dispatcher.Implement(194, guarded([&vfs](const A32SyscallFrame& frame) {
        const auto length = static_cast<std::uint64_t>(frame.arguments[2]) |
                            (static_cast<std::uint64_t>(frame.arguments[3])
                             << 32U);
        vfs.Truncate(std::bit_cast<std::int32_t>(frame.arguments[0]), length);
        return 0;
    }));

    const auto flush = guarded([&vfs](const A32SyscallFrame& frame) {
        vfs.Flush(std::bit_cast<std::int32_t>(frame.arguments[0]));
        return 0;
    });
    dispatcher.Implement(118, flush);  // fsync
    dispatcher.Implement(148, flush);  // fdatasync

    // pread64/pwrite64 must not disturb the descriptor offset.
    dispatcher.Implement(180, guarded([&vfs, &address_space](
                                          const A32SyscallFrame& frame) {
        const auto count = frame.arguments[2];
        if (count > kMaxIoSize) return -kEinval;
        const auto descriptor = std::bit_cast<std::int32_t>(
            frame.arguments[0]);
        const auto offset = static_cast<std::uint64_t>(frame.arguments[4]) |
                            (static_cast<std::uint64_t>(frame.arguments[5])
                             << 32U);
        const memory::GuestAddress destination{frame.arguments[1]};
        address_space.Validate({destination, count},
                               memory::AccessType::write, frame.thread_id);
        const auto saved = vfs.Seek(descriptor, 0, VfsSeekWhence::current);
        static_cast<void>(vfs.Seek(
            descriptor, static_cast<std::int64_t>(offset),
            VfsSeekWhence::begin));
        std::vector<std::byte> bytes(count);
        const auto actual = vfs.Read(descriptor, bytes);
        static_cast<void>(vfs.Seek(descriptor,
                                   static_cast<std::int64_t>(saved),
                                   VfsSeekWhence::begin));
        if (actual != 0) {
            address_space.Write(destination,
                                std::span<const std::byte>(bytes).first(actual),
                                frame.thread_id);
        }
        return static_cast<std::int32_t>(actual);
    }));
    dispatcher.Implement(181, guarded([&vfs, &address_space](
                                          const A32SyscallFrame& frame) {
        const auto count = frame.arguments[2];
        if (count > kMaxIoSize) return -kEinval;
        const auto descriptor = std::bit_cast<std::int32_t>(
            frame.arguments[0]);
        const auto offset = static_cast<std::uint64_t>(frame.arguments[4]) |
                            (static_cast<std::uint64_t>(frame.arguments[5])
                             << 32U);
        std::vector<std::byte> bytes(count);
        if (count != 0) {
            address_space.Read(memory::GuestAddress{frame.arguments[1]}, bytes,
                               frame.thread_id);
        }
        const auto saved = vfs.Seek(descriptor, 0, VfsSeekWhence::current);
        static_cast<void>(vfs.Seek(
            descriptor, static_cast<std::int64_t>(offset),
            VfsSeekWhence::begin));
        const auto actual = vfs.Write(descriptor, bytes);
        static_cast<void>(vfs.Seek(descriptor,
                                   static_cast<std::int64_t>(saved),
                                   VfsSeekWhence::begin));
        return static_cast<std::int32_t>(actual);
    }));

    // getdents64(fd, dirp, count): fills whole records only, so a caller
    // with a small buffer pages instead of receiving a truncated entry.
    dispatcher.Implement(217, guarded([&vfs, &address_space](
                                          const A32SyscallFrame& frame) {
        const auto capacity = frame.arguments[2];
        if (capacity > kMaxIoSize) return -kEinval;
        const auto descriptor = std::bit_cast<std::int32_t>(
            frame.arguments[0]);
        std::vector<std::byte> out;
        while (true) {
            const auto page = vfs.ReadDirectory(descriptor, 1);
            if (page.empty()) break;
            const auto& entry = page.front();
            const auto raw = kDirentHeaderSize + entry.name.size() + 1U;
            const auto record = (raw + 7U) & ~std::size_t{7};
            if (out.size() + record > capacity) {
                // ReadDirectory advances one snapshot entry at a time. Put
                // this complete record back for the next getdents64 page.
                static_cast<void>(vfs.Seek(descriptor, -1,
                                           VfsSeekWhence::current));
                if (out.empty()) return -kEinval;  // buffer too small for one
                break;
            }
            const auto base = out.size();
            out.resize(base + record);
            const std::span<std::byte> span(out);
            PutLittleEndian(span, base + 0, 1, 8);            // d_ino
            PutLittleEndian(span, base + 8, base + record, 8);  // d_off
            PutLittleEndian(span, base + 16, record, 2);      // d_reclen
            span[base + 18] = static_cast<std::byte>(
                entry.is_directory ? kDirentTypeDirectory : kDirentTypeRegular);
            for (std::size_t index = 0; index < entry.name.size(); ++index) {
                span[base + kDirentHeaderSize + index] =
                    static_cast<std::byte>(entry.name[index]);
            }
        }
        if (out.empty()) return 0;  // end of directory
        const memory::GuestAddress destination{frame.arguments[1]};
        address_space.Validate({destination,
                                static_cast<std::uint32_t>(out.size())},
                               memory::AccessType::write, frame.thread_id);
        address_space.Write(destination, out, frame.thread_id);
        return static_cast<std::int32_t>(out.size());
    }));
    static_cast<void>(kEnoent);
}

}  // namespace ogplay::runtime

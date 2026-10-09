#include "ogplay/hal/fs.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <limits>
#include <system_error>

namespace ogplay::hal {
namespace {
class OwnedHandle final {
public:
    explicit OwnedHandle(HANDLE value) : value_(value) {
        if (value == INVALID_HANDLE_VALUE || value == nullptr)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "host file handle");
    }
    ~OwnedHandle() { CloseHandle(value_); }
    OwnedHandle(const OwnedHandle&) = delete;
    OwnedHandle& operator=(const OwnedHandle&) = delete;
    [[nodiscard]] HANDLE Get() const noexcept { return value_; }
private:
    HANDLE value_;
};

class WindowsReadFile final : public HostReadFile {
public:
    explicit WindowsReadFile(const std::filesystem::path& path)
        : handle_(CreateFileW(path.c_str(), GENERIC_READ,
              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
              OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) {
        BY_HANDLE_FILE_INFORMATION native{};
        if (!GetFileInformationByHandle(handle_.Get(), &native))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "host file identity");
        if ((native.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0 ||
            GetFileType(handle_.Get()) != FILE_TYPE_DISK)
            throw std::system_error(std::make_error_code(std::errc::invalid_argument), "host backing must be a regular file");
        info_ = {{native.dwVolumeSerialNumber,
                  (static_cast<std::uint64_t>(native.nFileIndexHigh) << 32U) | native.nFileIndexLow, 0},
                 (static_cast<std::uint64_t>(native.nFileSizeHigh) << 32U) | native.nFileSizeLow};
    }
    HostReadFileInfo Info() const noexcept override { return info_; }
    std::size_t ReadAt(std::uint64_t offset, std::span<std::byte> output,
                       std::stop_token stop) const override {
        if (offset > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) ||
            output.size() > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) - offset)
            throw std::system_error(std::make_error_code(std::errc::value_too_large), "host read offset");
        if (output.empty()) return 0;
        OwnedHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        std::size_t total{};
        while (total < output.size()) {
            if (stop.stop_requested())
                throw std::system_error(std::make_error_code(std::errc::operation_canceled), "host read cancelled");
            OVERLAPPED operation{};
            operation.Offset = static_cast<DWORD>(offset);
            operation.OffsetHigh = static_cast<DWORD>(offset >> 32U);
            operation.hEvent = event.Get();
            ResetEvent(event.Get());
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(output.size() - total, 65536U));
            DWORD count{};
            if (!ReadFile(handle_.Get(), output.data() + total, chunk, &count, &operation)) {
                const auto error = GetLastError();
                if (error == ERROR_HANDLE_EOF) break;
                if (error != ERROR_IO_PENDING)
                    throw std::system_error(static_cast<int>(error), std::system_category(), "host read");
                // Keep operation/buffer/event alive until this read completes.
                if (!GetOverlappedResult(handle_.Get(), &operation, &count, TRUE)) {
                    const auto completion_error = GetLastError();
                    if (completion_error == ERROR_HANDLE_EOF) break;
                    throw std::system_error(static_cast<int>(completion_error), std::system_category(), "host read completion");
                }
            }
            total += count;
            offset += count;
            if (count < chunk) break;
        }
        return total;
    }
private:
    OwnedHandle handle_;
    HostReadFileInfo info_;
};
} // namespace

std::shared_ptr<const HostReadFile> OpenHostReadFile(const std::filesystem::path& path) {
    return std::make_shared<WindowsReadFile>(path);
}
} // namespace ogplay::hal

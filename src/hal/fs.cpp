#include "ogplay/hal/fs.h"

#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace ogplay::hal {
namespace {

class StandardHostFileSystem final : public HostFileSystem {
public:
    std::optional<HostFileInfo> Status(
        const std::filesystem::path& path) const override {
        std::error_code error;
        const auto status = std::filesystem::status(path, error);
        if (error == std::errc::no_such_file_or_directory ||
            status.type() == std::filesystem::file_type::not_found) {
            return std::nullopt;
        }
        if (error) throw std::filesystem::filesystem_error("status", path, error);
        if (std::filesystem::is_regular_file(status)) {
            const auto size = std::filesystem::file_size(path, error);
            if (error) {
                throw std::filesystem::filesystem_error("file_size", path, error);
            }
            return HostFileInfo{HostFileType::regular, size};
        }
        if (std::filesystem::is_directory(status)) {
            return HostFileInfo{HostFileType::directory, 0};
        }
        return HostFileInfo{HostFileType::other, 0};
    }

    std::vector<std::byte> ReadFile(
        const std::filesystem::path& path) const override {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) throw std::runtime_error("failed to open host file for reading");
        const auto end = stream.tellg();
        if (end < 0) throw std::runtime_error("failed to query host file size");
        const auto size = static_cast<std::uintmax_t>(end);
        if (size > std::numeric_limits<std::size_t>::max() ||
            size > static_cast<std::uintmax_t>(
                       std::numeric_limits<std::streamsize>::max())) {
            throw std::length_error("host file is too large to read");
        }
        std::vector<std::byte> contents(static_cast<std::size_t>(size));
        stream.seekg(0, std::ios::beg);
        if (!contents.empty()) {
            stream.read(reinterpret_cast<char*>(contents.data()),
                        static_cast<std::streamsize>(contents.size()));
        }
        if (!stream) throw std::runtime_error("failed to read complete host file");
        return contents;
    }

    void WriteFile(const std::filesystem::path& path,
                   const std::span<const std::byte> contents) override {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("failed to open host file for writing");
        if (contents.size() >
            static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
            throw std::length_error("host file contents are too large to write");
        }
        if (!contents.empty()) {
            stream.write(reinterpret_cast<const char*>(contents.data()),
                         static_cast<std::streamsize>(contents.size()));
        }
        if (!stream) throw std::runtime_error("failed to write complete host file");
    }

    void CreateDirectories(const std::filesystem::path& path) override {
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error) {
            throw std::filesystem::filesystem_error("create_directories", path,
                                                     error);
        }
    }
};

}  // namespace

std::unique_ptr<HostFileSystem> CreateStandardHostFileSystem() {
    return std::make_unique<StandardHostFileSystem>();
}

}  // namespace ogplay::hal

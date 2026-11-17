#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ogplay::test {
struct ZipMember final {
    std::string name;  // raw bytes
    std::string content;
    bool deflate{};  // a single stored deflate block
    std::uint16_t flags{};
    std::uint16_t method{};    // for unsupported method tests
    std::string unicode_name;  // optional Info-ZIP Unicode Path
};

// A plain ZIP for archive-import tests.
void WriteZip(const std::filesystem::path& path, const std::vector<ZipMember>& members);
}  // namespace ogplay::test

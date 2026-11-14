#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ogplay::test {
struct ZipMember final {
    std::string name;      // raw bytes
    std::string content;
    bool deflate{};        // a single stored deflate block
    std::uint16_t flags{};
};
// A plain ZIP for archive-import tests (CRCs left zero: the reader does not check them, as for APKs).
void WriteZip(const std::filesystem::path& path, const std::vector<ZipMember>& members);
}  // namespace ogplay::test

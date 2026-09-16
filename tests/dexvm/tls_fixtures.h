#pragma once

#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::test {

inline std::vector<std::byte> ReadTlsFixture(const std::string_view name) {
    const auto path = std::filesystem::path(OGPLAY_SOURCE_DIR) /
        "tests/fixtures/tls" / std::string(name);
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("missing TLS fixture: " + path.string());
    const std::vector<char> raw{std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(raw.size());
    std::memcpy(bytes.data(), raw.data(), raw.size());
    return bytes;
}

inline void InstallCaPack(runtime::VirtualFileSystem& vfs,
                          const std::string_view file = "cacerts.ogplay") {
    vfs.CreateDirectory("/system");
    vfs.CreateDirectory("/system/etc");
    vfs.CreateDirectory("/system/etc/security");
    const auto pack = ReadTlsFixture(file);
    vfs.PutFile("/system/etc/security/cacerts.ogplay", pack, false);
}

}  // namespace ogplay::test

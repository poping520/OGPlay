#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "ogplay/runtime/bionic/bionic_selfcheck.h"

namespace {

[[nodiscard]] std::vector<std::byte> ReadBytes(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot read Bionic oracle");
    const auto size = input.tellg();
    if (size <= 0) throw std::runtime_error("empty Bionic oracle");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) throw std::runtime_error("truncated Bionic oracle read");
    return bytes;
}

[[nodiscard]] std::optional<std::filesystem::path> OracleRoot() {
#if defined(_WIN32)
    char* value{};
    std::size_t size{};
    if (_dupenv_s(&value, &size, "OGPLAY_BIONIC_ORACLE_ROOT") != 0 ||
        value == nullptr) {
        return std::nullopt;
    }
    const std::filesystem::path result{value};
    std::free(value);
    return result;
#else
    const auto* value = std::getenv("OGPLAY_BIONIC_ORACLE_ROOT");
    if (value == nullptr) return std::nullopt;
    return std::filesystem::path{value};
#endif
}

}  // namespace

TEST_CASE("API 19 22 23 Bionic libc passes the real module self-check") {
    const auto root = OracleRoot();
    if (!root.has_value()) return;
    for (const std::uint32_t api : {19U, 22U, 23U}) {
        const auto directory = *root / ("api" + std::to_string(api)) / "lib";
        const auto libc = ReadBytes(directory / "libc.so");
        const auto libdl = ReadBytes(directory / "libdl.so");
        const ogplay::loader::Elf32ModuleInput inputs[]{
            {"libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}},
            {"libdl.so", libdl, ogplay::memory::GuestAddress{0x20000000U}}};
        ogplay::memory::AddressSpace memory;
        const auto report = ogplay::runtime::SelfCheckBionicProfile(
            api, inputs, memory);
        CHECK(static_cast<std::uint32_t>(report.api) == api);
        CHECK(report.module_count == 2);
        CHECK(report.symbol_count > 100);
        CHECK(report.relocation_count > 10);
        CHECK(report.has_arm_exidx);
    }
}

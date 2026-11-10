#pragma once
#include "ogplay/runtime/resources/resources.h"
#include <filesystem>

namespace ogplay::test::resources {
namespace res = runtime::resources;
[[nodiscard]] std::shared_ptr<const res::AssetPath> Framework();
[[nodiscard]] res::Source ReadSource(const std::filesystem::path& path);
[[nodiscard]] res::Source OwnedSource(std::vector<std::byte> bytes, std::string name = "fixture.apk");
[[nodiscard]] res::ResTable Table();
[[nodiscard]] std::uint32_t Id(const res::ResTable& table, std::string_view name);
[[nodiscard]] res::ResTable_config Config(std::uint16_t width = 800, std::uint16_t height = 480,
                                          std::uint16_t density = 240);
[[nodiscard]] std::vector<std::byte> Zip(std::span<const std::pair<std::string, std::vector<std::byte>>> entries);
}  // namespace ogplay::test::resources

namespace ogplay::test::resources {
struct Entry {
    std::uint32_t id;
    std::string name;
    res::Value value;
    bool complex{};
    std::uint32_t parent{};
    std::vector<std::pair<std::uint32_t, res::Value>> bag;
    res::ResTable_config config;
    std::uint32_t flags{0x40000000};
};

[[nodiscard]] std::vector<std::byte> StringPool(std::span<const std::u16string> strings, bool utf8 = false,
                                                bool sorted = false);
[[nodiscard]] std::vector<std::byte> ResourceTable(std::span<const Entry> entries, std::uint32_t package = 0x7f,
                                                   std::string_view packageName = "fixture",
                                                   std::span<const std::u16string> strings = {},
                                                   std::span<const std::u16string> typeNames = {});
[[nodiscard]] res::Source AppApk();
}  // namespace ogplay::test::resources

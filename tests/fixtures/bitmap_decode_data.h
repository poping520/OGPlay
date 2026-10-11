#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace ogplay::test {
inline std::vector<std::byte> BitmapDecodePng() {
    constexpr unsigned char data[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0xf4,0x22,0x7f,0x8a,0x00,0x00,0x00,0x0e,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xcf,0xc0,0x00,0x42,0xff,0x01,0x0f,0xf9,0x03,0xfd,0x85,0x11,0x99,0x76,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
    std::vector<std::byte> result;
    for (auto b : data) result.push_back(static_cast<std::byte>(b));
    return result;
}
inline std::vector<std::byte> BitmapDecodeApk() {
    const std::string name = "res/drawable/fixture.png";
    const auto data = BitmapDecodePng();
    constexpr std::uint32_t crc = 0x2fa0abd5U; // fixed fixture PNG CRC32
    std::vector<std::byte> out;
    const auto word = [&](std::uint32_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i) out.push_back(static_cast<std::byte>(value >> (8*i)));
    };
    const auto append_name = [&] { for (auto c : name) out.push_back(static_cast<std::byte>(c)); };
    word(0x04034b50,4); word(20,2); word(0,2); word(0,2); word(0,4); word(crc,4);
    word(static_cast<std::uint32_t>(data.size()),4); word(static_cast<std::uint32_t>(data.size()),4);
    word(static_cast<std::uint32_t>(name.size()),2); word(0,2); append_name();
    out.insert(out.end(),data.begin(),data.end());
    const auto central = static_cast<std::uint32_t>(out.size());
    word(0x02014b50,4); word(20,2); word(20,2); word(0,2); word(0,2); word(0,4); word(crc,4);
    word(static_cast<std::uint32_t>(data.size()),4); word(static_cast<std::uint32_t>(data.size()),4);
    word(static_cast<std::uint32_t>(name.size()),2); word(0,2); word(0,2); word(0,2); word(0,2); word(0,4); word(0,4); append_name();
    const auto size = static_cast<std::uint32_t>(out.size()) - central;
    word(0x06054b50,4); word(0,2); word(0,2); word(1,2); word(1,2); word(size,4); word(central,4); word(0,2);
    return out;
}
} // namespace ogplay::test

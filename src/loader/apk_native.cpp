#include "ogplay/loader/apk_native.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::loader {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

std::uint32_t ReadBigEndian32(const std::span<const std::byte> bytes,
                              const std::size_t offset) {
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index) {
        value = (value << 8U) |
                std::to_integer<std::uint8_t>(bytes[offset + index]);
    }
    return value;
}

void TransformSha256(std::array<std::uint32_t, 8>& state,
                     const std::span<const std::byte> block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        words[index] = ReadBigEndian32(block, index * 4U);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const auto s0 = std::rotr(words[index - 15], 7) ^
                        std::rotr(words[index - 15], 18) ^
                        (words[index - 15] >> 3U);
        const auto s1 = std::rotr(words[index - 2], 17) ^
                        std::rotr(words[index - 2], 19) ^
                        (words[index - 2] >> 10U);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    auto a = state[0];
    auto b = state[1];
    auto c = state[2];
    auto d = state[3];
    auto e = state[4];
    auto f = state[5];
    auto g = state[6];
    auto h = state[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const auto choice = (e & f) ^ (~e & g);
        const auto temporary1 = h + sum1 + choice + kSha256RoundConstants[index] +
                                words[index];
        const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

std::string Sha256(const std::span<const std::byte> bytes) {
    std::array<std::uint32_t, 8> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::size_t offset{};
    while (bytes.size() - offset >= 64U) {
        TransformSha256(state, bytes.subspan(offset, 64));
        offset += 64U;
    }

    std::array<std::byte, 128> tail{};
    const auto remaining = bytes.size() - offset;
    std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end(),
              tail.begin());
    tail[remaining] = std::byte{0x80};
    const std::size_t padded_size = remaining < 56U ? 64U : 128U;
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;
    for (std::size_t index = 0; index < 8; ++index) {
        tail[padded_size - 1U - index] =
            static_cast<std::byte>(bit_length >> static_cast<unsigned>(index * 8U));
    }
    TransformSha256(state, std::span<const std::byte>{tail.data(), 64});
    if (padded_size == 128U) {
        TransformSha256(state, std::span<const std::byte>{tail.data() + 64, 64});
    }

    constexpr std::string_view hexadecimal = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    constexpr std::array<unsigned, 4> shifts{24U, 16U, 8U, 0U};
    for (const auto word : state) {
        for (const auto shift : shifts) {
            const auto byte = static_cast<std::uint8_t>(word >> shift);
            result.push_back(hexadecimal[byte >> 4U]);
            result.push_back(hexadecimal[byte & 0x0fU]);
        }
    }
    return result;
}

bool DecodeLibraryPath(const std::string_view entry, AndroidArmAbi& abi,
                       std::string& basename) {
    constexpr std::string_view prefix = "lib/";
    if (!entry.starts_with(prefix) || !entry.ends_with(".so")) return false;
    const auto separator = entry.find('/', prefix.size());
    if (separator == std::string_view::npos ||
        entry.find('/', separator + 1U) != std::string_view::npos) {
        return false;
    }
    const auto abi_name = entry.substr(prefix.size(), separator - prefix.size());
    if (abi_name == "armeabi") {
        abi = AndroidArmAbi::armeabi;
    } else if (abi_name == "armeabi-v7a") {
        abi = AndroidArmAbi::armeabi_v7a;
    } else {
        return false;
    }
    const auto filename = entry.substr(separator + 1U);
    if (filename.size() <= 3U) return false;
    basename = std::string(filename);
    return true;
}

}  // namespace

std::string_view ToString(const AndroidArmAbi abi) noexcept {
    switch (abi) {
    case AndroidArmAbi::armeabi: return "armeabi";
    case AndroidArmAbi::armeabi_v7a: return "armeabi-v7a";
    }
    return "unknown";
}

std::vector<ApkNativeLibrary> ReadApkArmNativeLibraries(
    const std::span<const std::byte> apk_bytes, const ApkArchive& archive) {
    std::vector<ApkNativeLibrary> result;
    for (const auto& entry : archive.entries) {
        AndroidArmAbi abi{};
        std::string basename;
        if (!DecodeLibraryPath(entry.name, abi, basename)) continue;
        auto image = ReadApkEntry(apk_bytes, archive, entry.name);
        if (image.empty()) {
            throw std::runtime_error("APK native library is empty: " + entry.name);
        }
        result.push_back({entry.name, std::move(basename), abi, Sha256(image),
                          std::move(image)});
    }
    std::ranges::sort(result, {}, &ApkNativeLibrary::entry_name);
    return result;
}

}  // namespace ogplay::loader

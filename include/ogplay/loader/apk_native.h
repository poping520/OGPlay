#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/loader/apk.h"

namespace ogplay::loader {

enum class AndroidArmAbi : std::uint8_t {
    armeabi,
    armeabi_v7a,
};

[[nodiscard]] std::string_view ToString(AndroidArmAbi abi) noexcept;

struct ApkNativeLibrary final {
    std::string entry_name;
    std::string basename;
    AndroidArmAbi abi{AndroidArmAbi::armeabi_v7a};
    std::string sha256;
    std::vector<std::byte> image;
};

[[nodiscard]] std::vector<ApkNativeLibrary> ReadApkArmNativeLibraries(
    std::span<const std::byte> apk_bytes, const ApkArchive& archive);

}  // namespace ogplay::loader

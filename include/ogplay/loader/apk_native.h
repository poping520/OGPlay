#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
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
    // Stable inventory identity derived from the APK entry basename. The
    // dynamic loader may additionally index a differing ELF DT_SONAME as an
    // alias for this same library.
    std::string soname;
    // System.loadLibrary token when the soname has the lib<name>.so shape.
    std::optional<std::string> logical_name;
};

enum class ApkNativeInventoryErrorReason : std::uint8_t {
    invalid_library_identity,
    duplicate_soname,
    duplicate_logical_name,
    native_abi_required,
    no_compatible_abi,
    abi_not_present,
};

class ApkNativeInventoryError final : public std::runtime_error {
public:
    ApkNativeInventoryError(ApkNativeInventoryErrorReason reason,
                            std::string message);
    [[nodiscard]] ApkNativeInventoryErrorReason Reason() const noexcept;

private:
    ApkNativeInventoryErrorReason reason_;
};

class ApkNativeLibraryInventory final {
public:
    explicit ApkNativeLibraryInventory(std::vector<ApkNativeLibrary> libraries);

    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] bool HasAbi(AndroidArmAbi abi) const noexcept;
    [[nodiscard]] std::vector<AndroidArmAbi> Abis() const;
    [[nodiscard]] std::span<const ApkNativeLibrary> Libraries() const noexcept;
    [[nodiscard]] const ApkNativeLibrary* FindSoname(
        AndroidArmAbi abi, std::string_view soname) const noexcept;
    [[nodiscard]] const ApkNativeLibrary* FindLogicalName(
        AndroidArmAbi abi, std::string_view logical_name) const noexcept;
    [[nodiscard]] const ApkNativeLibrary* FindEntryName(
        AndroidArmAbi abi, std::string_view entry_name) const noexcept;

private:
    std::vector<ApkNativeLibrary> libraries_;
};

class ApkSelectedNativeLibraries final {
public:
    ApkSelectedNativeLibraries(const ApkNativeLibraryInventory& inventory,
                               AndroidArmAbi abi);

    [[nodiscard]] AndroidArmAbi Abi() const noexcept;
    [[nodiscard]] std::span<const ApkNativeLibrary> Libraries() const noexcept;
    [[nodiscard]] const ApkNativeLibrary* FindSoname(
        std::string_view soname) const noexcept;
    [[nodiscard]] const ApkNativeLibrary* FindLogicalName(
        std::string_view logical_name) const noexcept;
    [[nodiscard]] const ApkNativeLibrary* FindEntryName(
        std::string_view entry_name) const noexcept;

private:
    const ApkNativeLibraryInventory* inventory_{};
    AndroidArmAbi abi_{AndroidArmAbi::armeabi_v7a};
};

[[nodiscard]] std::vector<ApkNativeLibrary> ReadApkArmNativeLibraries(
    std::span<const std::byte> apk_bytes, const ApkArchive& archive);
[[nodiscard]] ApkNativeLibraryInventory ReadApkNativeLibraryInventory(
    std::span<const std::byte> apk_bytes, const ApkArchive& archive);

[[nodiscard]] std::span<const AndroidArmAbi> RuntimeSupportedAndroidArmAbis()
    noexcept;
[[nodiscard]] AndroidArmAbi ResolveApkProcessAbi(
    std::span<const AndroidArmAbi> runtime_supported,
    const ApkNativeLibraryInventory& inventory);
[[nodiscard]] AndroidArmAbi ResolveApkProcessAbi(
    const ApkNativeLibraryInventory& inventory);
[[nodiscard]] ApkSelectedNativeLibraries SelectApkNativeLibraries(
    const ApkNativeLibraryInventory& inventory, AndroidArmAbi abi);

}  // namespace ogplay::loader

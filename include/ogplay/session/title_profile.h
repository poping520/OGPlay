#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ogplay::session {

class QuirkRegistry;

enum class ProfileLifecycle : std::uint8_t {
    dex_activity,
};

enum class ProfileSource : std::uint8_t { apk, obb, external };

enum class ProfileAbi : std::uint8_t { armeabi, armeabi_v7a };

struct ProfileIdentity final {
    std::string package;
    std::string name;
    std::vector<std::uint32_t> version_codes;
    std::vector<std::string> so_sha256;
    ProfileAbi abi{ProfileAbi::armeabi_v7a};
    // v1/v2 exact applicability guard. Schema v3 forbids ABI and leaves
    // process selection entirely to the APK inventory resolver.
    bool has_abi_guard{true};
};

struct ProfileSurface final {
    std::uint32_t width{};
    std::uint32_t height{};
};

using ProfileStaticPresetValue =
    std::variant<bool, std::int64_t, double, std::string>;

struct ProfileStaticPreset final {
    std::string class_name;
    std::string field;
    std::string type;
    ProfileStaticPresetValue value;
    std::string reason;
};

struct ProfileEntry final {
    std::string launch_activity;
};

struct ProfileRuntime final {
    static constexpr std::uint64_t kDefaultMaximumTicksPerCall =
        UINT64_C(200000000);
    static constexpr std::uint64_t kMaximumTicksPerCall =
        UINT64_C(10000000000);

    ProfileRuntime() = default;
    ProfileRuntime(std::uint32_t api, ProfileLifecycle lifecycle_value,
                   ProfileSurface surface_value)
        : api_level(api), lifecycle(lifecycle_value), surface(surface_value) {}

    struct DexVm final {
        enum class Interpreter : std::uint8_t {
            switch_dispatch,
            threaded,
        };

        std::uint64_t heap_budget_bytes{64ULL * 1024ULL * 1024ULL};
        std::uint32_t gc_watermark_percent{75};
        std::uint32_t max_frames{512};
        std::uint64_t ticks_per_call{kDefaultMaximumTicksPerCall};
        Interpreter interpreter{Interpreter::switch_dispatch};
    };

    std::uint32_t api_level{};
    ProfileLifecycle lifecycle{ProfileLifecycle::dex_activity};
    std::uint64_t maximum_ticks_per_call{kDefaultMaximumTicksPerCall};
    ProfileSurface surface;
    std::optional<DexVm> dexvm;
    std::optional<ProfileEntry> entry;
    std::vector<ProfileStaticPreset> presets;
};

struct ProfileMount final {
    std::string guest;
    ProfileSource source{ProfileSource::external};
    bool required{};
};

struct ProfileManifestEntry final {
    std::string path;
    bool required{};
};

struct ProfileData final {
    std::vector<ProfileMount> mounts;
    std::optional<std::string> working_directory;
    std::vector<ProfileManifestEntry> manifest;
};

struct ProfileCoverMusic final {
    ProfileSource source{ProfileSource::apk};
    std::string path;
    bool loop{};
};

struct ProfileSoundPool final {
    ProfileSource source{ProfileSource::apk};
    std::string path_pattern;
};

struct ProfileAudio final {
    std::optional<ProfileCoverMusic> cover_music;
    std::optional<ProfileSoundPool> sound_pool;
};

struct ProfileValue final {
    using Array = std::vector<ProfileValue>;
    using Table = std::map<std::string, ProfileValue, std::less<>>;
    using Storage = std::variant<bool, std::int64_t, double, std::string, Array, Table>;

    Storage value;
};

struct ProfileQuirks final {
    std::vector<std::string> enabled;
    std::map<std::string, ProfileValue::Table, std::less<>> parameters;
};

struct ProfileInput final {
    std::string profile;
};

struct TitleProfile final {
    std::uint32_t schema{};
    ProfileIdentity identity;
    ProfileRuntime runtime;
    std::optional<ProfileData> data;
    std::optional<ProfileAudio> audio;
    std::optional<ProfileQuirks> quirks;
    std::optional<ProfileInput> input;
};

struct TitleIdentity final {
    std::string package;
    std::uint32_t version_code{};
    std::string so_sha256;
};

class TitleProfileError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] TitleProfile LoadTitleProfileText(std::string_view text,
                                                std::string_view expected_package);
[[nodiscard]] TitleProfile LoadTitleProfile(const std::filesystem::path& path);

class TitleProfileCatalog final {
public:
    explicit TitleProfileCatalog(std::vector<TitleProfile> profiles);
    TitleProfileCatalog(std::vector<TitleProfile> profiles,
                        const QuirkRegistry& registry);

    [[nodiscard]] static TitleProfileCatalog LoadDirectory(
        const std::filesystem::path& directory);
    [[nodiscard]] static TitleProfileCatalog LoadDirectory(
        const std::filesystem::path& directory, const QuirkRegistry& registry);
    [[nodiscard]] const TitleProfile* Match(const TitleIdentity& identity) const;
    [[nodiscard]] const std::vector<TitleProfile>& Profiles() const noexcept;

private:
    void Validate(const QuirkRegistry* registry) const;

    std::vector<TitleProfile> profiles_;
};

[[nodiscard]] std::string_view ToString(ProfileLifecycle lifecycle) noexcept;
[[nodiscard]] std::string_view ToString(ProfileSource source) noexcept;
[[nodiscard]] std::string_view ToString(ProfileAbi abi) noexcept;

}  // namespace ogplay::session

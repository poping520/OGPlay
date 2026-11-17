#include "runtime/boundary/modules/android/android_module.h"

#include <algorithm>
#include <stdexcept>

namespace ogplay::runtime {

BoundaryResult AndroidModule::AConfiguration_new(const A32CallFrame&) {
    resources::ResTable_config config;
    if (services_.device_locale) {
        const auto locale = services_.device_locale();
        std::copy_n(locale.language.begin(), 2, config.language);
        std::copy_n(locale.region.begin(), 2, config.country);
    }
    std::scoped_lock lock(mutex_);
    if (next_configuration_.Value() >= 0x6e100000U) throw std::runtime_error("AConfiguration handle budget exhausted");
    const auto token = next_configuration_;
    configurations_.emplace(token.Value(), config);
    next_configuration_ = token.Add(1);
    return token.Value();
}

BoundaryResult AndroidModule::AConfiguration_delete(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    if (!configurations_.erase(call.RegisterArguments()[0])) throw std::invalid_argument("AConfiguration is stale");
    return 0;
}

BoundaryResult AndroidModule::AConfiguration_fromAssetManager(const A32CallFrame& call) {
    const auto args = call.RegisterArguments();
    std::shared_ptr<AndroidAssetManagerSource> manager;
    {
        std::scoped_lock lock(mutex_);
        const auto found = asset_managers_.find(args[1]);
        if (found == asset_managers_.end()) throw std::invalid_argument("AAssetManager is stale");
        manager = found->second;
    }
    const auto config = manager->Configuration();
    if (!config) throw std::invalid_argument("AAssetManager has no resource configuration");
    std::scoped_lock lock(mutex_);
    const auto found = configurations_.find(args[0]);
    if (found == configurations_.end()) throw std::invalid_argument("AConfiguration is stale");
    found->second = *config;
    return 0;
}

BoundaryResult AndroidModule::WriteConfigurationLocale(const A32CallFrame& call,
                                                       char (resources::ResTable_config::*field)[2]) {
    const auto output = call.Pointer<std::byte>(1);
    if (!output.IsNull()) {
        std::array<std::byte, 2> bytes;
        {
            std::scoped_lock lock(mutex_);
            const auto found = configurations_.find(call.RegisterArguments()[0]);
            if (found == configurations_.end()) throw std::invalid_argument("AConfiguration is stale");
            const auto& code = found->second.*field;
            bytes = {static_cast<std::byte>(code[0]), static_cast<std::byte>(code[1])};
        }
        services_.address_space.Write(output.Address(), bytes, call.ThreadId());
    }
    return 0;
}

BoundaryResult AndroidModule::AConfiguration_getLanguage(const A32CallFrame& call) {
    return WriteConfigurationLocale(call, &resources::ResTable_config::language);
}

BoundaryResult AndroidModule::AConfiguration_getCountry(const A32CallFrame& call) {
    return WriteConfigurationLocale(call, &resources::ResTable_config::country);
}

resources::ResTable_config& AndroidModule::RequireConfiguration(GuestPtr<void> handle) {
    const auto found = configurations_.find(handle.Address().Value());
    if (found == configurations_.end()) throw std::invalid_argument("AConfiguration is stale or unregistered");
    return found->second;
}

#define OGPLAY_CONFIG_FIELD(getter, setter, member)                                                                    \
    BoundaryResult AndroidModule::getter(const A32CallFrame& call) {                                                   \
        std::scoped_lock lock(mutex_);                                                                                 \
        return RequireConfiguration(call.Pointer<void>(0)).member;                                                     \
    }                                                                                                                  \
    BoundaryResult AndroidModule::setter(const A32CallFrame& call) {                                                   \
        std::scoped_lock lock(mutex_);                                                                                 \
        auto& field = RequireConfiguration(call.Pointer<void>(0)).member;                                              \
        field = static_cast<std::remove_reference_t<decltype(field)>>(call.Argument(1));                               \
        return 0;                                                                                                      \
    }

#define OGPLAY_CONFIG_MASKED_FIELD(getter, setter, member, mask, shift)                                                \
    BoundaryResult AndroidModule::getter(const A32CallFrame& call) {                                                   \
        std::scoped_lock lock(mutex_);                                                                                 \
        constexpr std::uint32_t field_mask = resources::ResTable_config::mask;                                         \
        return (RequireConfiguration(call.Pointer<void>(0)).member & field_mask) >> (shift);                           \
    }                                                                                                                  \
    BoundaryResult AndroidModule::setter(const A32CallFrame& call) {                                                   \
        std::scoped_lock lock(mutex_);                                                                                 \
        constexpr std::uint32_t field_mask = resources::ResTable_config::mask;                                         \
        auto& field = RequireConfiguration(call.Pointer<void>(0)).member;                                              \
        field = static_cast<std::uint8_t>((field & ~field_mask) | ((call.Argument(1) << (shift)) & field_mask));       \
        return 0;                                                                                                      \
    }

OGPLAY_CONFIG_FIELD(AConfiguration_getMcc, AConfiguration_setMcc, mcc)
OGPLAY_CONFIG_FIELD(AConfiguration_getMnc, AConfiguration_setMnc, mnc)
OGPLAY_CONFIG_FIELD(AConfiguration_getOrientation, AConfiguration_setOrientation, orientation)
OGPLAY_CONFIG_FIELD(AConfiguration_getTouchscreen, AConfiguration_setTouchscreen, touchscreen)
OGPLAY_CONFIG_FIELD(AConfiguration_getDensity, AConfiguration_setDensity, density)
OGPLAY_CONFIG_FIELD(AConfiguration_getKeyboard, AConfiguration_setKeyboard, keyboard)
OGPLAY_CONFIG_FIELD(AConfiguration_getNavigation, AConfiguration_setNavigation, navigation)
OGPLAY_CONFIG_MASKED_FIELD(AConfiguration_getKeysHidden, AConfiguration_setKeysHidden, inputFlags, MASK_KEYSHIDDEN, 0)
OGPLAY_CONFIG_MASKED_FIELD(AConfiguration_getNavHidden, AConfiguration_setNavHidden, inputFlags, MASK_NAVHIDDEN,
                           resources::ResTable_config::SHIFT_NAVHIDDEN)
OGPLAY_CONFIG_FIELD(AConfiguration_getSdkVersion, AConfiguration_setSdkVersion, sdkVersion)
OGPLAY_CONFIG_MASKED_FIELD(AConfiguration_getScreenSize, AConfiguration_setScreenSize, screenLayout, MASK_SCREENSIZE, 0)
OGPLAY_CONFIG_MASKED_FIELD(AConfiguration_getScreenLong, AConfiguration_setScreenLong, screenLayout, MASK_SCREENLONG,
                           resources::ResTable_config::SHIFT_SCREENLONG)
OGPLAY_CONFIG_MASKED_FIELD(AConfiguration_getUiModeType, AConfiguration_setUiModeType, uiMode, MASK_UI_MODE_TYPE, 0)
OGPLAY_CONFIG_MASKED_FIELD(AConfiguration_getUiModeNight, AConfiguration_setUiModeNight, uiMode, MASK_UI_MODE_NIGHT,
                           resources::ResTable_config::SHIFT_UI_MODE_NIGHT)
OGPLAY_CONFIG_FIELD(AConfiguration_getScreenWidthDp, AConfiguration_setScreenWidthDp, screenWidthDp)
OGPLAY_CONFIG_FIELD(AConfiguration_getScreenHeightDp, AConfiguration_setScreenHeightDp, screenHeightDp)
OGPLAY_CONFIG_FIELD(AConfiguration_getSmallestScreenWidthDp, AConfiguration_setSmallestScreenWidthDp,
                    smallestScreenWidthDp)
OGPLAY_CONFIG_MASKED_FIELD(AConfiguration_getLayoutDirection, AConfiguration_setLayoutDirection, screenLayout,
                           MASK_LAYOUTDIR, resources::ResTable_config::SHIFT_LAYOUTDIR)

#undef OGPLAY_CONFIG_FIELD
#undef OGPLAY_CONFIG_MASKED_FIELD

namespace {
void ReadLocaleCode(memory::AddressSpace& space, GuestPtr<char> source, char (&code)[2], std::uint64_t thread) {
    if (source.IsNull()) throw std::invalid_argument("AConfiguration requires a locale code");
    std::array<std::byte, 2> bytes{};
    space.Read(source.Address(), bytes, thread);
    code[0] = static_cast<char>(bytes[0]);
    code[1] = static_cast<char>(bytes[1]);
}
}  // namespace

BoundaryResult AndroidModule::AConfiguration_setLanguage(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    ReadLocaleCode(services_.address_space, call.Pointer<char>(1), RequireConfiguration(call.Pointer<void>(0)).language,
                   call.ThreadId());
    return 0;
}

BoundaryResult AndroidModule::AConfiguration_setCountry(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    ReadLocaleCode(services_.address_space, call.Pointer<char>(1), RequireConfiguration(call.Pointer<void>(0)).country,
                   call.ThreadId());
    return 0;
}

BoundaryResult AndroidModule::AConfiguration_copy(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    RequireConfiguration(call.Pointer<void>(0)) = RequireConfiguration(call.Pointer<void>(1));
    return 0;
}

BoundaryResult AndroidModule::AConfiguration_diff(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    return RequireConfiguration(call.Pointer<void>(0)).diff(RequireConfiguration(call.Pointer<void>(1)));
}

BoundaryResult AndroidModule::AConfiguration_match(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    return RequireConfiguration(call.Pointer<void>(0)).match(RequireConfiguration(call.Pointer<void>(1)));
}

BoundaryResult AndroidModule::AConfiguration_isBetterThan(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto requested = call.Pointer<void>(2);
    return RequireConfiguration(call.Pointer<void>(0))
        .isBetterThan(RequireConfiguration(call.Pointer<void>(1)),
                      requested.IsNull() ? nullptr : &RequireConfiguration(requested));
}

}  // namespace ogplay::runtime

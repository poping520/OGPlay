#pragma once

#include <array>
#include <string_view>

namespace ogplay::runtime::dexvm {
enum class ServiceFactory {
    unsupported,
    singleton,
    inflater,
    activity,
    alarm,
    audio,
    sensor,
    nfc,
    usb,
    connectivity,
    wifi,
    power,
    vibrator,
    notification,
    keyguard,
    telephony,
    location
};

struct PlatformService final {
    std::string_view name;
    ServiceFactory factory;
    std::string_view descriptor;
    std::string_view singleton_key;

    [[nodiscard]] constexpr bool Provided() const { return factory != ServiceFactory::unsupported; }
};

inline constexpr auto kSystemServices = std::to_array<PlatformService>({
    {"accessibility", ServiceFactory::unsupported, "", "accessibility"},
    {"account", ServiceFactory::unsupported, "", "account"},
    {"activity", ServiceFactory::activity, "", "activity"},
    {"alarm", ServiceFactory::alarm, {}, {}},
    {"appops", ServiceFactory::unsupported, "", "appops"},
    {"audio", ServiceFactory::audio, "", "audio"},
    {"bluetooth", ServiceFactory::unsupported, "", "bluetooth"},
    {"camera", ServiceFactory::unsupported, "", "camera"},
    {"captioning", ServiceFactory::unsupported, "", "captioning"},
    {"clipboard", ServiceFactory::unsupported, "", "clipboard"},
    {"connectivity", ServiceFactory::connectivity, "Landroid/net/ConnectivityManager;", "connectivity"},
    {"consumer_ir", ServiceFactory::unsupported, "", "consumer_ir"},
    {"country_detector", ServiceFactory::unsupported, "", "country_detector"},
    {"device_policy", ServiceFactory::unsupported, "", "device_policy"},
    {"display", ServiceFactory::singleton, "Landroid/hardware/display/DisplayManager;", "display_manager"},
    {"download", ServiceFactory::unsupported, "", "download"},
    {"dropbox", ServiceFactory::unsupported, "", "dropbox"},
    {"input", ServiceFactory::unsupported, "", "input"},
    {"input_method", ServiceFactory::singleton, "Landroid/view/inputmethod/InputMethodManager;", "input_method"},
    {"keyguard", ServiceFactory::keyguard, "", "keyguard"},
    {"layout_inflater", ServiceFactory::inflater, "", "layout_inflater"},
    {"location", ServiceFactory::location, "Landroid/location/LocationManager;", "location"},
    {"media_router", ServiceFactory::unsupported, "", "media_router"},
    {"netpolicy", ServiceFactory::unsupported, "", "netpolicy"},
    {"nfc", ServiceFactory::nfc, "", "nfc"},
    {"notification", ServiceFactory::notification, "", "notification"},
    {"phone", ServiceFactory::telephony, "Landroid/telephony/TelephonyManager;", "phone"},
    {"power", ServiceFactory::power, "Landroid/os/PowerManager;", "power"},
    {"print", ServiceFactory::unsupported, "", "print"},
    {"search", ServiceFactory::unsupported, "", "search"},
    {"sensor", ServiceFactory::sensor, "", "sensor"},
    {"serial", ServiceFactory::unsupported, "", "serial"},
    {"servicediscovery", ServiceFactory::unsupported, "", "servicediscovery"},
    {"statusbar", ServiceFactory::unsupported, "", "statusbar"},
    {"storage", ServiceFactory::unsupported, "", "storage"},
    {"textservices", ServiceFactory::unsupported, "", "textservices"},
    {"uimode", ServiceFactory::unsupported, "", "uimode"},
    {"usb", ServiceFactory::usb, "", "usb"},
    {"user", ServiceFactory::unsupported, "", "user"},
    {"vibrator", ServiceFactory::vibrator, "Landroid/os/SystemVibrator;", "vibrator"},
    {"wallpaper", ServiceFactory::unsupported, "", "wallpaper"},
    {"wifi", ServiceFactory::wifi, "Landroid/net/wifi/WifiManager;", "wifi"},
    {"wifip2p", ServiceFactory::unsupported, "", "wifip2p"},
    {"window", ServiceFactory::singleton, "Landroid/view/WindowManagerImpl;", "window_manager"},
});

}  // namespace ogplay::runtime::dexvm

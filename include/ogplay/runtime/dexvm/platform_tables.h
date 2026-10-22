#pragma once

#include <array>
#include <string_view>

namespace ogplay::runtime::dexvm {
enum class ServiceFactory { unsupported, singleton, inflater, activity, sensor, nfc, notification, keyguard };

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
    {"alarm", ServiceFactory::singleton, "Landroid/app/AlarmManager;", "alarm"},
    {"appops", ServiceFactory::unsupported, "", "appops"},
    {"audio", ServiceFactory::singleton, "Landroid/media/AudioManager;", "audio"},
    {"bluetooth", ServiceFactory::unsupported, "", "bluetooth"},
    {"camera", ServiceFactory::unsupported, "", "camera"},
    {"captioning", ServiceFactory::unsupported, "", "captioning"},
    {"clipboard", ServiceFactory::unsupported, "", "clipboard"},
    {"connectivity", ServiceFactory::singleton, "Landroid/net/ConnectivityManager;", "connectivity"},
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
    {"location", ServiceFactory::singleton, "Landroid/location/LocationManager;", "location"},
    {"media_router", ServiceFactory::unsupported, "", "media_router"},
    {"netpolicy", ServiceFactory::unsupported, "", "netpolicy"},
    {"nfc", ServiceFactory::nfc, "", "nfc"},
    {"notification", ServiceFactory::notification, "", "notification"},
    {"phone", ServiceFactory::singleton, "Landroid/telephony/TelephonyManager;", "phone"},
    {"power", ServiceFactory::singleton, "Landroid/os/PowerManager;", "power"},
    {"print", ServiceFactory::unsupported, "", "print"},
    {"search", ServiceFactory::unsupported, "", "search"},
    {"sensor", ServiceFactory::sensor, "", "sensor"},
    {"serial", ServiceFactory::unsupported, "", "serial"},
    {"servicediscovery", ServiceFactory::unsupported, "", "servicediscovery"},
    {"statusbar", ServiceFactory::unsupported, "", "statusbar"},
    {"storage", ServiceFactory::unsupported, "", "storage"},
    {"textservices", ServiceFactory::unsupported, "", "textservices"},
    {"uimode", ServiceFactory::unsupported, "", "uimode"},
    {"usb", ServiceFactory::unsupported, "", "usb"},
    {"user", ServiceFactory::unsupported, "", "user"},
    {"vibrator", ServiceFactory::singleton, "Landroid/os/Vibrator;", "vibrator"},
    {"wallpaper", ServiceFactory::unsupported, "", "wallpaper"},
    {"wifi", ServiceFactory::singleton, "Landroid/net/wifi/WifiManager;", "wifi"},
    {"wifip2p", ServiceFactory::unsupported, "", "wifip2p"},
    {"window", ServiceFactory::singleton, "Landroid/view/WindowManagerImpl;", "window_manager"},
});

struct PlatformProperty final {
    std::string_view key;
    std::string_view value;
    std::string_view source;
};

// Values with a source are defaults for a VM without an attached native process.
// Android binding replaces passwd/ICU/environment values from that process.
inline constexpr auto kSystemProperties = std::to_array<PlatformProperty>({
    {"android.icu.cldr.version", "", "ICU.getCldrVersion()"},
    {"android.icu.library.version", "", "ICU.getIcuVersion()"},
    {"android.icu.unicode.version", "", "ICU.getUnicodeVersion()"},
    {"file.encoding", "UTF-8", ""},
    {"file.separator", "/", ""},
    {"java.boot.class.path", "/system/framework/bootdex.jar", "runtime.bootClassPath()"},
    {"java.class.path", ".", "runtime.classPath()"},
    {"java.class.version", "50.0", ""},
    {"java.compiler", "", ""},
    {"java.ext.dirs", "", ""},
    {"java.home", "/system", "getenv.JAVA_HOME"},
    {"java.io.tmpdir", "/tmp", ""},
    {"java.library.path", "/vendor/lib:/system/lib", "getenv.LD_LIBRARY_PATH"},
    {"java.runtime.name", "Android Runtime", ""},
    {"java.runtime.version", "0.9", ""},
    {"java.specification.name", "Dalvik Core Library", ""},
    {"java.specification.vendor", "The Android Project", ""},
    {"java.specification.version", "0.9", ""},
    {"java.vendor", "The Android Project", ""},
    {"java.vendor.url", "http://www.android.com/", ""},
    {"java.version", "0", ""},
    {"java.vm.name", "Dalvik", ""},
    {"java.vm.specification.name", "Dalvik Virtual Machine Specification", ""},
    {"java.vm.specification.vendor", "The Android Project", ""},
    {"java.vm.specification.version", "0.9", ""},
    {"java.vm.vendor", "The Android Project", ""},
    {"java.vm.vendor.url", "http://www.android.com/", ""},
    {"java.vm.version", "1.6.0", "runtime.vmVersion()"},
    {"line.separator", "\n", ""},
    {"os.arch", "armv7l", "uname.machine"},
    {"os.name", "Linux", "uname.sysname"},
    {"os.version", "3.4.0", "uname.release"},
    {"path.separator", ":", ""},
    {"user.home", "/data", "passwd.pw_dir"},
    {"user.language", "en", ""},
    {"user.name", "u0_a0", "passwd.pw_name"},
    {"user.region", "US", ""},
    {"user.dir", "/", ""},
});
}  // namespace ogplay::runtime::dexvm

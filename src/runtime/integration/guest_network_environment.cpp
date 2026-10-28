#include "guest_network_environment.h"
#include "ogplay/runtime/integration/dexvm_android.h"

#include <iomanip>
#include <sstream>

namespace ogplay::runtime {
namespace {
std::string Hex(const std::uint32_t value, const int width = 0) {
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(width) << value;
    return text.str();
}
}

GuestNetworkEnvironment::GuestNetworkEnvironment(const AndroidOfflineConnectivity&)
    // API19-era Linux v3.4 drivers/net/loopback.c: MTU 16*1024+20+20+12,
    // https://github.com/torvalds/linux/blob/v3.4/drivers/net/loopback.c
    // IFF_LOOPBACK; net/core/dev.c/dev_get_flags adds UP and RUNNING.
    // net/ipv4/devinet.c loopback 127.0.0.1/8; net/ipv6/addrconf.c ::1,
    // IPV6_ADDR_LOOPBACK / IFA_HOST (0x10), IFA_F_PERMANENT (0x80). No host adapter is sampled.
    : interfaces_{{"lo", 1, 0x49, 16436, "00:00:00:00:00:00",
        std::array{std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}},
        {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{0}}, {},
        {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
         std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
         std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
         std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}}, 128, 0x10, 0x80}} {}

void GuestNetworkEnvironment::Publish(VirtualFileSystem& filesystem) {
    std::vector<std::unique_ptr<VfsGeneratedFileRegistration>> files;
    const auto publish = [&](const std::string& path, const std::string& text) {
        const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
        files.push_back(filesystem.RegisterGeneratedReadOnly(path, bytes.size(),
            [contents = std::vector<std::byte>(bytes.begin(), bytes.end())] { return contents; }));
    };
    std::string inet6;
    for (const auto& interface : interfaces_) {
        const auto root = "/sys/class/net/" + interface.name + "/";
        publish(root + "ifindex", std::to_string(interface.index) + "\n");
        publish(root + "flags", "0x" + Hex(interface.flags) + "\n");
        publish(root + "mtu", std::to_string(interface.mtu) + "\n");
        publish(root + "address", interface.hardware_address + "\n");
        for (const auto byte : interface.ipv6) inet6 += Hex(std::to_integer<unsigned>(byte), 2);
        inet6 += " " + Hex(interface.index, 2) + " " + Hex(interface.ipv6_prefix, 2) +
                 " " + Hex(interface.ipv6_scope, 2) + " " + Hex(interface.ipv6_flags, 2) +
                 "       " + interface.name + "\n";
    }
    publish("/proc/net/if_inet6", inet6);
    files_ = std::move(files); // Any collision destroys all tentative registrations.
}
}  // namespace ogplay::runtime

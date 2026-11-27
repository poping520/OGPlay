#include "ogplay/runtime/integration/api19_guest_process.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/loader/link_namespace.h"

#include "ogplay/runtime/bionic/bionic_tls.h"

namespace ogplay::runtime {
namespace {

constexpr std::size_t kMaximumProgramNameBytes = 31;
// struct prop_area: bytes_used, serial, magic, version, reserved[28].
constexpr std::uint32_t kPropertyAreaHeaderBytes = 128;
constexpr std::uint32_t kPropertyAreaMagic = 0x504f5250U;
constexpr std::uint32_t kPropertyAreaVersion = 0xfc6ed0abU;
// PROP_NAME_MAX / PROP_VALUE_MAX include the terminator.
constexpr std::size_t kPropertyNameMax = 32;
constexpr std::size_t kPropertyValueMax = 92;
// struct prop_bt: namelen, reserved[3], prop, left, right, children, name[].
constexpr std::uint32_t kPropBtBytes = 20;
// struct prop_info: serial, value[PROP_VALUE_MAX], name[].
constexpr std::uint32_t kPropInfoBytes = 4 + kPropertyValueMax;

// Builds the data region exactly as bionic's find_property(..., alloc=true)
// would while adding the properties in order.
class PropertyTrie final {
public:
    explicit PropertyTrie(const std::uint32_t capacity) : data_(capacity) {
        Allocate(kPropBtBytes);  // root node; bionic reserves sizeof(prop_bt) for it
    }

    void Add(const std::string_view name, const std::string_view value) {
        if (name.empty() || name.size() >= kPropertyNameMax)
            throw std::invalid_argument("system property name must be 1..31 bytes: " + std::string(name));
        if (value.size() >= kPropertyValueMax)
            throw std::invalid_argument("system property value exceeds 91 bytes: " + std::string(name));
        if (name.find('\0') != std::string_view::npos || value.find('\0') != std::string_view::npos)
            throw std::invalid_argument("system property contains a null byte");
        std::uint32_t trie = 0;
        for (std::size_t start = 0;;) {
            const auto separator = name.find('.', start);
            const auto segment =
                name.substr(start, separator == std::string_view::npos ? std::string_view::npos : separator - start);
            if (segment.empty())
                throw std::invalid_argument("system property name has an empty segment: " + std::string(name));
            trie = FindOrAddChild(trie, segment);
            if (separator == std::string_view::npos) break;
            start = separator + 1U;
        }
        if (Read(trie + 4U) != 0) throw std::invalid_argument("duplicate system property: " + std::string(name));
        const auto info = Allocate(kPropInfoBytes + static_cast<std::uint32_t>(name.size()) + 1U);
        Write(info, static_cast<std::uint32_t>(value.size()) << 24U);
        Copy(info + 4U, value);
        Copy(info + kPropInfoBytes, name);
        Write(trie + 4U, info);
    }

    [[nodiscard]] std::uint32_t BytesUsed() const noexcept { return used_; }

    [[nodiscard]] const std::vector<std::byte>& Data() const noexcept { return data_; }

private:
    std::uint32_t FindOrAddChild(const std::uint32_t parent, const std::string_view segment) {
        if (Read(parent + 16U) == 0) return Link(parent + 16U, segment);
        auto node = Read(parent + 16U);
        while (true) {
            const auto length = static_cast<std::size_t>(data_[node]);
            const std::string_view current(reinterpret_cast<const char*>(data_.data() + node + kPropBtBytes), length);
            // cmp_prop_name: shorter names sort first, then strncmp.
            const int order = segment.size() != length ? (segment.size() < length ? -1 : 1) : segment.compare(current);
            if (order == 0) return node;
            const auto link = node + (order < 0 ? 8U : 12U);
            if (Read(link) == 0) return Link(link, segment);
            node = Read(link);
        }
    }

    std::uint32_t Link(const std::uint32_t slot, const std::string_view segment) {
        const auto node = Allocate(kPropBtBytes + static_cast<std::uint32_t>(segment.size()) + 1U);
        data_[node] = static_cast<std::byte>(segment.size());
        Copy(node + kPropBtBytes, segment);
        Write(slot, node);
        return node;
    }

    std::uint32_t Allocate(const std::uint32_t size) {
        const auto aligned = (size + 3U) & ~3U;
        if (aligned > data_.size() - used_) throw std::length_error("system properties exceed the prop_area");
        const auto offset = used_;
        used_ += aligned;
        return offset;
    }

    [[nodiscard]] std::uint32_t Read(const std::uint32_t offset) const {
        std::uint32_t value{};
        for (std::uint32_t index = 0; index < 4U; ++index)
            value |= static_cast<std::uint32_t>(data_[offset + index]) << (index * 8U);
        return value;
    }

    void Write(const std::uint32_t offset, const std::uint32_t value) {
        for (std::uint32_t index = 0; index < 4U; ++index)
            data_[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }

    void Copy(const std::uint32_t offset, const std::string_view text) {
        for (std::size_t index = 0; index < text.size(); ++index)
            data_[offset + index] = static_cast<std::byte>(static_cast<unsigned char>(text[index]));
    }

    std::vector<std::byte> data_;
    std::uint32_t used_{};
};

void Write32(memory::MemoryBus& bus, const memory::GuestAddress address,
             const std::uint32_t value, const std::uint64_t thread_id) {
    bus.Write32(address, value, thread_id);
}

void WriteString(memory::AddressSpace& address_space,
                 const memory::GuestAddress address,
                 const std::string_view value,
                 const std::uint64_t thread_id) {
    std::vector<std::byte> bytes;
    bytes.reserve(value.size() + 1);
    for (const auto character : value) {
        bytes.push_back(
            static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    bytes.push_back(std::byte{});
    address_space.Write(address, bytes, thread_id);
}

}  // namespace

Api19GuestProcessMemory InitializeApi19GuestProcess(
    memory::AddressSpace& address_space, memory::MemoryBus& memory_bus,
    const loader::Elf32LinkNamespace& link_namespace,
    const Api19GuestProcessRequest& request) {
    if (request.root_thread_id == 0 ||
        request.root_thread_id > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(
            "API 19 guest process root thread id is outside the 32-bit ABI");
    }
    if (request.program_name.empty() ||
        request.program_name.size() > kMaximumProgramNameBytes ||
        request.program_name.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(
            "API 19 guest process program name must contain 1..31 non-null bytes");
    }

    const auto exported =
        loader::LookupElf32Symbol(link_namespace, "__system_property_area__");
    const memory::GuestRange exported_slot{exported.address,
                                            sizeof(std::uint32_t)};
    address_space.Validate(exported_slot, memory::AccessType::write,
                           request.root_thread_id);
    const auto previous_property_area =
        memory_bus.Read32(exported.address, request.root_thread_id);

    const auto page_size = address_space.PageSize();
    const auto default_environment = GuestProcessEnvironment::Api19();
    const auto& environment = request.environment == nullptr
                                  ? default_environment
                                  : *request.environment;
    if (environment.SerializedBytes() > page_size) {
        throw std::invalid_argument(
            "API 19 guest environment exceeds one guest page");
    }
    const auto read_write =
        memory::PageProtection::read | memory::PageProtection::write;
    const memory::GuestRange thread_info{kApi19GuestThreadInfoAddress,
                                         page_size};
    const memory::GuestRange preinit{kApi19GuestPreinitAddress, page_size};
    const memory::GuestRange environment_page{kApi19GuestEnvironmentAddress,
                                               page_size};
    const memory::GuestRange stack{kApi19GuestStackAddress,
                                   kApi19GuestStackSize};
    const memory::GuestRange return_trap{kApi19GuestReturnAddress, page_size};
    const memory::GuestRange property_area{kApi19GuestPropertyAreaAddress, kApi19GuestPropertyAreaSize};
    bool thread_info_mapped{};
    bool preinit_mapped{};
    bool environment_mapped{};
    bool stack_mapped{};
    bool return_mapped{};
    bool property_mapped{};
    bool export_written{};
    std::optional<BionicTlsBlock> tls;

    try {
        address_space.Map(thread_info, read_write);
        thread_info_mapped = true;
        address_space.Map(preinit, read_write);
        preinit_mapped = true;
        address_space.Map(environment_page, read_write);
        environment_mapped = true;
        tls = CreateBionicTlsBlock(
            address_space, kApi19GuestTlsAddress,
            kApi19GuestThreadInfoAddress, kApi19GuestPreinitAddress);
        address_space.Map(stack, read_write);
        stack_mapped = true;
        address_space.Map(return_trap, read_write);
        return_mapped = true;
        address_space.Map(property_area, read_write);
        property_mapped = true;

        const auto thread_id =
            static_cast<std::uint32_t>(request.root_thread_id);
        const auto guest_page_size = static_cast<std::uint32_t>(page_size);
        Write32(memory_bus, kApi19GuestThreadInfoAddress.Add(12),
                kApi19GuestStackAddress.Value(), request.root_thread_id);
        Write32(memory_bus, kApi19GuestThreadInfoAddress.Add(16),
                static_cast<std::uint32_t>(kApi19GuestStackSize),
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestThreadInfoAddress.Add(20),
                guest_page_size, request.root_thread_id);
        Write32(memory_bus, kApi19GuestThreadInfoAddress.Add(32), thread_id,
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestThreadInfoAddress.Add(60),
                kApi19GuestTlsAddress.Value(), request.root_thread_id);

        const auto argv = kApi19GuestPreinitAddress.Add(0x40);
        const auto envp = kApi19GuestEnvironmentAddress;
        const auto auxv = kApi19GuestPreinitAddress.Add(0x60);
        const auto abort_message = kApi19GuestPreinitAddress.Add(0x80);
        const auto program_name = kApi19GuestPreinitAddress.Add(0xa0);
        const auto random_bytes = kApi19GuestPreinitAddress.Add(0xc0);
        Write32(memory_bus, kApi19GuestPreinitAddress, 1,
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPreinitAddress.Add(4), argv.Value(),
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPreinitAddress.Add(8), envp.Value(),
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPreinitAddress.Add(12), auxv.Value(),
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPreinitAddress.Add(16),
                abort_message.Value(), request.root_thread_id);
        Write32(memory_bus, argv, program_name.Value(), request.root_thread_id);
        Write32(memory_bus, argv.Add(4), 0, request.root_thread_id);
        auto string_address = envp.Add(
            (environment.Entries().size() + 1U) * sizeof(std::uint32_t));
        for (std::size_t index = 0; index < environment.Entries().size();
             ++index) {
            const auto& entry = environment.Entries()[index];
            Write32(memory_bus, envp.Add(index * sizeof(std::uint32_t)),
                    string_address.Value(), request.root_thread_id);
            const auto serialized = entry.name + "=" + entry.value;
            WriteString(address_space, string_address, serialized,
                        request.root_thread_id);
            string_address = string_address.Add(serialized.size() + 1U);
        }
        Write32(memory_bus,
                envp.Add(environment.Entries().size() * sizeof(std::uint32_t)),
                0, request.root_thread_id);
        Write32(memory_bus, auxv, 25, request.root_thread_id);
        Write32(memory_bus, auxv.Add(4), random_bytes.Value(),
                request.root_thread_id);
        Write32(memory_bus, auxv.Add(8), 6, request.root_thread_id);
        Write32(memory_bus, auxv.Add(12), guest_page_size,
                request.root_thread_id);
        Write32(memory_bus, auxv.Add(16), 0, request.root_thread_id);
        Write32(memory_bus, auxv.Add(20), 0, request.root_thread_id);
        Write32(memory_bus, abort_message, 0, request.root_thread_id);
        WriteString(address_space, program_name, request.program_name,
                    request.root_thread_id);
        Write32(memory_bus, random_bytes, 0x4f47504cU,
                request.root_thread_id);
        Write32(memory_bus, random_bytes.Add(4), 0x41594d34U,
                request.root_thread_id);
        Write32(memory_bus, random_bytes.Add(8), 0x10293847U,
                request.root_thread_id);
        Write32(memory_bus, random_bytes.Add(12), 0x56473829U,
                request.root_thread_id);

        Write32(memory_bus, kApi19GuestReturnAddress, 0xef000001U,
                request.root_thread_id);
        address_space.Protect(
            return_trap,
            memory::PageProtection::read | memory::PageProtection::execute);

        Write32(memory_bus, kApi19GuestPropertyAreaAddress, 20,
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPropertyAreaAddress.Add(4), 0,
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPropertyAreaAddress.Add(8), 0x504f5250U,
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPropertyAreaAddress.Add(12), 0xfc6ed0abU,
                request.root_thread_id);
        Write32(memory_bus, exported.address,
                kApi19GuestPropertyAreaAddress.Value(),
                request.root_thread_id);
        export_written = true;
        // map_prop_area() normally sets these after mapping the area; lookups
        // past pa_data_size fail, so publish the mapped extent as well.
        for (const auto& [name, value] :
             {std::pair{"pa_size", kApi19GuestPropertyAreaSize},
              std::pair{"pa_data_size", kApi19GuestPropertyAreaSize - kPropertyAreaHeaderBytes}}) {
            try {
                const auto symbol = loader::LookupElf32Symbol(link_namespace, name);
                Write32(memory_bus, symbol.address, value, request.root_thread_id);
            } catch (const loader::LinkError&) {
                // Minimal libc fixtures export only __system_property_area__.
            }
        }

        return {request.root_thread_id, tls->thread_pointer,
                kApi19GuestStackAddress.Add(kApi19GuestStackSize - 64U),
                kApi19GuestReturnAddress, kApi19GuestPropertyAreaAddress,
                envp};
    } catch (...) {
        if (export_written) {
            try {
                Write32(memory_bus, exported.address, previous_property_area,
                        request.root_thread_id);
            } catch (...) {
            }
        }
        if (property_mapped) address_space.Unmap(property_area);
        if (return_mapped) address_space.Unmap(return_trap);
        if (stack_mapped) address_space.Unmap(stack);
        if (tls.has_value()) DestroyBionicTlsBlock(address_space, *tls);
        if (environment_mapped) address_space.Unmap(environment_page);
        if (preinit_mapped) address_space.Unmap(preinit);
        if (thread_info_mapped) address_space.Unmap(thread_info);
        throw;
    }
}

void PublishApi19SystemProperties(memory::AddressSpace& address_space, memory::MemoryBus& memory_bus,
                                  const memory::GuestAddress property_area,
                                  const std::span<const std::pair<std::string, std::string>> properties,
                                  const std::uint64_t thread_id) {
    PropertyTrie trie(kApi19GuestPropertyAreaSize - kPropertyAreaHeaderBytes);
    for (const auto& [name, value] : properties)
        trie.Add(name, value);
    // Write the trie before the header so a concurrent reader never sees
    // bytes_used beyond initialized nodes.
    address_space.Write(property_area.Add(kPropertyAreaHeaderBytes), trie.Data(), thread_id);
    Write32(memory_bus, property_area, trie.BytesUsed(), thread_id);
    Write32(memory_bus, property_area.Add(8), kPropertyAreaMagic, thread_id);
    Write32(memory_bus, property_area.Add(12), kPropertyAreaVersion, thread_id);
}

}  // namespace ogplay::runtime

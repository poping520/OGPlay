#include "ogplay/runtime/dexvm/guest_memory.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm {
std::byte* GuestMemoryRuntime::Resolve(memory::GuestAddress address, std::uint32_t size, bool write) const {
    if (!access_.resolve) throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "guest memory is unavailable"};
    return access_.resolve(address, size, write);
}

std::uint32_t GuestMemoryRuntime::WithTemporaryGuestMemory(const std::span<const std::byte> bytes, const bool copy_back,
                                                           const GuestMemoryOperation& operation,
                                                           std::vector<std::byte>* const copied_back) {
    if (!operation) {
        throw std::invalid_argument("NIO guest memory operation is absent");
    }
    if (bytes.empty()) {
        if (copied_back != nullptr) copied_back->clear();
        return operation(memory::GuestAddress{});
    }
    if (!access_.allocate || !access_.release || !access_.write || (copy_back && !access_.read)) {
        throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "temporary guest memory is unavailable"};
    }
    const auto size = static_cast<std::uint32_t>(bytes.size());
    const auto address = access_.allocate(size);

    struct Release final {
        GuestMemoryAccess* access;
        memory::GuestAddress address;
        std::uint32_t size;

        ~Release() {
            try {
                access->release(address, size);
            } catch (const std::exception&) {
            }
        }
    } release{&access_, address, size};

    access_.write(address, bytes);
    const auto result = operation(address);
    if (copy_back) {
        std::vector<std::byte> output(bytes.size());
        access_.read(address, output);
        if (copied_back != nullptr) *copied_back = std::move(output);
    }
    return result;
}

}  // namespace ogplay::runtime::dexvm

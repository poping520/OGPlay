#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include "ogplay/memory/address.h"

namespace ogplay::runtime {

struct AssetFileDescriptor final {
    std::int32_t fd{-1};
    std::uint64_t start{}, length{};
};

// Sources retain their sealed backing independently of the manager and cursor.
// The concrete module does not depend on JNI, loader or VFS. No host address
// or host descriptor crosses the guest ABI.
class AndroidAssetSource {
public:
    virtual ~AndroidAssetSource() = default;
    virtual std::uint64_t Length() const = 0;
    virtual std::size_t ReadAt(std::uint64_t, std::span<std::byte>) const = 0;
    virtual AssetFileDescriptor OpenFileDescriptor() const = 0;
    virtual void CloseFileDescriptor(std::int32_t) const = 0;
};

class AndroidAssetManagerSource {
public:
    virtual ~AndroidAssetManagerSource() = default;
    virtual std::shared_ptr<const AndroidAssetSource> Open(std::string_view) = 0;
};

struct AssetManagerJavaReference final { std::uint32_t token{}; };
struct AndroidAssetManagerHooks final {
    void* owner{};
    memory::GuestAddress (*from_java)(void*, std::uint64_t,
        memory::GuestAddress, AssetManagerJavaReference){};
};

} // namespace ogplay::runtime

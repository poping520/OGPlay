#pragma once

#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_object.h"

#include <mutex>
#include <unordered_map>

namespace ogplay::runtime {

class TestJniStrings final : public JniStringBackend {
public:
    explicit TestJniStrings(JavaPayloadProvider& provider) : arrays_(provider) {}

    JniObjectIdentity Create(std::span<const JniChar> chars) override;
    bool Contains(JniObjectIdentity string) const noexcept override;
    JniSize Length(JniObjectIdentity string) const override;
    JniSize ModifiedUtf8Length(JniObjectIdentity string) const override;
    void ReadRegion(JniObjectIdentity string, JniSize start, JniSize length, const RegionReader& read) const override;
    std::vector<std::uint8_t> ModifiedUtf8Region(JniObjectIdentity string, JniSize start,
                                                 JniSize length) const override;
    JniStringAccess Acquire(JniObjectIdentity string, JniStringAccessKind kind) override;
    bool Release(JniObjectIdentity string, std::uint64_t token, JniStringAccessKind kind) override;

    void Delete(JniObjectIdentity string);
    std::size_t PinCount(JniObjectIdentity string) const;

private:
    JniPrimitiveArrayStore arrays_;
    std::mutex mutex_;
    std::unordered_map<std::uint64_t, JniObjectIdentity> leases_;
    std::uint64_t next_lease_{1};
};

}  // namespace ogplay::runtime

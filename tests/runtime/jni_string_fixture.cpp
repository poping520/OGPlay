#include "jni_string_fixture.h"

#include <algorithm>

#include "ogplay/runtime/jni/jni_utf.h"

namespace ogplay::runtime {

JniObjectIdentity TestJniStrings::Create(std::span<const JniChar> chars) {
    const auto id = arrays_.New(JniPrimitiveKind::character, static_cast<JniSize>(chars.size()));
    try {
        arrays_.SetRegion(id, 0, std::vector<JniChar>(chars.begin(), chars.end()));
    } catch (...) {
        arrays_.Delete(id);
        throw;
    }
    return id;
}

bool TestJniStrings::Contains(JniObjectIdentity string) const noexcept {
    try {
        return arrays_.Kind(string) == JniPrimitiveKind::character;
    } catch (const JniArrayError&) {
        return false;
    }
}

JniSize TestJniStrings::Length(JniObjectIdentity string) const {
    if (!Contains(string)) throw JniStringError(JniStringErrorReason::unknown_object, "JNI object is not a String");
    return arrays_.Length(string);
}

JniSize TestJniStrings::ModifiedUtf8Length(JniObjectIdentity string) const {
    return static_cast<JniSize>(JniModifiedUtf8Length(Region(string, 0, Length(string))));
}

void TestJniStrings::ReadRegion(JniObjectIdentity string, JniSize start, JniSize length,
                                const RegionReader& read) const {
    const auto size = Length(string);
    if (start < 0 || length < 0 || start > size || length > size - start)
        throw JniStringError(JniStringErrorReason::invalid_region, "invalid test string region");
    const auto view = arrays_.View(string);
    const auto chars = std::get<JavaPayloadAccessor<JniChar>>(view).ReadOnlySpan();
    read(chars.subspan(static_cast<std::size_t>(start), static_cast<std::size_t>(length)));
}

std::vector<std::uint8_t> TestJniStrings::ModifiedUtf8Region(JniObjectIdentity string, JniSize start,
                                                             JniSize length) const {
    return EncodeJniModifiedUtf8(Region(string, start, length));
}

JniStringAccess TestJniStrings::Acquire(JniObjectIdentity string, JniStringAccessKind kind) {
    std::scoped_lock lock(mutex_);
    JniStringAccess result;
    result.kind = kind;
    if (kind == JniStringAccessKind::modified_utf8) {
        result.modified_utf8 = ModifiedUtf8Region(string, 0, Length(string));
        result.token = next_lease_++;
        leases_.emplace(result.token, string);
    } else {
        const auto pin = arrays_.Acquire(string, JniArrayAccessKind::critical);
        result.address = pin.address;
        result.pin_count = pin.pin_count;
        result.warn_threshold = pin.warn_threshold;
        result.is_copy = false;
    }
    return result;
}

bool TestJniStrings::Release(JniObjectIdentity string, std::uint64_t token, JniStringAccessKind kind) {
    std::scoped_lock lock(mutex_);
    if (kind != JniStringAccessKind::modified_utf8) return arrays_.Release(string, JniArrayReleaseMode::abort);
    const auto it = leases_.find(token);
    if (it == leases_.end() || it->second != string)
        throw JniStringError(JniStringErrorReason::invalid_access, "invalid test string lease");
    leases_.erase(it);
    return true;
}

void TestJniStrings::Delete(JniObjectIdentity string) {
    std::scoped_lock lock(mutex_);
    if (arrays_.PinCount(string) ||
        std::ranges::any_of(leases_, [&](const auto& lease) { return lease.second == string; }))
        throw JniStringError(JniStringErrorReason::access_still_active, "test string is pinned");
    arrays_.Delete(string);
}

std::size_t TestJniStrings::PinCount(JniObjectIdentity string) const {
    return arrays_.PinCount(string);
}

}  // namespace ogplay::runtime

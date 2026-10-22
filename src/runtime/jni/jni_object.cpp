#include "ogplay/runtime/jni/jni_object.h"

#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <utility>
#include <optional>
#include <algorithm>

#include "ogplay/runtime/jni/jni_utf.h"

namespace ogplay::runtime {
namespace {

std::atomic<std::uint64_t> g_next_host_object{1};

[[noreturn]] void Fail(const JniStringErrorReason reason,
                       const char* message) {
    throw JniStringError(reason, message);
}

[[nodiscard]] std::size_t CheckedSize(const std::size_t value) {
    if (value > static_cast<std::size_t>(std::numeric_limits<JniSize>::max())) {
        throw std::length_error("JNI string exceeds jsize range");
    }
    return value;
}

}  // namespace

JniObjectIdentity AllocateJniHostObjectIdentity() {
    auto current = g_next_host_object.load(std::memory_order_relaxed);
    while (current != 0) {
        const auto next = current + 1;
        if (g_next_host_object.compare_exchange_weak(
                current, next, std::memory_order_relaxed)) {
            return {JniObjectDomain::host, current};
        }
    }
    throw std::length_error("JNI host object identity space exhausted");
}

JniStringError::JniStringError(const JniStringErrorReason reason,
                               const char* message)
    : std::runtime_error(message), reason_(reason) {}

JniStringErrorReason JniStringError::Reason() const noexcept { return reason_; }

class JniStringStore::Impl final {
public:
    explicit Impl(JavaPayloadProvider& value) : provider(value) {}

    struct StringEntry final {
        JavaPayloadHandle payload;
        std::optional<JavaPayloadAccessor<JniChar>> view;
        std::size_t pins{}, utf8_accesses{};
        std::uint64_t charge{};
        bool charged{}, warned{};
    };

    struct AccessEntry final {
        std::uint64_t string_identity{};
        JniStringAccessKind kind;
    };

    JavaPayloadProvider& provider;
    JavaPayloadAllocationContext context;
    JniStringAllocationStatistics stats;
    std::function<void(std::uint64_t, std::uint64_t)> teardown_observer;
    mutable std::mutex mutex_;
    std::map<std::uint64_t, StringEntry> strings_;
    std::map<std::uint64_t, AccessEntry> accesses_;
    std::uint64_t next_token_{1};

    ~Impl() {
        std::uint64_t count{}, bytes{};
        for (const auto& [id, e] : strings_) {
            static_cast<void>(id);
            if (e.pins) {
                ++count;
                bytes += e.payload.Bytes();
            }
        }
        if (count && teardown_observer) teardown_observer(count, bytes);
        for (auto& [id, e] : strings_) {
            static_cast<void>(id);
            e.view.reset();
            provider.Release(e.payload);
        }
    }

    auto RequireString(JniObjectIdentity string) {
        auto it = strings_.find(string.value);
        if (string.domain != JniObjectDomain::host || it == strings_.end())
            Fail(JniStringErrorReason::unknown_object, "JNI object is not a string in this store");
        return it;
    }

    auto RequireString(JniObjectIdentity string) const {
        auto it = strings_.find(string.value);
        if (string.domain != JniObjectDomain::host || it == strings_.end())
            Fail(JniStringErrorReason::unknown_object, "JNI object is not a string in this store");
        return it;
    }

    static std::vector<JniChar> Snapshot(const StringEntry& e, std::size_t offset, std::size_t count) {
        std::vector<JniChar> data(count);
        e.view->ReadRegion(offset, data.data(), count);
        return data;
    }

    JniObjectIdentity Create(std::span<const JniChar> utf16, bool include_header = true) {
        static_cast<void>(CheckedSize(utf16.size()));
        const auto guard = context.enter ? context.enter() : nullptr;
        std::function<void()> rollback;
        std::optional<JavaPayloadHandle> payload;
        const auto bytes = static_cast<std::uint64_t>(utf16.size()) * sizeof(JniChar);
        const auto charge = bytes + (include_header ? 32U : 0U);
        try {
            if (context.reserve) rollback = context.reserve(charge);
            payload = provider.Allocate(static_cast<std::size_t>(bytes), context.emergency && context.emergency());
            auto view = provider.Access<JniChar>(*payload);
            view.WriteRegion(0, utf16.data(), utf16.size());
            const auto id = AllocateJniHostObjectIdentity();
            std::scoped_lock lock(mutex_);
            strings_.emplace(id.value, StringEntry{*payload, std::move(view), 0, 0, charge,
                                                   static_cast<bool>(context.reserve), false});
            constexpr std::size_t bounds[]{0, 16, 64, 256, 1024, 8192};
            std::size_t bucket{};
            while (bucket < std::size(bounds) && utf16.size() > bounds[bucket])
                ++bucket;
            ++stats.allocations[bucket];
            stats.live_bytes += bytes;
            stats.peak_live_bytes = std::max(stats.peak_live_bytes, stats.live_bytes);
            return id;
        } catch (...) {
            if (payload) provider.Release(*payload);
            if (rollback) rollback();
            std::scoped_lock lock(mutex_);
            ++stats.allocation_failures;
            throw;
        }
    }

    std::uint64_t Delete(JniObjectIdentity string) {
        const auto guard = context.enter ? context.enter() : nullptr;
        std::uint64_t charge{};
        {
            std::scoped_lock lock(mutex_);
            auto found = RequireString(string);
            auto& e = found->second;
            if (e.pins || e.utf8_accesses)
                Fail(JniStringErrorReason::access_still_active, "JNI string still has active chars access");
            const auto bytes = e.payload.Bytes();
            e.view.reset();
            try {
                provider.Release(e.payload);
            } catch (...) {
                e.view = provider.Access<JniChar>(e.payload);
                throw;
            }
            stats.live_bytes -= bytes;
            if (e.charged) charge = e.charge;
            strings_.erase(found);
        }
        if (charge && context.refund) context.refund(charge);
        return charge;
    }

    bool Contains(JniObjectIdentity string) const noexcept {
        std::scoped_lock lock(mutex_);
        return string.domain == JniObjectDomain::host && strings_.contains(string.value);
    }
    void VisitAccessRoots(const std::function<void(JniObjectIdentity)>& visit) const {
        std::vector<JniObjectIdentity> roots;
        {
            std::scoped_lock lock(mutex_);
            for (const auto& [id, e] : strings_)
                if (e.pins || e.utf8_accesses) roots.push_back({JniObjectDomain::host, id});
        }
        for (auto id : roots)
            visit(id);
    }

    JniSize Length(JniObjectIdentity string) const {
        std::scoped_lock lock(mutex_);
        return static_cast<JniSize>(RequireString(string)->second.view->Size());
    }

    std::vector<JniChar> Region(JniObjectIdentity string, JniSize start, JniSize length) const {
        std::scoped_lock lock(mutex_);
        const auto& e = RequireString(string)->second;
        if (start < 0 || length < 0 || static_cast<std::size_t>(start) > e.view->Size() ||
            static_cast<std::size_t>(length) > e.view->Size() - static_cast<std::size_t>(start))
            Fail(JniStringErrorReason::invalid_region, "JNI string region is outside the object");
        return Snapshot(e, static_cast<std::size_t>(start), static_cast<std::size_t>(length));
    }

    JniSize ModifiedLength(JniObjectIdentity string) const {
        std::scoped_lock lock(mutex_);
        const auto& e = RequireString(string)->second;
        return static_cast<JniSize>(CheckedSize(JniModifiedUtf8Length(Snapshot(e, 0, e.view->Size()))));
    }

    JniStringAccess Acquire(JniObjectIdentity string, JniStringAccessKind kind) {
        const auto guard = context.enter ? context.enter() : nullptr;
        std::scoped_lock lock(mutex_);
        auto& e = RequireString(string)->second;
        JniStringAccess result;
        result.kind = kind;
        if (kind == JniStringAccessKind::modified_utf8) {
            result.modified_utf8 = EncodeJniModifiedUtf8(Snapshot(e, 0, e.view->Size()));
            if (!next_token_) throw std::length_error("JNI string access token space exhausted");
            result.token = next_token_++;
            accesses_.emplace(result.token, AccessEntry{string.value, kind});
            ++e.utf8_accesses;
        } else {
            result.address = e.payload.Address();
            if (e.pins == std::numeric_limits<std::size_t>::max()) throw std::bad_alloc();
            if (!e.pins) {
                ++stats.pinned_strings;
                stats.pinned_bytes += e.payload.Bytes();
            }
            ++e.pins;
            ++stats.pin_count;
            result.pin_count = e.pins;
            result.is_copy = false;
            result.warn_threshold = e.pins > 10 && !e.warned;
            if (result.warn_threshold) e.warned = true;
        }
        return result;
    }

    bool Release(JniObjectIdentity string, std::uint64_t token, JniStringAccessKind kind) {
        const auto guard = context.enter ? context.enter() : nullptr;
        std::scoped_lock lock(mutex_);
        auto& e = RequireString(string)->second;
        if (kind != JniStringAccessKind::modified_utf8) {
            if (!e.pins) return false;
            --e.pins;
            --stats.pin_count;
            if (!e.pins) {
                --stats.pinned_strings;
                stats.pinned_bytes -= e.payload.Bytes();
            }
            return true;
        }
        auto it = accesses_.find(token);
        if (it == accesses_.end() || it->second.string_identity != string.value)
            Fail(JniStringErrorReason::invalid_access, "JNI string access token is invalid for this object");
        if (it->second.kind != kind)
            Fail(JniStringErrorReason::access_kind_mismatch, "JNI string access release kind does not match acquire");
        accesses_.erase(it);
        --e.utf8_accesses;
        return true;
    }
};

JniStringStore::JniStringStore(JavaPayloadProvider& provider) : impl_(std::make_unique<Impl>(provider)) {}
JniStringStore::~JniStringStore() = default;
JniStringStore::JniStringStore(JniStringStore&&) noexcept = default;
JniStringStore& JniStringStore::operator=(JniStringStore&&) noexcept = default;

JniObjectIdentity JniStringStore::Create(const std::span<const JniChar> utf16, bool include_header) {
    return impl_->Create(utf16, include_header);
}

JniObjectIdentity JniStringStore::CreateModifiedUtf8(
    const std::span<const std::uint8_t> encoded) {
    const auto utf16 = DecodeJniModifiedUtf8(encoded);
    return impl_->Create(utf16);
}

std::uint64_t JniStringStore::Delete(const JniObjectIdentity string) {
    return impl_->Delete(string);
}
bool JniStringStore::Contains(const JniObjectIdentity string) const noexcept {
    return impl_->Contains(string);
}

void JniStringStore::VisitAccessRoots(const std::function<void(JniObjectIdentity)>& visit) const {
    impl_->VisitAccessRoots(visit);
}

JniSize JniStringStore::Length(const JniObjectIdentity string) const {
    return impl_->Length(string);
}

JniSize JniStringStore::ModifiedUtf8Length(
    const JniObjectIdentity string) const {
    return impl_->ModifiedLength(string);
}

std::vector<JniChar> JniStringStore::Region(const JniObjectIdentity string,
                                            const JniSize start,
                                            const JniSize length) const {
    return impl_->Region(string, start, length);
}

std::vector<std::uint8_t> JniStringStore::ModifiedUtf8Region(
    const JniObjectIdentity string, const JniSize start,
    const JniSize length) const {
    return EncodeJniModifiedUtf8(impl_->Region(string, start, length));
}

JniStringAccess JniStringStore::Acquire(const JniObjectIdentity string,
                                        const JniStringAccessKind kind) {
    return impl_->Acquire(string, kind);
}

bool JniStringStore::Release(const JniObjectIdentity string, const std::uint64_t token,
                             const JniStringAccessKind kind) {
    return impl_->Release(string, token, kind);
}

void JniStringStore::SetAllocationContext(JavaPayloadAllocationContext context) {
    if (static_cast<bool>(context.reserve) != static_cast<bool>(context.refund))
        throw std::invalid_argument("incomplete string allocation context");
    if (impl_->context.reserve && context.reserve)
        throw std::logic_error("string allocation context already installed");
    if (!context.reserve) {
        std::scoped_lock lock(impl_->mutex_);
        for (auto& [id, e] : impl_->strings_) {
            static_cast<void>(id);
            e.charged = false;
        }
    }
    impl_->context = std::move(context);
}

void JniStringStore::ConfigurePayloadHeap(std::uint64_t maximum, std::uint64_t growth) {
    impl_->provider.ConfigureHeap(maximum, growth);
}

void JniStringStore::EnsureCharged(JniObjectIdentity string) {
    const auto guard = impl_->context.enter ? impl_->context.enter() : nullptr;
    std::uint64_t charge{};
    {
        std::scoped_lock lock(impl_->mutex_);
        const auto& e = impl_->RequireString(string)->second;
        if (e.charged || !impl_->context.reserve) return;
        charge = e.charge;
    }
    auto rollback = impl_->context.reserve(charge);
    std::scoped_lock lock(impl_->mutex_);
    auto& e = impl_->RequireString(string)->second;
    if (e.charged)
        rollback();
    else
        e.charged = true;
}

JniStringAllocationStatistics JniStringStore::AllocationStatistics() const {
    std::scoped_lock lock(impl_->mutex_);
    return impl_->stats;
}

std::size_t JniStringStore::PinCount(JniObjectIdentity string) const {
    std::scoped_lock lock(impl_->mutex_);
    return impl_->RequireString(string)->second.pins;
}

void JniStringStore::SetPinTeardownObserver(std::function<void(std::uint64_t, std::uint64_t)> observer) {
    impl_->teardown_observer = std::move(observer);
}

}  // namespace ogplay::runtime

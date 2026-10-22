#include "ogplay/runtime/jni/jni_array.h"

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <type_traits>
#include <utility>

#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime {
namespace {

[[noreturn]] void Fail(const JniArrayErrorReason reason, const char* message) {
    throw JniArrayError(reason, message);
}

[[nodiscard]] std::size_t DataSize(const JniPrimitiveArrayData& data) {
    return std::visit([](const auto& values) { return values.size(); }, data);
}

[[nodiscard]] std::pair<std::size_t, std::size_t> ValidateRegion(
    const std::size_t size, const JniSize start, const JniSize length) {
    if (start < 0 || length < 0) {
        Fail(JniArrayErrorReason::invalid_region,
             "JNI array region has a negative bound");
    }
    const auto offset = static_cast<std::size_t>(start);
    const auto count = static_cast<std::size_t>(length);
    if (offset > size || count > size - offset) {
        Fail(JniArrayErrorReason::invalid_region,
             "JNI array region is outside the object");
    }
    return {offset, count};
}

}  // namespace

JniArrayError::JniArrayError(const JniArrayErrorReason reason,
                             const char* message)
    : std::runtime_error(message), reason_(reason) {}

JniArrayErrorReason JniArrayError::Reason() const noexcept { return reason_; }

class JniPrimitiveArrayStore::Impl final {
public:
    explicit Impl(JavaPayloadProvider& value) : provider(value) {}

    ~Impl() {
        // Owner has stopped and joined all guest threads and CPUs.
        std::uint64_t objects{}, bytes{};
        for (const auto& [id, entry] : arrays_) {
            static_cast<void>(id);
            if (entry.active_accesses) {
                ++objects;
                bytes += entry.payload.Bytes();
            }
        }
        if (objects && teardown_observer) teardown_observer(objects, bytes);
        for (auto& [id, entry] : arrays_) {
            static_cast<void>(id);
            entry.view.reset();
            provider.Release(entry.payload);
        }
    }

    struct ArrayEntry final {
        JniPrimitiveKind kind;
        JniSize length;
        JavaPayloadHandle payload;
        std::optional<JniPrimitiveArrayView> view;
        std::size_t active_accesses{};
        bool charged{};
        bool pin_warned{};
    };

    using Iterator = std::map<std::uint64_t, ArrayEntry>::iterator;
    using ConstIterator = std::map<std::uint64_t, ArrayEntry>::const_iterator;
    JavaPayloadProvider& provider;
    JniArrayAllocationContext context;
    JniArrayAllocationStatistics stats;
    mutable std::mutex mutex_;
    std::map<std::uint64_t, ArrayEntry> arrays_;
    std::function<void(std::uint64_t, std::uint64_t)> teardown_observer;

    static std::size_t Width(JniPrimitiveKind kind) {
        constexpr std::size_t widths[]{1, 1, 2, 2, 4, 8, 4, 8};
        if (static_cast<std::size_t>(kind) >= std::size(widths)) throw std::invalid_argument("invalid primitive kind");
        return widths[static_cast<std::size_t>(kind)];
    }

    JniPrimitiveArrayView MakeView(const JavaPayloadHandle& h, JniPrimitiveKind kind) {
        switch (kind) {
        case JniPrimitiveKind::boolean: return provider.Access<JniBoolean>(h);
        case JniPrimitiveKind::byte: return provider.Access<JniByte>(h);
        case JniPrimitiveKind::character: return provider.Access<JniChar>(h);
        case JniPrimitiveKind::short_integer: return provider.Access<JniShort>(h);
        case JniPrimitiveKind::integer: return provider.Access<JniInt>(h);
        case JniPrimitiveKind::long_integer: return provider.Access<JniLong>(h);
        case JniPrimitiveKind::float_value: return provider.Access<JniFloat>(h);
        case JniPrimitiveKind::double_value: return provider.Access<JniDouble>(h);
        }
        throw std::invalid_argument("invalid primitive kind");
    }

    Iterator Require(JniObjectIdentity array) {
        if (array.domain != JniObjectDomain::host) Unknown();
        const auto found = arrays_.find(array.value);
        if (found == arrays_.end()) Unknown();
        return found;
    }

    ConstIterator Require(JniObjectIdentity array) const {
        if (array.domain != JniObjectDomain::host) Unknown();
        const auto found = arrays_.find(array.value);
        if (found == arrays_.end()) Unknown();
        return found;
    }

    [[noreturn]] static void Unknown() { Fail(JniArrayErrorReason::unknown_object, "unknown JNI primitive array"); }

    [[noreturn]] static void TypeMismatch() {
        Fail(JniArrayErrorReason::type_mismatch, "JNI primitive array type mismatch");
    }

    static JniPrimitiveArrayData Snapshot(const ArrayEntry& e, JniSize start, JniSize length) {
        const auto [offset, count] = ValidateRegion(static_cast<std::size_t>(e.length), start, length);
        return std::visit(
            [&](const auto& view) -> JniPrimitiveArrayData {
                using T = decltype(view.Read(0));
                std::vector<T> data(count);
                view.ReadRegion(offset, data.data(), count);
                return data;
            },
            *e.view);
    }

    static void Write(ArrayEntry& e, JniSize start, const JniPrimitiveArrayData& data) {
        if (static_cast<std::size_t>(e.kind) != data.index()) TypeMismatch();
        const auto count = DataSize(data);
        if (count > static_cast<std::size_t>(INT32_MAX))
            Fail(JniArrayErrorReason::invalid_region, "JNI region exceeds jsize");
        const auto [offset, checked] =
            ValidateRegion(static_cast<std::size_t>(e.length), start, static_cast<JniSize>(count));
        std::visit(
            [&](const auto& view) {
                using T = decltype(view.Read(0));
                const auto& values = std::get<std::vector<T>>(data);
                view.WriteRegion(offset, values.data(), checked);
            },
            *e.view);
    }

    JniObjectIdentity New(JniPrimitiveKind kind, JniSize length) {
        if (length < 0) Fail(JniArrayErrorReason::invalid_length, "JNI array length cannot be negative");
        const auto bytes = static_cast<std::uint64_t>(length) * Width(kind);
        const auto guard = context.enter ? context.enter() : nullptr;
        bool charged = false;
        std::function<void()> rollback;
        std::optional<JavaPayloadHandle> payload;
        try {
            if (context.reserve) {
                rollback = context.reserve(32 + bytes);
                charged = true;
            }
            payload = provider.Allocate(static_cast<std::size_t>(bytes), context.emergency && context.emergency());
            ArrayEntry entry{kind, length, *payload, MakeView(*payload, kind), 0, charged};
            const auto identity = AllocateJniHostObjectIdentity();
            std::scoped_lock lock(mutex_);
            arrays_.emplace(identity.value, std::move(entry));
            constexpr JniSize bounds[]{0, 16, 64, 256, 1024, 8192};
            std::size_t bucket{};
            while (bucket < std::size(bounds) && length > bounds[bucket])
                ++bucket;
            ++stats.allocations[static_cast<std::size_t>(kind)][bucket];
            stats.live_bytes += bytes;
            stats.peak_live_bytes = std::max(stats.peak_live_bytes, stats.live_bytes);
            return identity;
        } catch (...) {
            if (payload) provider.Release(*payload);
            if (rollback) rollback();
            {
                std::scoped_lock lock(mutex_);
                ++stats.allocation_failures;
            }
            throw;
        }
    }

    void Delete(JniObjectIdentity array) {
        const auto guard = context.enter ? context.enter() : nullptr;
        std::uint64_t charge{};
        {
            std::scoped_lock lock(mutex_);
            const auto found = Require(array);
            auto& e = found->second;
            if (e.active_accesses)
                Fail(JniArrayErrorReason::access_still_active, "JNI array still has active elements access");
            const auto bytes = e.payload.Bytes();
            e.view.reset();
            try {
                provider.Release(e.payload);
            } catch (...) {
                e.view = MakeView(e.payload, e.kind);
                throw;
            }
            if (e.charged) charge = bytes + 32;
            stats.live_bytes -= bytes;
            arrays_.erase(found);
        }
        if (charge && context.refund) context.refund(charge);
    }

    JniSize Length(JniObjectIdentity array) const {
        std::scoped_lock lock(mutex_);
        return Require(array)->second.length;
    }

    JniPrimitiveKind Kind(JniObjectIdentity array) const {
        std::scoped_lock lock(mutex_);
        return Require(array)->second.kind;
    }

    JniPrimitiveArrayData Region(JniObjectIdentity array, JniSize start, JniSize length) const {
        std::scoped_lock lock(mutex_);
        return Snapshot(Require(array)->second, start, length);
    }

    void SetRegion(JniObjectIdentity array, JniSize start, const JniPrimitiveArrayData& data) {
        std::scoped_lock lock(mutex_);
        Write(Require(array)->second, start, data);
    }

    JniArrayAccess Acquire(JniObjectIdentity array, JniArrayAccessKind kind) {
        const auto guard = context.enter ? context.enter() : nullptr;
        std::scoped_lock lock(mutex_);
        auto& e = Require(array)->second;
        static_cast<void>(kind);                   // Elements and Critical share the Dalvik pin table.
        const auto address = e.payload.Address();  // host-copy is explicitly unsupported.
        if (e.active_accesses == std::numeric_limits<std::size_t>::max()) throw std::bad_alloc{};
        if (!e.active_accesses) {
            ++stats.pinned_arrays;
            stats.pinned_bytes += e.payload.Bytes();
        }
        ++e.active_accesses;
        ++stats.pin_count;
        const bool warn = e.active_accesses > 10 && !e.pin_warned;
        if (warn) e.pin_warned = true;
        return {address, e.active_accesses, warn, false};
    }

    bool Release(JniObjectIdentity array, JniArrayReleaseMode mode) {
        const auto guard = context.enter ? context.enter() : nullptr;
        std::scoped_lock lock(mutex_);
        auto& e = Require(array)->second;
        if (mode == JniArrayReleaseMode::commit) return true;
        if (!e.active_accesses) return false;
        --e.active_accesses;
        --stats.pin_count;
        if (!e.active_accesses) {
            --stats.pinned_arrays;
            stats.pinned_bytes -= e.payload.Bytes();
        }
        return true;
    }
};

JniPrimitiveArrayStore::JniPrimitiveArrayStore(JavaPayloadProvider& provider)
    : impl_(std::make_unique<Impl>(provider)) {}
JniPrimitiveArrayStore::~JniPrimitiveArrayStore() = default;
JniPrimitiveArrayStore::JniPrimitiveArrayStore(
    JniPrimitiveArrayStore&&) noexcept = default;
JniPrimitiveArrayStore& JniPrimitiveArrayStore::operator=(
    JniPrimitiveArrayStore&&) noexcept = default;

JniObjectIdentity JniPrimitiveArrayStore::New(const JniPrimitiveKind kind,
                                              const JniSize length) {
    return impl_->New(kind, length);
}
void JniPrimitiveArrayStore::Delete(const JniObjectIdentity array) {
    impl_->Delete(array);
}
JniSize JniPrimitiveArrayStore::Length(const JniObjectIdentity array) const {
    return impl_->Length(array);
}
JniPrimitiveKind JniPrimitiveArrayStore::Kind(
    const JniObjectIdentity array) const {
    return impl_->Kind(array);
}
JniPrimitiveArrayData JniPrimitiveArrayStore::Region(
    const JniObjectIdentity array, const JniSize start,
    const JniSize length) const {
    return impl_->Region(array, start, length);
}
void JniPrimitiveArrayStore::SetRegion(
    const JniObjectIdentity array, const JniSize start,
    const JniPrimitiveArrayData& data) {
    impl_->SetRegion(array, start, data);
}
JniArrayAccess JniPrimitiveArrayStore::Acquire(
    const JniObjectIdentity array, const JniArrayAccessKind kind) {
    return impl_->Acquire(array, kind);
}

bool JniPrimitiveArrayStore::Release(JniObjectIdentity array, JniArrayReleaseMode mode) {
    return impl_->Release(array, mode);
}

std::size_t JniPrimitiveArrayStore::PinCount(JniObjectIdentity array) const {
    std::scoped_lock lock(impl_->mutex_);
    return impl_->Require(array)->second.active_accesses;
}

void JniPrimitiveArrayStore::SetPinTeardownObserver(std::function<void(std::uint64_t, std::uint64_t)> observer) {
    impl_->teardown_observer = std::move(observer);
}

}  // namespace ogplay::runtime

namespace ogplay::runtime {
void JniPrimitiveArrayStore::SetAllocationContext(JniArrayAllocationContext context) {
    if (static_cast<bool>(context.reserve) != static_cast<bool>(context.refund))
        throw std::invalid_argument("incomplete array allocation context");
    if (impl_->context.reserve && context.reserve) throw std::logic_error("array allocation context already installed");
    if (!context.reserve) {
        std::scoped_lock lock(impl_->mutex_);
        for (auto& [identity, entry] : impl_->arrays_) {
            static_cast<void>(identity);
            entry.charged = false;
        }
    }
    impl_->context = std::move(context);
}

void JniPrimitiveArrayStore::EnsureCharged(JniObjectIdentity array) {
    const auto guard = impl_->context.enter ? impl_->context.enter() : nullptr;
    std::uint64_t bytes{};
    {
        std::scoped_lock lock(impl_->mutex_);
        const auto& e = impl_->Require(array)->second;
        if (e.charged || !impl_->context.reserve) return;
        bytes = 32 + e.payload.Bytes();
    }
    auto rollback = impl_->context.reserve(bytes);
    std::scoped_lock lock(impl_->mutex_);
    auto& e = impl_->Require(array)->second;
    if (e.charged)
        rollback();
    else
        e.charged = true;
}

void JniPrimitiveArrayStore::VisitAccessRoots(const std::function<void(JniObjectIdentity)>& visit) const {
    std::vector<JniObjectIdentity> roots;
    {
        std::scoped_lock lock(impl_->mutex_);
        for (const auto& [id, e] : impl_->arrays_)
            if (e.active_accesses) roots.push_back({JniObjectDomain::host, id});
    }
    for (const auto root : roots)
        visit(root);
}

JniPrimitiveArrayView JniPrimitiveArrayStore::View(JniObjectIdentity array) const {
    std::scoped_lock lock(impl_->mutex_);
    return *impl_->Require(array)->second.view;
}

JniArrayAllocationStatistics JniPrimitiveArrayStore::AllocationStatistics() const {
    std::scoped_lock lock(impl_->mutex_);
    return impl_->stats;
}

void JniPrimitiveArrayStore::Copy(JniObjectIdentity source, JniSize from, JniObjectIdentity target, JniSize to,
                                  JniSize length) {
    std::scoped_lock lock(impl_->mutex_);
    const auto& s = impl_->Require(source)->second;
    const auto& t = impl_->Require(target)->second;
    if (s.kind != t.kind) Impl::TypeMismatch();
    const auto [src, count] = ValidateRegion(static_cast<std::size_t>(s.length), from, length);
    const auto [dst, ignored] = ValidateRegion(static_cast<std::size_t>(t.length), to, length);
    static_cast<void>(ignored);
    std::visit(
        [&](const auto& view) {
            using V = std::decay_t<decltype(view)>;
            std::get<V>(*t.view).CopyFrom(view, src, dst, count);
        },
        *s.view);
}

void JniPrimitiveArrayStore::CopyWithin(JniObjectIdentity array, JniSize from, JniSize to, JniSize length) {
    Copy(array, from, array, to, length);
}
}  // namespace ogplay::runtime

namespace ogplay::runtime {
void JniPrimitiveArrayStore::ConfigurePayloadHeap(std::uint64_t maximum, std::uint64_t growth) {
    impl_->provider.ConfigureHeap(maximum, growth);
}
}  // namespace ogplay::runtime

#include "ogplay/runtime/dexvm/object_model.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ogplay::runtime::dexvm {
namespace {

[[noreturn]] void Fail(const DexVmErrorReason reason, const char* message) {
    throw DexVmError(reason, message);
}

struct IdentityHash final {
    [[nodiscard]] std::size_t operator()(
        const JniObjectIdentity& identity) const noexcept {
        return std::hash<std::uint64_t>{}(identity.value) ^
               (static_cast<std::size_t>(identity.domain) << 1U);
    }
};

[[nodiscard]] std::uint32_t PrimitiveWidth(const JniPrimitiveKind kind) {
    switch (kind) {
        case JniPrimitiveKind::boolean:
        case JniPrimitiveKind::byte:
            return 1;
        case JniPrimitiveKind::character:
        case JniPrimitiveKind::short_integer:
            return 2;
        case JniPrimitiveKind::integer:
        case JniPrimitiveKind::float_value:
            return 4;
        case JniPrimitiveKind::long_integer:
        case JniPrimitiveKind::double_value:
            return 8;
    }
    Fail(DexVmErrorReason::object_model_failure,
         "primitive array kind is invalid");
}

}  // namespace

class JavaObjectModel::Impl final {
public:
    struct Record final {
        bool occupied{true};
        VmObjectKind kind{VmObjectKind::external};
        JniObjectIdentity identity;
        DexClassId java_class;
        std::uint32_t storage{};  // index into kind-specific storage
        std::uint64_t host_state{};
        std::uint64_t reserved_bytes{};
        JniPrimitiveKind primitive_kind{JniPrimitiveKind::integer};
        std::uint32_t identity_hash{};
    };

    struct ObjectArray final {
        DexClassId element_class;
        std::vector<VmObjectRef> elements;
    };

    JniStringStore* strings{};
    JniPrimitiveArrayStore* arrays{};
    JavaObjectInterop interop;
    JavaObjectModelConfig config;

    std::vector<Record> records;
    std::vector<std::uint32_t> free_records;
    std::unordered_map<JniObjectIdentity, std::uint32_t, IdentityHash>
        by_identity;
    std::vector<std::vector<Slot>> instance_storage;
    std::vector<std::uint32_t> free_instance_storage;
    // Isolated unit fixtures may omit JNI interop. Production sessions use
    // the injected JniObjectArrayStore and never populate this fallback.
    std::vector<ObjectArray> fallback_object_arrays;
    std::vector<std::uint32_t> free_fallback_object_arrays;
    std::unordered_map<std::u16string, VmObjectRef> intern_table;
    std::unordered_map<std::uint32_t, VmObjectRef> class_objects;
    std::function<std::string(DexClassId)> class_descriptor_resolver;

    DexClassId string_class;
    DexClassId class_class;
    std::uint64_t allocated_bytes{};
    std::uint64_t object_count{};
    std::uint64_t next_vm_identity{1};
    std::uint64_t next_identity_hash{1};

    [[nodiscard]] std::uint32_t NextIdentityHash() {
        if (next_identity_hash > std::numeric_limits<std::uint32_t>::max()) {
            Fail(DexVmErrorReason::object_model_failure,
                 "Java identity hash space is exhausted");
        }
        return static_cast<std::uint32_t>(next_identity_hash++);
    }

    [[nodiscard]] static std::uint32_t ClassIdentityHash(
        const std::string_view descriptor) noexcept {
        // Fixed FNV-1a over the stable descriptor. Identity hash collisions
        // are legal; zero is reserved here for null/unassigned records.
        std::uint32_t hash = 2166136261U;
        for (const auto byte : descriptor) {
            hash ^= static_cast<std::uint8_t>(
                static_cast<unsigned char>(byte));
            hash *= 16777619U;
        }
        return hash == 0U ? 1U : hash;
    }

    [[nodiscard]] VmObjectRef Register(Record record) {
        if (record.identity_hash == 0U) {
            record.identity_hash = NextIdentityHash();
        }
        std::uint32_t index{};
        if (free_records.empty()) {
            index = static_cast<std::uint32_t>(records.size());
            records.push_back(std::move(record));
        } else {
            index = free_records.back();
            free_records.pop_back();
            records[index] = std::move(record);
        }
        const auto handle = VmObjectRef(index + 1U);
        by_identity.emplace(records[index].identity, index);
        ++object_count;
        return handle;
    }

    [[nodiscard]] Record& At(const VmObjectRef ref) {
        if (!ref.IsValid() || ref.Value() > records.size()) {
            Fail(DexVmErrorReason::object_model_failure,
                 "object reference is invalid");
        }
        auto& record = records[ref.Value() - 1];
        if (!record.occupied) {
            throw DexVmError(
                DexVmErrorReason::object_model_failure,
                "object reference " + std::to_string(ref.Value()) +
                    " names a reclaimed record");
        }
        return record;
    }
    [[nodiscard]] const Record& At(const VmObjectRef ref) const {
        if (!ref.IsValid() || ref.Value() > records.size()) {
            Fail(DexVmErrorReason::object_model_failure,
                 "object reference is invalid");
        }
        const auto& record = records[ref.Value() - 1];
        if (!record.occupied) {
            throw DexVmError(
                DexVmErrorReason::object_model_failure,
                "object reference " + std::to_string(ref.Value()) +
                    " names a reclaimed record");
        }
        return record;
    }

    bool emergency_reserve{};

    void Reserve(const std::uint64_t bytes) {
        if (emergency_reserve) {
            allocated_bytes += bytes;
            return;
        }
        if (allocated_bytes + bytes > config.heap_budget_bytes) {
            throw DexVmError(DexVmErrorReason::heap_budget_exhausted,
                             "dexvm heap budget exhausted: allocated " +
                                 std::to_string(allocated_bytes) +
                                 " bytes, requested " + std::to_string(bytes) +
                                 ", budget " +
                                 std::to_string(config.heap_budget_bytes));
        }
        allocated_bytes += bytes;
    }

    [[nodiscard]] JniObjectIdentity NextVmIdentity() {
        return JniObjectIdentity{JniObjectDomain::dex_vm, next_vm_identity++};
    }
};

JavaObjectModel::JavaObjectModel(JniStringStore& strings,
                                 JniPrimitiveArrayStore& arrays,
                                 JavaObjectModelConfig config,
                                 JavaObjectInterop interop)
    : impl_(std::make_unique<Impl>()) {
    impl_->strings = &strings;
    impl_->arrays = &arrays;
    impl_->config = config;
    impl_->interop = std::move(interop);
    if (config.gc_watermark_percent > 100U) {
        throw std::invalid_argument("DexVM GC watermark must be in 0..100");
    }
}

JavaObjectModel::~JavaObjectModel() = default;

void JavaObjectModel::SetCoreClasses(const DexClassId string_class,
                                     const DexClassId class_class) {
    impl_->string_class = string_class;
    impl_->class_class = class_class;
}

void JavaObjectModel::SetClassDescriptorResolver(
    std::function<std::string(DexClassId)> resolver) {
    if (!resolver) {
        throw std::invalid_argument(
            "DexVM class descriptor resolver must not be empty");
    }
    impl_->class_descriptor_resolver = std::move(resolver);
}

VmObjectRef JavaObjectModel::FromIdentity(const JniObjectIdentity identity) {
    if (identity == JniObjectIdentity{}) return VmObjectRef{};
    const auto found = impl_->by_identity.find(identity);
    if (found != impl_->by_identity.end()) {
        return VmObjectRef(found->second + 1);
    }
    Impl::Record record;
    record.identity = identity;
    // Objects created on the JNI side (NewStringUTF, NewByteArray, ...)
    // flow in with host identities; classify them against the shared
    // stores so the interpreter sees the same object with its real form.
    record.kind = VmObjectKind::external;
    try {
        static_cast<void>(impl_->strings->Length(identity));
        record.kind = VmObjectKind::string;
        record.java_class = impl_->string_class;
    } catch (const JniStringError&) {
        try {
            record.primitive_kind = impl_->arrays->Kind(identity);
            record.kind = VmObjectKind::primitive_array;
        } catch (const JniArrayError&) {
            if (impl_->interop.object_arrays != nullptr &&
                impl_->interop.object_arrays->Contains(identity)) {
                if (!impl_->interop.resolve_object_array_class) {
                    Fail(DexVmErrorReason::object_model_failure,
                         "JNI object array class resolver is not installed");
                }
                const auto [array_class, element_class] =
                    impl_->interop.resolve_object_array_class(
                        impl_->interop.object_arrays->ElementClass(identity));
                static_cast<void>(element_class);
                record.kind = VmObjectKind::object_array;
                record.java_class = array_class;
                record.reserved_bytes =
                    EstimateObjectArrayBytes(
                        impl_->interop.object_arrays->Length(identity));
                impl_->Reserve(record.reserved_bytes);
            } else {
                record.kind = VmObjectKind::external;
                if (impl_->interop.resolve_object_class) {
                    record.java_class =
                        impl_->interop.resolve_object_class(identity);
                }
            }
        }
    }
    return impl_->Register(std::move(record));
}

JniObjectIdentity JavaObjectModel::ToIdentity(const VmObjectRef ref) const {
    if (!ref.IsValid()) return {};
    return impl_->At(ref).identity;
}

VmObjectKind JavaObjectModel::Kind(const VmObjectRef ref) const {
    return impl_->At(ref).kind;
}

DexClassId JavaObjectModel::ObjectClass(const VmObjectRef ref) const {
    const auto& record = impl_->At(ref);
    if (record.kind == VmObjectKind::string &&
        !record.java_class.IsValid()) {
        return impl_->string_class;
    }
    return record.java_class;
}

std::int32_t JavaObjectModel::IdentityHashCode(const VmObjectRef ref) const {
    if (!ref.IsValid()) return 0;
    return static_cast<std::int32_t>(impl_->At(ref).identity_hash);
}

bool JavaObjectModel::IsValidRef(const VmObjectRef ref) const noexcept {
    return ref.IsValid() && ref.Value() <= impl_->records.size() &&
           impl_->records[ref.Value() - 1U].occupied;
}

VmObjectRef JavaObjectModel::FindIdentity(
    const JniObjectIdentity identity) const noexcept {
    const auto found = impl_->by_identity.find(identity);
    return found == impl_->by_identity.end()
               ? VmObjectRef{}
               : VmObjectRef(found->second + 1U);
}

void JavaObjectModel::VisitPermanentRoots(const RootVisitor& visitor) const {
    if (!visitor) return;
    for (const auto& [_, ref] : impl_->intern_table) visitor(ref);
    for (const auto& [_, ref] : impl_->class_objects) visitor(ref);
}

GcMarkResult JavaObjectModel::MarkReachable(
    const std::vector<VmObjectRef>& roots,
    const std::function<void(VmObjectRef, const RootVisitor&)>&
        trace_host_edges) {
    // JNI may have populated an object array without entering dexvm for each
    // element. Import those identities before sizing the mark bitmap so the
    // array edge set is complete and stable for this collection.
    if (impl_->interop.object_arrays != nullptr) {
        const auto records_before = impl_->records.size();
        for (std::size_t index = 0; index < records_before; ++index) {
            const auto identity = impl_->records[index].identity;
            const auto kind = impl_->records[index].kind;
            const auto occupied = impl_->records[index].occupied;
            if (!occupied || kind != VmObjectKind::object_array) {
                continue;
            }
            const auto length =
                impl_->interop.object_arrays->Length(identity);
            for (JniSize element = 0; element < length; ++element) {
                const auto value =
                    impl_->interop.object_arrays->Get(identity, element);
                if (value.has_value()) {
                    static_cast<void>(FromIdentity(value->object));
                }
            }
        }
    }
    GcMarkResult result;
    result.marked.assign(impl_->records.size(), false);
    std::vector<VmObjectRef> gray;
    const RootVisitor mark = [&](const VmObjectRef ref) {
        if (!ref.IsValid() || ref.Value() > impl_->records.size()) return;
        const auto index = static_cast<std::size_t>(ref.Value() - 1U);
        if (result.marked[index]) return;
        result.marked[index] = true;
        gray.push_back(ref);
    };
    for (const auto root : roots) mark(root);
    while (!gray.empty()) {
        const auto ref = gray.back();
        gray.pop_back();
        const auto& record = impl_->At(ref);
        if (record.java_class.IsValid()) {
            const auto class_object =
                impl_->class_objects.find(record.java_class.Value());
            if (class_object != impl_->class_objects.end()) {
                mark(class_object->second);
            }
        }
        switch (record.kind) {
            case VmObjectKind::vm_instance:
                for (const auto& slot : impl_->instance_storage[record.storage]) {
                    if (slot.tag == SlotTag::ref) mark(VmObjectRef(slot.bits));
                }
                break;
            case VmObjectKind::object_array:
                if (impl_->interop.object_arrays != nullptr) {
                    for (JniSize index = 0;
                         index < impl_->interop.object_arrays->Length(record.identity);
                         ++index) {
                        const auto value = impl_->interop.object_arrays->Get(
                            record.identity, index);
                        if (value.has_value()) {
                            mark(FindIdentity(value->object));
                        }
                    }
                } else {
                    for (const auto element :
                         impl_->fallback_object_arrays[record.storage].elements) {
                        mark(element);
                    }
                }
                break;
            case VmObjectKind::host_backed:
                break;
            case VmObjectKind::string:
            case VmObjectKind::primitive_array:
            case VmObjectKind::class_object:
            case VmObjectKind::external:
                break;
        }
        // Registered side tables are logical instance fields. The current
        // four owners are ordinary intrinsic/throwable instances, while the
        // hook remains valid for future host-backed declarations as well.
        if (trace_host_edges) trace_host_edges(ref, mark);
    }
    for (std::size_t index = 0; index < impl_->records.size(); ++index) {
        if (!impl_->records[index].occupied) continue;
        const auto bytes = impl_->records[index].reserved_bytes;
        if (result.marked[index]) {
            ++result.live_objects;
            result.live_bytes += bytes;
        } else {
            ++result.garbage_objects;
            result.garbage_bytes += bytes;
        }
    }
    return result;
}

GcSweepResult JavaObjectModel::Sweep(const GcMarkResult& mark,
                                     const GcSweepHooks& hooks) {
    if (mark.marked.size() != impl_->records.size()) {
        Fail(DexVmErrorReason::internal_invariant,
             "GC mark bitmap does not match the object record table");
    }
    GcSweepResult result;
    for (std::size_t index = 0; index < impl_->records.size(); ++index) {
        auto& record = impl_->records[index];
        if (!record.occupied || mark.marked[index]) continue;
        const auto ref = VmObjectRef(static_cast<std::uint32_t>(index + 1U));
        if (hooks.before_release &&
            hooks.before_release(ref, record.kind, record.java_class,
                                 record.host_state)) {
            ++result.host_destructors_run;
        }
        switch (record.kind) {
            case VmObjectKind::string:
                impl_->strings->Delete(record.identity);
                break;
            case VmObjectKind::primitive_array:
                impl_->arrays->Delete(record.identity);
                break;
            case VmObjectKind::vm_instance:
                impl_->instance_storage[record.storage].clear();
                impl_->free_instance_storage.push_back(record.storage);
                break;
            case VmObjectKind::object_array:
                if (impl_->interop.object_arrays != nullptr) {
                    impl_->interop.object_arrays->Delete(record.identity);
                } else {
                    auto& array =
                        impl_->fallback_object_arrays[record.storage];
                    array.elements.clear();
                    array.element_class = DexClassId{};
                    impl_->free_fallback_object_arrays.push_back(
                        record.storage);
                }
                break;
            case VmObjectKind::host_backed:
            case VmObjectKind::class_object:
            case VmObjectKind::external:
                break;
        }
        if (hooks.release_external_state) {
            hooks.release_external_state(ref, record.identity);
        }
        impl_->by_identity.erase(record.identity);
        result.freed_bytes += record.reserved_bytes;
        ++result.freed_objects;
        impl_->allocated_bytes -= record.reserved_bytes;
        --impl_->object_count;
        record.occupied = false;
        impl_->free_records.push_back(static_cast<std::uint32_t>(index));
    }
    return result;
}

VmObjectRef JavaObjectModel::NewInstance(const DexClassId java_class,
                                         const std::uint16_t slot_count) {
    const auto bytes = 32ULL + static_cast<std::uint64_t>(slot_count) * 8ULL;
    impl_->Reserve(bytes);
    Impl::Record record;
    record.kind = VmObjectKind::vm_instance;
    record.identity = impl_->NextVmIdentity();
    record.java_class = java_class;
    record.reserved_bytes = bytes;
    if (impl_->free_instance_storage.empty()) {
        record.storage = static_cast<std::uint32_t>(
            impl_->instance_storage.size());
        impl_->instance_storage.emplace_back(slot_count, Slot{});
    } else {
        record.storage = impl_->free_instance_storage.back();
        impl_->free_instance_storage.pop_back();
        impl_->instance_storage[record.storage].assign(slot_count, Slot{});
    }
    return impl_->Register(std::move(record));
}

std::span<Slot> JavaObjectModel::InstanceSlots(const VmObjectRef ref) {
    auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::vm_instance) {
        Fail(DexVmErrorReason::object_model_failure,
             "object has no field slot board");
    }
    return impl_->instance_storage[record.storage];
}

VmObjectRef JavaObjectModel::NewHostBacked(const DexClassId java_class,
                                           const std::uint64_t host_state) {
    impl_->Reserve(48ULL);
    Impl::Record record;
    record.kind = VmObjectKind::host_backed;
    record.identity = impl_->NextVmIdentity();
    record.java_class = java_class;
    record.host_state = host_state;
    record.reserved_bytes = 48ULL;
    return impl_->Register(std::move(record));
}

std::uint64_t JavaObjectModel::HostState(const VmObjectRef ref) const {
    const auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::host_backed) {
        Fail(DexVmErrorReason::object_model_failure,
             "object has no host state");
    }
    return record.host_state;
}

void JavaObjectModel::SetHostState(const VmObjectRef ref,
                                   const std::uint64_t host_state) {
    auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::host_backed) {
        Fail(DexVmErrorReason::object_model_failure,
             "object has no host state");
    }
    record.host_state = host_state;
}

VmObjectRef JavaObjectModel::NewString(const std::u16string_view value) {
    const auto bytes = 32ULL + value.size() * 2ULL;
    impl_->Reserve(bytes);
    std::vector<JniChar> units(value.begin(), value.end());
    const auto identity = impl_->strings->Create(units);
    Impl::Record record;
    record.kind = VmObjectKind::string;
    record.identity = identity;
    record.java_class = impl_->string_class;
    record.reserved_bytes = bytes;
    return impl_->Register(std::move(record));
}

VmObjectRef JavaObjectModel::InternString(const std::u16string_view value) {
    const std::u16string key(value);
    const auto found = impl_->intern_table.find(key);
    if (found != impl_->intern_table.end()) return found->second;
    const auto created = NewString(value);
    impl_->intern_table.emplace(key, created);
    return created;
}

void JavaObjectModel::BindString(const VmObjectRef ref,
                                 const std::u16string_view value) {
    auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::vm_instance ||
        record.java_class != impl_->string_class) {
        Fail(DexVmErrorReason::object_model_failure,
             "string constructor receiver is not an unbound string "
             "instance");
    }
    impl_->Reserve(value.size() * 2ULL);
    std::vector<JniChar> units(value.begin(), value.end());
    const auto identity = impl_->strings->Create(units);
    impl_->by_identity.erase(record.identity);
    impl_->by_identity.emplace(
        identity, static_cast<std::uint32_t>(ref.Value() - 1));
    impl_->instance_storage[record.storage].clear();
    impl_->free_instance_storage.push_back(record.storage);
    record.storage = 0;
    record.kind = VmObjectKind::string;
    record.identity = identity;
    record.reserved_bytes += value.size() * 2ULL;
}

std::u16string JavaObjectModel::StringValue(const VmObjectRef ref) const {
    const auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::string &&
        record.kind != VmObjectKind::external) {
        Fail(DexVmErrorReason::object_model_failure,
             "object is not a string");
    }
    const auto length = impl_->strings->Length(record.identity);
    const auto units = impl_->strings->Region(record.identity, 0, length);
    return std::u16string(units.begin(), units.end());
}

VmObjectRef JavaObjectModel::NewPrimitiveArray(const DexClassId array_class,
                                               const JniPrimitiveKind kind,
                                               const JniSize length) {
    if (length < 0) {
        Fail(DexVmErrorReason::object_model_failure,
             "primitive array length is negative");
    }
    const auto bytes = 32ULL + static_cast<std::uint64_t>(length) *
                                  PrimitiveWidth(kind);
    impl_->Reserve(bytes);
    const auto identity = impl_->arrays->New(kind, length);
    Impl::Record record;
    record.kind = VmObjectKind::primitive_array;
    record.identity = identity;
    record.java_class = array_class;
    record.primitive_kind = kind;
    record.reserved_bytes = bytes;
    return impl_->Register(std::move(record));
}

JniPrimitiveKind JavaObjectModel::PrimitiveArrayKind(
    const VmObjectRef ref) const {
    const auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::primitive_array) {
        Fail(DexVmErrorReason::object_model_failure,
             "object is not a primitive array");
    }
    return record.primitive_kind;
}

std::uint64_t JavaObjectModel::GetPrimitiveElement(const VmObjectRef ref,
                                                   const JniSize index) const {
    const auto& record = impl_->At(ref);
    const auto data = impl_->arrays->Region(record.identity, index, 1);
    return std::visit(
        [](const auto& elements) -> std::uint64_t {
            using Element = std::decay_t<decltype(elements[0])>;
            if constexpr (std::is_same_v<Element, JniFloat>) {
                std::uint32_t bits{};
                std::memcpy(&bits, &elements[0], sizeof(bits));
                return bits;
            } else if constexpr (std::is_same_v<Element, JniDouble>) {
                std::uint64_t bits{};
                std::memcpy(&bits, &elements[0], sizeof(bits));
                return bits;
            } else {
                return static_cast<std::uint64_t>(
                    static_cast<std::make_unsigned_t<
                        std::conditional_t<std::is_same_v<Element, JniBoolean>,
                                           std::uint8_t, Element>>>(
                        elements[0]));
            }
        },
        data);
}

void JavaObjectModel::SetPrimitiveElement(const VmObjectRef ref,
                                          const JniSize index,
                                          const std::uint64_t bits) {
    const auto& record = impl_->At(ref);
    JniPrimitiveArrayData data;
    switch (record.primitive_kind) {
        case JniPrimitiveKind::boolean:
            data = std::vector<JniBoolean>{
                static_cast<JniBoolean>(bits & 0xffU)};
            break;
        case JniPrimitiveKind::byte:
            data = std::vector<JniByte>{static_cast<JniByte>(bits & 0xffU)};
            break;
        case JniPrimitiveKind::character:
            data = std::vector<JniChar>{static_cast<JniChar>(bits & 0xffffU)};
            break;
        case JniPrimitiveKind::short_integer:
            data = std::vector<JniShort>{
                static_cast<JniShort>(bits & 0xffffU)};
            break;
        case JniPrimitiveKind::integer:
            data = std::vector<JniInt>{
                static_cast<JniInt>(static_cast<std::uint32_t>(bits))};
            break;
        case JniPrimitiveKind::long_integer:
            data = std::vector<JniLong>{static_cast<JniLong>(bits)};
            break;
        case JniPrimitiveKind::float_value: {
            const auto narrow = static_cast<std::uint32_t>(bits);
            JniFloat value{};
            std::memcpy(&value, &narrow, sizeof(value));
            data = std::vector<JniFloat>{value};
            break;
        }
        case JniPrimitiveKind::double_value: {
            JniDouble value{};
            std::memcpy(&value, &bits, sizeof(value));
            data = std::vector<JniDouble>{value};
            break;
        }
    }
    impl_->arrays->SetRegion(record.identity, index, data);
}

VmObjectRef JavaObjectModel::NewObjectArray(const DexClassId array_class,
                                            const DexClassId element_class,
                                            const JniSize length) {
    if (length < 0) {
        Fail(DexVmErrorReason::object_model_failure,
             "object array length is negative");
    }
    const auto bytes = EstimateObjectArrayBytes(length);
    impl_->Reserve(bytes);
    Impl::Record record;
    record.kind = VmObjectKind::object_array;
    if (impl_->interop.object_arrays != nullptr) {
        if (!impl_->interop.publish_class) {
            Fail(DexVmErrorReason::object_model_failure,
                 "JNI object array class publisher is not installed");
        }
        record.identity = impl_->interop.object_arrays->New(
            impl_->interop.publish_class(element_class), length);
    } else {
        record.identity = impl_->NextVmIdentity();
        if (impl_->free_fallback_object_arrays.empty()) {
            record.storage = static_cast<std::uint32_t>(
                impl_->fallback_object_arrays.size());
            impl_->fallback_object_arrays.push_back(
                Impl::ObjectArray{element_class,
                                  std::vector<VmObjectRef>(
                                      static_cast<std::size_t>(length))});
        } else {
            record.storage = impl_->free_fallback_object_arrays.back();
            impl_->free_fallback_object_arrays.pop_back();
            auto& reused = impl_->fallback_object_arrays[record.storage];
            reused.element_class = element_class;
            reused.elements.assign(static_cast<std::size_t>(length),
                                   VmObjectRef{});
        }
    }
    record.java_class = array_class;
    record.reserved_bytes = bytes;
    return impl_->Register(std::move(record));
}

DexClassId JavaObjectModel::ObjectArrayElementClass(
    const VmObjectRef ref) const {
    const auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::object_array) {
        Fail(DexVmErrorReason::object_model_failure,
             "object is not an object array");
    }
    if (impl_->interop.object_arrays == nullptr) {
        return impl_->fallback_object_arrays[record.storage].element_class;
    }
    if (!impl_->interop.resolve_class) {
        Fail(DexVmErrorReason::object_model_failure,
             "JNI object array class resolver is not installed");
    }
    return impl_->interop.resolve_class(
        impl_->interop.object_arrays->ElementClass(record.identity));
}

VmObjectRef JavaObjectModel::GetObjectElement(const VmObjectRef ref,
                                              const JniSize index) {
    const auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::object_array) {
        Fail(DexVmErrorReason::object_model_failure,
             "object is not an object array");
    }
    if (impl_->interop.object_arrays == nullptr) {
        const auto& array = impl_->fallback_object_arrays[record.storage];
        if (index < 0 || static_cast<std::size_t>(index) >= array.elements.size()) {
            Fail(DexVmErrorReason::object_model_failure,
                 "object array index is out of range");
        }
        return array.elements[static_cast<std::size_t>(index)];
    }
    const auto value = impl_->interop.object_arrays->Get(record.identity, index);
    return value.has_value() ? FromIdentity(value->object) : VmObjectRef{};
}

void JavaObjectModel::SetObjectElement(const VmObjectRef ref,
                                       const JniSize index,
                                       const VmObjectRef value) {
    const auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::object_array) {
        Fail(DexVmErrorReason::object_model_failure,
             "object is not an object array");
    }
    if (impl_->interop.object_arrays == nullptr) {
        auto& array = impl_->fallback_object_arrays[record.storage];
        if (index < 0 || static_cast<std::size_t>(index) >= array.elements.size()) {
            Fail(DexVmErrorReason::object_model_failure,
                 "object array index is out of range");
        }
        array.elements[static_cast<std::size_t>(index)] = value;
        return;
    }
    std::optional<JniObjectValue> stored;
    if (value.IsValid()) {
        stored = JniObjectValue{
            ToIdentity(value),
            impl_->interop.publish_class(ObjectClass(value))};
    }
    impl_->interop.object_arrays->Set(record.identity, index, stored);
}

JniSize JavaObjectModel::ArrayLength(const VmObjectRef ref) const {
    const auto& record = impl_->At(ref);
    if (record.kind == VmObjectKind::primitive_array ||
        record.kind == VmObjectKind::external) {
        return impl_->arrays->Length(record.identity);
    }
    if (record.kind == VmObjectKind::object_array) {
        return impl_->interop.object_arrays != nullptr
                   ? impl_->interop.object_arrays->Length(record.identity)
                   : static_cast<JniSize>(
                         impl_->fallback_object_arrays[record.storage]
                             .elements.size());
    }
    Fail(DexVmErrorReason::object_model_failure, "object is not an array");
}

VmObjectRef JavaObjectModel::CloneObject(const VmObjectRef source) {
    // Capture identity fields before New* reallocates records/storage.
    // New header (handle, identity, lock) is allocated; only payload is
    // copied, matching AOSP dvmCloneObject's memcpy past sizeof(Object).
    switch (Kind(source)) {
        case VmObjectKind::vm_instance: {
            const auto java_class = ObjectClass(source);
            const auto source_span = InstanceSlots(source);
            const std::vector<Slot> source_slots(source_span.begin(),
                                                 source_span.end());
            const auto clone = NewInstance(
                java_class,
                static_cast<std::uint16_t>(source_slots.size()));
            auto target_slots = InstanceSlots(clone);
            std::copy(source_slots.begin(), source_slots.end(),
                      target_slots.begin());
            return clone;
        }
        case VmObjectKind::primitive_array: {
            const auto java_class = ObjectClass(source);
            const auto kind = PrimitiveArrayKind(source);
            const auto source_identity = impl_->At(source).identity;
            const auto length = impl_->arrays->Length(source_identity);
            const auto clone = NewPrimitiveArray(java_class, kind, length);
            if (length > 0) {
                const auto data =
                    impl_->arrays->Region(source_identity, 0, length);
                impl_->arrays->SetRegion(impl_->At(clone).identity, 0, data);
            }
            return clone;
        }
        case VmObjectKind::object_array: {
            const auto java_class = ObjectClass(source);
            const auto element_class = ObjectArrayElementClass(source);
            const auto length = ArrayLength(source);
            const auto clone = NewObjectArray(
                java_class, element_class, length);
            for (JniSize index = 0; index < length; ++index) {
                SetObjectElement(clone, index, GetObjectElement(source, index));
            }
            return clone;
        }
        case VmObjectKind::host_backed:
        case VmObjectKind::string:
        case VmObjectKind::class_object:
        case VmObjectKind::external:
            break;
    }
    Fail(DexVmErrorReason::object_model_failure,
         "clone is not supported for this object kind");
}

void JavaObjectModel::WriteByteRegion(const VmObjectRef ref,
                                      const JniSize start,
                                      const std::span<const std::byte> bytes) {
    const auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::primitive_array ||
        record.primitive_kind != JniPrimitiveKind::byte) {
        Fail(DexVmErrorReason::object_model_failure,
             "bulk byte write requires a byte array");
    }
    std::vector<JniByte> staged(bytes.size());
    std::memcpy(staged.data(), bytes.data(), bytes.size());
    impl_->arrays->SetRegion(record.identity, start,
                             JniPrimitiveArrayData{std::move(staged)});
}

std::vector<std::byte> JavaObjectModel::ReadByteRegion(
    const VmObjectRef ref, const JniSize start, const JniSize length) const {
    const auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::primitive_array ||
        record.primitive_kind != JniPrimitiveKind::byte) {
        Fail(DexVmErrorReason::object_model_failure,
             "bulk byte read requires a byte array");
    }
    const auto data = impl_->arrays->Region(record.identity, start, length);
    const auto& typed = std::get<std::vector<JniByte>>(data);
    std::vector<std::byte> out(typed.size());
    std::memcpy(out.data(), typed.data(), typed.size());
    return out;
}

VmObjectRef JavaObjectModel::ClassObject(const DexClassId java_class) {
    const auto found = impl_->class_objects.find(java_class.Value());
    if (found != impl_->class_objects.end()) return found->second;
    impl_->Reserve(48ULL);
    Impl::Record record;
    record.kind = VmObjectKind::class_object;
    record.identity = impl_->NextVmIdentity();
    record.java_class = impl_->class_class;
    record.host_state = java_class.Value();
    record.reserved_bytes = 48ULL;
    if (!impl_->class_descriptor_resolver) {
        Fail(DexVmErrorReason::object_model_failure,
             "class descriptor resolver is not installed");
    }
    record.identity_hash = Impl::ClassIdentityHash(
        impl_->class_descriptor_resolver(java_class));
    const auto handle = impl_->Register(std::move(record));
    impl_->class_objects.emplace(java_class.Value(), handle);
    return handle;
}

DexClassId JavaObjectModel::ClassOfClassObject(const VmObjectRef ref) const {
    const auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::class_object) {
        Fail(DexVmErrorReason::object_model_failure,
             "object is not a class object");
    }
    return DexClassId(static_cast<std::uint32_t>(record.host_state));
}

void JavaObjectModel::SetEmergencyReserve(const bool enabled) noexcept {
    impl_->emergency_reserve = enabled;
}

std::uint64_t JavaObjectModel::AllocatedBytes() const noexcept {
    return impl_->allocated_bytes;
}
std::uint64_t JavaObjectModel::ObjectCount() const noexcept {
    return impl_->object_count;
}
std::uint64_t JavaObjectModel::HeapBudgetBytes() const noexcept {
    return impl_->config.heap_budget_bytes;
}

std::uint32_t JavaObjectModel::GcWatermarkPercent() const noexcept {
    return impl_->config.gc_watermark_percent;
}

bool JavaObjectModel::ShouldCollectFor(
    const std::uint64_t request_bytes) const noexcept {
    const auto percent = impl_->config.gc_watermark_percent;
    if (percent == 0U) return false;
    const auto watermark =
        (impl_->config.heap_budget_bytes * percent) / 100ULL;
    return request_bytes > watermark ||
           impl_->allocated_bytes > watermark - request_bytes;
}

std::uint64_t JavaObjectModel::EstimateInstanceBytes(
    const std::uint16_t slot_count) noexcept {
    return 32ULL + static_cast<std::uint64_t>(slot_count) * 8ULL;
}

std::uint64_t JavaObjectModel::EstimateStringBytes(
    const std::size_t code_units) noexcept {
    return 32ULL + static_cast<std::uint64_t>(code_units) * 2ULL;
}

std::uint64_t JavaObjectModel::EstimatePrimitiveArrayBytes(
    const JniPrimitiveKind kind, const JniSize length) {
    return 32ULL + static_cast<std::uint64_t>(length) * PrimitiveWidth(kind);
}

std::uint64_t JavaObjectModel::EstimateObjectArrayBytes(const JniSize length) {
    return 32ULL + static_cast<std::uint64_t>(length) * 4ULL;
}

}  // namespace ogplay::runtime::dexvm

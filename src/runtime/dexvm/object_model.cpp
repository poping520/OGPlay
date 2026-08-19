#include "ogplay/runtime/dexvm/object_model.h"

#include <algorithm>
#include <cstring>
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
        VmObjectKind kind{VmObjectKind::external};
        JniObjectIdentity identity;
        DexClassId java_class;
        std::uint32_t storage{};  // index into kind-specific storage
        std::uint64_t host_state{};
        std::uint64_t reserved_bytes{};
        JniPrimitiveKind primitive_kind{JniPrimitiveKind::integer};
    };

    struct ObjectArray final {
        DexClassId element_class;
        std::vector<VmObjectRef> elements;
    };

    JniStringStore* strings{};
    JniPrimitiveArrayStore* arrays{};
    JavaObjectModelConfig config;

    std::vector<Record> records;
    std::unordered_map<JniObjectIdentity, std::uint32_t, IdentityHash>
        by_identity;
    std::vector<std::vector<Slot>> instance_storage;
    std::vector<ObjectArray> object_arrays;
    std::unordered_map<std::u16string, VmObjectRef> intern_table;
    std::unordered_map<std::uint32_t, VmObjectRef> class_objects;

    DexClassId string_class;
    DexClassId class_class;
    std::uint64_t allocated_bytes{};
    std::uint64_t next_vm_identity{1};

    [[nodiscard]] VmObjectRef Register(Record record) {
        const auto handle =
            VmObjectRef(static_cast<std::uint32_t>(records.size() + 1));
        by_identity.emplace(record.identity,
                            static_cast<std::uint32_t>(records.size()));
        records.push_back(std::move(record));
        return handle;
    }

    [[nodiscard]] Record& At(const VmObjectRef ref) {
        if (!ref.IsValid() || ref.Value() > records.size()) {
            Fail(DexVmErrorReason::object_model_failure,
                 "object reference is invalid");
        }
        return records[ref.Value() - 1];
    }
    [[nodiscard]] const Record& At(const VmObjectRef ref) const {
        if (!ref.IsValid() || ref.Value() > records.size()) {
            Fail(DexVmErrorReason::object_model_failure,
                 "object reference is invalid");
        }
        return records[ref.Value() - 1];
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
                                 JavaObjectModelConfig config)
    : impl_(std::make_unique<Impl>()) {
    impl_->strings = &strings;
    impl_->arrays = &arrays;
    impl_->config = config;
}

JavaObjectModel::~JavaObjectModel() = default;

void JavaObjectModel::SetCoreClasses(const DexClassId string_class,
                                     const DexClassId class_class) {
    impl_->string_class = string_class;
    impl_->class_class = class_class;
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
            record.kind = VmObjectKind::external;
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

bool JavaObjectModel::IsValidRef(const VmObjectRef ref) const noexcept {
    return ref.IsValid() && ref.Value() <= impl_->records.size();
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
        trace_host_edges) const {
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
                for (const auto element :
                     impl_->object_arrays[record.storage].elements) {
                    mark(element);
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

VmObjectRef JavaObjectModel::NewInstance(const DexClassId java_class,
                                         const std::uint16_t slot_count) {
    const auto bytes = 32ULL + static_cast<std::uint64_t>(slot_count) * 8ULL;
    impl_->Reserve(bytes);
    Impl::Record record;
    record.kind = VmObjectKind::vm_instance;
    record.identity = impl_->NextVmIdentity();
    record.java_class = java_class;
    record.reserved_bytes = bytes;
    record.storage = static_cast<std::uint32_t>(
        impl_->instance_storage.size());
    impl_->instance_storage.emplace_back(slot_count, Slot{});
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
    const auto bytes = 32ULL + static_cast<std::uint64_t>(length) * 4ULL;
    impl_->Reserve(bytes);
    Impl::Record record;
    record.kind = VmObjectKind::object_array;
    record.identity = impl_->NextVmIdentity();
    record.java_class = array_class;
    record.reserved_bytes = bytes;
    record.storage =
        static_cast<std::uint32_t>(impl_->object_arrays.size());
    impl_->object_arrays.push_back(Impl::ObjectArray{
        element_class,
        std::vector<VmObjectRef>(static_cast<std::size_t>(length))});
    return impl_->Register(std::move(record));
}

DexClassId JavaObjectModel::ObjectArrayElementClass(
    const VmObjectRef ref) const {
    const auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::object_array) {
        Fail(DexVmErrorReason::object_model_failure,
             "object is not an object array");
    }
    return impl_->object_arrays[record.storage].element_class;
}

VmObjectRef JavaObjectModel::GetObjectElement(const VmObjectRef ref,
                                              const JniSize index) const {
    const auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::object_array) {
        Fail(DexVmErrorReason::object_model_failure,
             "object is not an object array");
    }
    const auto& array = impl_->object_arrays[record.storage];
    if (index < 0 ||
        static_cast<std::size_t>(index) >= array.elements.size()) {
        Fail(DexVmErrorReason::object_model_failure,
             "object array index is out of range");
    }
    return array.elements[static_cast<std::size_t>(index)];
}

void JavaObjectModel::SetObjectElement(const VmObjectRef ref,
                                       const JniSize index,
                                       const VmObjectRef value) {
    const auto& record = impl_->At(ref);
    if (record.kind != VmObjectKind::object_array) {
        Fail(DexVmErrorReason::object_model_failure,
             "object is not an object array");
    }
    auto& array = impl_->object_arrays[record.storage];
    if (index < 0 ||
        static_cast<std::size_t>(index) >= array.elements.size()) {
        Fail(DexVmErrorReason::object_model_failure,
             "object array index is out of range");
    }
    array.elements[static_cast<std::size_t>(index)] = value;
}

JniSize JavaObjectModel::ArrayLength(const VmObjectRef ref) const {
    const auto& record = impl_->At(ref);
    if (record.kind == VmObjectKind::primitive_array ||
        record.kind == VmObjectKind::external) {
        return impl_->arrays->Length(record.identity);
    }
    if (record.kind == VmObjectKind::object_array) {
        return static_cast<JniSize>(
            impl_->object_arrays[record.storage].elements.size());
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
            const auto elements =
                impl_->object_arrays[impl_->At(source).storage].elements;
            const auto clone = NewObjectArray(
                java_class, element_class,
                static_cast<JniSize>(elements.size()));
            impl_->object_arrays[impl_->At(clone).storage].elements = elements;
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
    return impl_->records.size();
}
std::uint64_t JavaObjectModel::HeapBudgetBytes() const noexcept {
    return impl_->config.heap_budget_bytes;
}

}  // namespace ogplay::runtime::dexvm

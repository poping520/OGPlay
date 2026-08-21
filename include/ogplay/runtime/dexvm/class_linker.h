#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/loader/dex.h"
#include "ogplay/loader/dex_class_data.h"
#include "ogplay/loader/dex_code.h"
#include "ogplay/runtime/dexvm/dexvm_types.h"
#include "ogplay/runtime/dexvm/fast_code.h"

namespace ogplay::runtime::dexvm {

struct IntrinsicContext;
struct VmValue;
using IntrinsicHandler = std::function<VmValue(IntrinsicContext&)>;
using HostStateDestructor = std::function<void(std::uint64_t)>;

// Class linking: registration, hierarchy resolution, field layout, vtable
// construction and lazy method precheck (docs/design/dexvm/02-architecture.md
// §5). Algorithm shape follows AOSP vm/oo/Class.cpp (vtable build) and
// vm/analysis/CodeVerify.cpp (structural precheck subset) at the pinned
// baseline; full dataflow verification is intentionally out of scope.

enum class MethodKind : std::uint8_t {
    interpreted,
    native,
    intrinsic,
    abstract,
};

struct VmClassLoaderId final {
    std::uint32_t value{};

    auto operator<=>(const VmClassLoaderId&) const = default;
};

inline constexpr VmClassLoaderId kBootstrapLoader{0};
inline constexpr VmClassLoaderId kApplicationLoader{1};

enum class DeclaredInvokeKind : std::uint8_t {
    direct,
    static_call,
    virtual_call,
    interface_call,
};

struct IntrinsicMethodDecl final {
    std::string name;
    std::string descriptor;
    bool is_static{};
    bool overridable{};
    std::uint32_t access_flags{};
    DeclaredInvokeKind invoke_kind{DeclaredInvokeKind::direct};
    IntrinsicHandler implementation;
};

struct IntrinsicFieldDecl final {
    std::string name;
    std::string descriptor;
    bool is_static{};
    std::uint32_t access_flags{};
    // Constant statics (Build.VERSION.SDK_INT) materialize at class
    // initialization; I / J / Z / Ljava/lang/String; are supported.
    bool has_constant{};
    std::int64_t integral{};
    std::string string_value;
    // Non-zero only for fields declared through the bound-field builder API.
    // The linker publishes the final VmFieldId into its per-VM binding table;
    // handlers retain only this stable declaration token.
    std::uint64_t binding_token{};
};

struct IntrinsicClassDecl final {
    std::string descriptor;
    std::optional<std::string> superclass;
    std::vector<std::string> interfaces;
    bool is_interface{};
    std::uint32_t access_flags{};
    std::vector<IntrinsicMethodDecl> methods;
    std::vector<IntrinsicFieldDecl> fields;
    IntrinsicHandler clinit_implementation;
    HostStateDestructor host_state_destructor;
};

struct LinkedField final {
    VmFieldId id;
    DexClassId owner;
    std::string name;
    std::string descriptor;
    std::uint32_t access_flags{};
    bool is_static{};
    bool is_wide{};
    bool is_ref{};
    std::uint16_t slot{};
};

struct LinkedMethod final {
    VmMethodId id;
    DexClassId owner;
    std::string name;
    std::string descriptor;
    std::uint32_t access_flags{};
    MethodKind kind{MethodKind::interpreted};
    bool is_static{};
    bool overridable{};
    DeclaredInvokeKind declared_invoke_kind{DeclaredInvokeKind::direct};
    char return_shorty{'V'};
    std::uint16_t ins_words{};
    std::int32_t vtable_index{-1};
    std::optional<loader::DexMethodCode> code;
    IntrinsicHandler implementation;
    bool prechecked{};
    std::shared_ptr<FastCode> fast_code;
    std::optional<std::uint32_t> dex_method_index;
};

struct ReflectionClassSystemMetadata final {
    bool has_inner_class{};
    std::optional<std::string> inner_name;
    std::uint32_t inner_access_flags{};
    std::optional<DexClassId> enclosing_class;
    std::optional<VmMethodId> enclosing_method;
    std::vector<DexClassId> member_classes;
};

enum class ClinitState : std::uint8_t {
    uninitialized,
    initializing,
    initialized,
    failed,
};

struct LinkedClass final {
    DexClassId id;
    std::string descriptor;
    std::optional<DexClassId> super;
    VmClassLoaderId defining_loader{kBootstrapLoader};
    std::uint8_t initiating_loader_mask{};
    std::vector<DexClassId> direct_interfaces;
    std::vector<DexClassId> interfaces;  // direct + inherited, flattened
    std::uint32_t access_flags{};
    bool is_intrinsic{};
    bool is_interface{};
    bool is_array{};
    std::string array_element_descriptor;
    std::uint16_t instance_slots{};
    std::vector<VmFieldId> own_instance_fields;
    std::vector<VmFieldId> own_static_fields;
    std::vector<VmMethodId> own_direct_methods;
    std::vector<VmMethodId> own_virtual_methods;
    std::vector<VmMethodId> vtable;
    std::optional<VmMethodId> clinit;
    ClinitState clinit_state{ClinitState::uninitialized};
    std::uint64_t clinit_thread{};
    std::vector<std::uint32_t> static_storage;  // raw slots (wide = 2)
    // Precomputed exact reference slots. Static storage itself is untagged,
    // so GC must derive this once from the declared field descriptors.
    std::vector<std::uint16_t> static_ref_slots;
    std::optional<std::uint32_t> dex_class_def_index;
    IntrinsicHandler clinit_implementation;
    HostStateDestructor host_state_destructor;
    std::vector<IntrinsicFieldDecl> intrinsic_constants;
};

struct ResolvedMethodRef final {
    VmMethodId method;
    DexClassId declared_owner;  // class named in the constant pool
};

struct ResolvedFieldRef final {
    VmFieldId field;
    DexClassId declared_owner;
};

// One platform surface the running title reached but the catalogs do not
// declare, with how often it was hit (survey mode only).
struct GapSurveyHit final {
    std::string owner_descriptor;
    std::string member;  // empty when the whole class was missing
    std::uint32_t hits{};
};

class DexClassLinker final {
public:
    DexClassLinker();
    ~DexClassLinker();
    DexClassLinker(const DexClassLinker&) = delete;
    DexClassLinker& operator=(const DexClassLinker&) = delete;

    void RegisterIntrinsics(std::span<const IntrinsicClassDecl> catalog);
    void RegisterDex(std::vector<std::uint8_t> dex_bytes);
    void Link();

    [[nodiscard]] bool IsLinked() const noexcept;
    [[nodiscard]] std::optional<DexClassId> FindClass(
        std::string_view descriptor) const;
    // Completes deferred hierarchy linking when a registered class is first
    // used. Unavailable hierarchy nodes fail here (or become recorded survey
    // stubs in gap-survey mode), never during the whole-DEX startup scan.
    void EnsureClassLinked(DexClassId id);
    // Resolves (and synthesizes on first use) an array class.
    [[nodiscard]] DexClassId ResolveDescriptor(std::string_view descriptor);

    [[nodiscard]] const LinkedClass& Class(DexClassId id) const;
    [[nodiscard]] LinkedClass& MutableClass(DexClassId id);
    [[nodiscard]] const LinkedMethod& Method(VmMethodId id) const;
    [[nodiscard]] LinkedMethod& MutableMethod(VmMethodId id);
    [[nodiscard]] const LinkedField& Field(VmFieldId id) const;
    [[nodiscard]] VmFieldId ResolveIntrinsicFieldBinding(
        std::uint64_t token) const;

    // Constant-pool resolution against the registered dex (cached).
    [[nodiscard]] DexClassId ResolveTypeIndex(std::uint32_t type_index);
    [[nodiscard]] ResolvedMethodRef ResolveMethodIndex(
        std::uint32_t method_index, bool direct_or_static);
    [[nodiscard]] ResolvedFieldRef ResolveFieldIndex(
        std::uint32_t field_index, bool is_static);

    // Runtime dispatch helpers.
    [[nodiscard]] std::optional<std::uint16_t> FindVtableIndex(
        DexClassId owner, const std::string& name,
        const std::string& descriptor) const;
    [[nodiscard]] std::optional<VmMethodId> FindDirectMethod(
        DexClassId owner, const std::string& name,
        const std::string& descriptor) const;
    [[nodiscard]] std::optional<VmFieldId> FindFieldRecursive(
        DexClassId owner, const std::string& name,
        const std::string& descriptor) const;
    [[nodiscard]] bool IsAssignable(DexClassId target, DexClassId source);
    [[nodiscard]] bool IsInitiatedBy(DexClassId java_class,
                                     VmClassLoaderId loader) const;
    void MarkInitiatedBy(DexClassId java_class, VmClassLoaderId loader);

    [[nodiscard]] const loader::DexImage& Image() const;
    [[nodiscard]] std::span<const std::uint8_t> DexBytes() const;
    [[nodiscard]] std::vector<loader::DexEncodedValue> StaticValues(
        const LinkedClass& linked) const;
    [[nodiscard]] std::size_t ClassCount() const noexcept;
    // Own methods declared by the class (not inherited).
    [[nodiscard]] std::vector<VmMethodId> MethodsOf(DexClassId owner) const;
    [[nodiscard]] std::vector<DexClassId> AllClasses() const;
    [[nodiscard]] ReflectionClassSystemMetadata ReflectionSystemMetadata(
        DexClassId java_class);
    [[nodiscard]] std::vector<DexClassId> ReflectionExceptionTypes(
        VmMethodId method);

    // Structural precheck (lazy, cached per method). Throws DexVmError with
    // class/method diagnostics on malformed code.
    void PrecheckMethod(VmMethodId id);
    // Builds once after structural precheck. The cache is derived host
    // metadata and never mutates the original dex instruction stream.
    [[nodiscard]] const FastCode& FastCodeFor(VmMethodId id);

    // Gap survey (diagnostic only, off by default — see
    // docs/playbook/NEW-TITLE.md). When enabled, an unresolved
    // *platform* class or method is synthesized as a recorded neutral stub
    // instead of failing the run, so one execution harvests the whole gap
    // list a title actually reaches. A survey run is never a compatibility
    // result: every stub is logged and reported, and the frontend must label
    // the run as a survey.
    void EnableGapSurvey();
    [[nodiscard]] bool GapSurveyEnabled() const noexcept;
    // member is "name(descriptor)ret", or empty for a whole missing class.
    void RecordGapSurveyHit(const std::string& owner_descriptor,
                            const std::string& member);
    // Synthesizes a neutral platform method on owner and records the hit.
    [[nodiscard]] VmMethodId SynthesizeSurveyMethod(
        DexClassId owner, const std::string& name,
        const std::string& descriptor, bool is_static);
    [[nodiscard]] std::vector<GapSurveyHit> GapSurveyHits() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Built-in java.* core intrinsic declarations that the interpreter itself
// depends on (Object, String, Throwable and the implicit exception
// hierarchy). Platform providers merge additional classes at assembly.
[[nodiscard]] std::vector<IntrinsicClassDecl> CoreIntrinsicCatalog();

}  // namespace ogplay::runtime::dexvm

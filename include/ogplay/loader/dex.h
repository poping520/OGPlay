#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ogplay::loader {

enum class DexMapItemType : std::uint16_t {
    header = 0x0000,
    string_id = 0x0001,
    type_id = 0x0002,
    proto_id = 0x0003,
    field_id = 0x0004,
    method_id = 0x0005,
    class_def = 0x0006,
    call_site_id = 0x0007,
    method_handle = 0x0008,
    map_list = 0x1000,
    type_list = 0x1001,
    annotation_set_ref_list = 0x1002,
    annotation_set = 0x1003,
    class_data = 0x2000,
    code = 0x2001,
    string_data = 0x2002,
    debug_info = 0x2003,
    annotation = 0x2004,
    encoded_array = 0x2005,
    annotations_directory = 0x2006,
};

struct DexHeader final {
    std::string version;
    std::uint32_t checksum{};
    std::array<std::uint8_t, 20> signature{};
    std::uint32_t file_size{};
    std::uint32_t header_size{};
    std::uint32_t endian_tag{};
    std::uint32_t link_size{};
    std::uint32_t link_offset{};
    std::uint32_t map_offset{};
    std::uint32_t string_ids_size{};
    std::uint32_t string_ids_offset{};
    std::uint32_t type_ids_size{};
    std::uint32_t type_ids_offset{};
    std::uint32_t proto_ids_size{};
    std::uint32_t proto_ids_offset{};
    std::uint32_t field_ids_size{};
    std::uint32_t field_ids_offset{};
    std::uint32_t method_ids_size{};
    std::uint32_t method_ids_offset{};
    std::uint32_t class_defs_size{};
    std::uint32_t class_defs_offset{};
    std::uint32_t data_size{};
    std::uint32_t data_offset{};
};

struct DexMapItem final {
    std::uint16_t type{};
    std::uint32_t size{};
    std::uint32_t offset{};
};

struct DexString final {
    std::uint32_t data_offset{};
    std::u16string value;
};

struct DexType final {
    std::uint32_t descriptor_string_index{};
    std::string descriptor;
};

struct DexPrototype final {
    std::uint32_t shorty_string_index{};
    std::uint32_t return_type_index{};
    std::vector<std::uint32_t> parameter_type_indices;
};

struct DexFieldId final {
    std::uint32_t class_type_index{};
    std::uint32_t type_index{};
    std::uint32_t name_string_index{};
};

struct DexMethodId final {
    std::uint32_t class_type_index{};
    std::uint32_t prototype_index{};
    std::uint32_t name_string_index{};
};

struct DexClassDef final {
    std::uint32_t class_type_index{};
    std::uint32_t access_flags{};
    std::optional<std::uint32_t> superclass_type_index;
    std::vector<std::uint32_t> interface_type_indices;
    std::optional<std::uint32_t> source_file_string_index;
    std::uint32_t annotations_offset{};
    std::uint32_t class_data_offset{};
    std::uint32_t static_values_offset{};
};

// Host-only projection of the Dalvik system annotations reflection needs.
// These are decoded facts, not guest Annotation proxy objects.
struct DexClassSystemMetadata final {
    bool has_inner_class{};
    std::optional<std::string> inner_name;
    std::uint32_t inner_access_flags{};
    std::optional<std::uint32_t> enclosing_class_type_index;
    std::optional<std::uint32_t> enclosing_method_index;
    std::vector<std::uint32_t> member_class_type_indices;
};

struct DexMethodSystemMetadata final {
    std::vector<std::uint32_t> exception_type_indices;
};

enum class DexAnnotationVisibility : std::uint8_t {
    build = 0,
    runtime = 1,
    system = 2,
};

enum class DexAnnotationValueKind : std::uint8_t {
    byte_value = 0x00,
    short_value = 0x02,
    char_value = 0x03,
    int_value = 0x04,
    long_value = 0x06,
    float_value = 0x10,
    double_value = 0x11,
    method_type_index = 0x15,
    method_handle_index = 0x16,
    string_index = 0x17,
    type_index = 0x18,
    field_index = 0x19,
    method_index = 0x1a,
    enum_field_index = 0x1b,
    array = 0x1c,
    annotation = 0x1d,
    null_reference = 0x1e,
    boolean_value = 0x1f,
};

struct DexAnnotationElement;

struct DexAnnotationValue final {
    DexAnnotationValueKind kind{DexAnnotationValueKind::null_reference};
    std::int64_t integral{};
    double floating{};
    std::uint32_t index{};
    std::uint32_t nested_type_index{};
    std::vector<DexAnnotationValue> values;
    std::vector<DexAnnotationElement> nested_elements;
};

struct DexAnnotationElement final {
    std::uint32_t name_string_index{};
    DexAnnotationValue value;
};

struct DexRuntimeAnnotation final {
    std::uint32_t type_index{};
    bool has_elements{};
    DexAnnotationVisibility visibility{DexAnnotationVisibility::runtime};
    std::vector<DexAnnotationElement> elements;
};

struct DexFieldRuntimeMetadata final {
    std::vector<DexRuntimeAnnotation> annotations;
};

struct DexClassAnnotationMetadata final {
    std::vector<DexRuntimeAnnotation> runtime_annotations;
    std::optional<DexRuntimeAnnotation> annotation_default;
};

struct DexImage final {
    DexHeader header;
    std::vector<DexMapItem> map_items;
    std::vector<DexString> strings;
    std::vector<DexType> types;
    std::vector<DexPrototype> prototypes;
    std::vector<DexFieldId> fields;
    std::vector<DexMethodId> methods;
    std::vector<DexClassDef> classes;
    // Aligned with classes/methods above.
    std::vector<DexClassSystemMetadata> class_system_metadata;
    std::vector<DexMethodSystemMetadata> method_system_metadata;
    std::vector<DexFieldRuntimeMetadata> field_runtime_metadata;
    std::vector<DexClassAnnotationMetadata> class_annotation_metadata;

    [[nodiscard]] std::optional<DexMapItem> FindMapItem(
        DexMapItemType type) const noexcept;
};

enum class DexErrorReason : std::uint8_t {
    truncated,
    invalid_magic,
    unsupported_version,
    invalid_header,
    invalid_endian,
    invalid_range,
    invalid_map,
    invalid_uleb128,
    invalid_string,
    invalid_index,
    invalid_descriptor,
    invalid_prototype,
    invalid_member,
    invalid_class_def,
};

class DexError final : public std::runtime_error {
public:
    DexError(DexErrorReason reason, std::size_t offset, std::string message);
    [[nodiscard]] DexErrorReason Reason() const noexcept;
    [[nodiscard]] std::size_t Offset() const noexcept;

private:
    DexErrorReason reason_;
    std::size_t offset_{};
};

[[nodiscard]] DexImage ParseDex(std::span<const std::uint8_t> bytes);

}  // namespace ogplay::loader

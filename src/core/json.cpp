#include "ogplay/core/json.h"

#include <yyjson.h>

#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace ogplay::core {
namespace {

bool ValidateTree(yyjson_val* value, const std::size_t depth,
                  const std::size_t maximum_depth, JsonParseError& error) {
    if (depth > maximum_depth) {
        error = {JsonParseErrorKind::nesting_limit, 0, "JSON nesting exceeds limit"};
        return false;
    }
    if (yyjson_is_arr(value)) {
        yyjson_arr_iter iterator = yyjson_arr_iter_with(value);
        while (auto* element = yyjson_arr_iter_next(&iterator)) {
            if (!ValidateTree(element, depth + 1U, maximum_depth, error)) return false;
        }
        return true;
    }
    if (!yyjson_is_obj(value)) return true;

    std::unordered_set<std::string> names;
    names.reserve(yyjson_obj_size(value));
    yyjson_obj_iter iterator = yyjson_obj_iter_with(value);
    while (auto* key = yyjson_obj_iter_next(&iterator)) {
        const std::string name(yyjson_get_str(key), yyjson_get_len(key));
        if (!names.emplace(name).second) {
            error = {JsonParseErrorKind::duplicate_key, 0,
                     "duplicate JSON object member: " + name};
            return false;
        }
        if (!ValidateTree(yyjson_obj_iter_get_val(key), depth + 1U, maximum_depth, error)) {
            return false;
        }
    }
    return true;
}

void Require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

JsonValue::JsonValue(void* value) noexcept : value_(value) {}

bool JsonValue::IsValid() const noexcept { return value_ != nullptr; }
bool JsonValue::IsNull() const noexcept {
    return value_ != nullptr && yyjson_is_null(static_cast<yyjson_val*>(value_));
}
bool JsonValue::IsBool() const noexcept {
    return value_ != nullptr && yyjson_is_bool(static_cast<yyjson_val*>(value_));
}
bool JsonValue::IsInteger() const noexcept {
    return value_ != nullptr && yyjson_is_int(static_cast<yyjson_val*>(value_));
}
bool JsonValue::IsString() const noexcept {
    return value_ != nullptr && yyjson_is_str(static_cast<yyjson_val*>(value_));
}
bool JsonValue::IsArray() const noexcept {
    return value_ != nullptr && yyjson_is_arr(static_cast<yyjson_val*>(value_));
}
bool JsonValue::IsObject() const noexcept {
    return value_ != nullptr && yyjson_is_obj(static_cast<yyjson_val*>(value_));
}

std::optional<bool> JsonValue::Bool() const noexcept {
    if (!IsBool()) return std::nullopt;
    return yyjson_get_bool(static_cast<yyjson_val*>(value_));
}

std::optional<std::int64_t> JsonValue::Integer() const noexcept {
    auto* const value = static_cast<yyjson_val*>(value_);
    if (!value) return std::nullopt;
    if (yyjson_is_sint(value)) return yyjson_get_sint(value);
    if (yyjson_is_uint(value)) {
        const auto parsed = yyjson_get_uint(value);
        if (parsed <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return static_cast<std::int64_t>(parsed);
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t> JsonValue::UnsignedInteger() const noexcept {
    auto* const value = static_cast<yyjson_val*>(value_);
    if (!value) return std::nullopt;
    if (!yyjson_is_uint(value)) return std::nullopt;
    return yyjson_get_uint(value);
}

std::optional<std::string_view> JsonValue::String() const noexcept {
    auto* const value = static_cast<yyjson_val*>(value_);
    if (!value) return std::nullopt;
    if (!yyjson_is_str(value)) return std::nullopt;
    return std::string_view(yyjson_get_str(value), yyjson_get_len(value));
}

std::optional<JsonValue> JsonValue::Member(const std::string_view name) const noexcept {
    auto* const value = static_cast<yyjson_val*>(value_);
    if (!value) return std::nullopt;
    if (!yyjson_is_obj(value)) return std::nullopt;
    const char* const name_bytes = name.data() ? name.data() : "";
    auto* const member = yyjson_obj_getn(value, name_bytes, name.size());
    if (!member) return std::nullopt;
    return JsonValue(member);
}

std::optional<JsonValue> JsonValue::Element(const std::size_t index) const noexcept {
    auto* const value = static_cast<yyjson_val*>(value_);
    if (!value) return std::nullopt;
    if (!yyjson_is_arr(value)) return std::nullopt;
    auto* const element = yyjson_arr_get(value, index);
    if (!element) return std::nullopt;
    return JsonValue(element);
}

std::size_t JsonValue::Size() const noexcept {
    auto* const value = static_cast<yyjson_val*>(value_);
    if (!value) return 0;
    if (yyjson_is_arr(value)) return yyjson_arr_size(value);
    if (yyjson_is_obj(value)) return yyjson_obj_size(value);
    return 0;
}

JsonDocument::JsonDocument(void* document) noexcept : document_(document) {}

JsonDocument::~JsonDocument() { yyjson_doc_free(static_cast<yyjson_doc*>(document_)); }

JsonDocument::JsonDocument(JsonDocument&& other) noexcept
    : document_(std::exchange(other.document_, nullptr)) {}

JsonDocument& JsonDocument::operator=(JsonDocument&& other) noexcept {
    if (this == &other) return *this;
    yyjson_doc_free(static_cast<yyjson_doc*>(document_));
    document_ = std::exchange(other.document_, nullptr);
    return *this;
}

std::optional<JsonDocument> JsonDocument::ParseStrict(
    const std::string_view text, JsonParseError& error, const std::size_t maximum_bytes,
    const std::size_t maximum_depth) {
    error = {};
    if (text.size() > maximum_bytes) {
        error = {JsonParseErrorKind::size_limit, maximum_bytes,
                 "JSON input exceeds size limit"};
        return std::nullopt;
    }
    yyjson_read_err read_error{};
    const char* const bytes = text.data() ? text.data() : "";
    auto* const document = yyjson_read_opts(
        const_cast<char*>(bytes), text.size(), YYJSON_READ_NOFLAG, nullptr, &read_error);
    if (!document) {
        error = {JsonParseErrorKind::syntax, read_error.pos,
                 read_error.msg ? read_error.msg : "invalid JSON"};
        return std::nullopt;
    }
    JsonDocument result(document);
    if (!ValidateTree(yyjson_doc_get_root(document), 1U, maximum_depth, error)) {
        return std::nullopt;
    }
    return result;
}

JsonValue JsonDocument::Root() const noexcept {
    if (!document_) return {};
    return JsonValue(yyjson_doc_get_root(static_cast<yyjson_doc*>(document_)));
}

JsonWriter::Value::Value(void* value) noexcept : value_(value) {}
bool JsonWriter::Value::IsValid() const noexcept { return value_ != nullptr; }

JsonWriter::JsonWriter() : document_(yyjson_mut_doc_new(nullptr)) {
    if (!document_) throw std::bad_alloc();
}

JsonWriter::~JsonWriter() {
    yyjson_mut_doc_free(static_cast<yyjson_mut_doc*>(document_));
}

JsonWriter::JsonWriter(JsonWriter&& other) noexcept
    : document_(std::exchange(other.document_, nullptr)) {}

JsonWriter& JsonWriter::operator=(JsonWriter&& other) noexcept {
    if (this == &other) return *this;
    yyjson_mut_doc_free(static_cast<yyjson_mut_doc*>(document_));
    document_ = std::exchange(other.document_, nullptr);
    return *this;
}

JsonWriter::Value JsonWriter::Object() {
    auto* const value = yyjson_mut_obj(static_cast<yyjson_mut_doc*>(document_));
    if (!value) throw std::bad_alloc();
    return Value(value);
}

JsonWriter::Value JsonWriter::Array() {
    auto* const value = yyjson_mut_arr(static_cast<yyjson_mut_doc*>(document_));
    if (!value) throw std::bad_alloc();
    return Value(value);
}

JsonWriter::Value JsonWriter::Null() {
    auto* const value = yyjson_mut_null(static_cast<yyjson_mut_doc*>(document_));
    if (!value) throw std::bad_alloc();
    return Value(value);
}

JsonWriter::Value JsonWriter::Bool(const bool value) {
    auto* const result = yyjson_mut_bool(static_cast<yyjson_mut_doc*>(document_), value);
    if (!result) throw std::bad_alloc();
    return Value(result);
}

JsonWriter::Value JsonWriter::Integer(const std::int64_t value) {
    auto* const result = yyjson_mut_sint(static_cast<yyjson_mut_doc*>(document_), value);
    if (!result) throw std::bad_alloc();
    return Value(result);
}

JsonWriter::Value JsonWriter::UnsignedInteger(const std::uint64_t value) {
    auto* const result = yyjson_mut_uint(static_cast<yyjson_mut_doc*>(document_), value);
    if (!result) throw std::bad_alloc();
    return Value(result);
}

JsonWriter::Value JsonWriter::Real(const double value) {
    auto* const result = yyjson_mut_real(static_cast<yyjson_mut_doc*>(document_), value);
    if (!result) throw std::bad_alloc();
    return Value(result);
}

JsonWriter::Value JsonWriter::String(const std::string_view value) {
    const char* const bytes = value.data() ? value.data() : "";
    auto* const result = yyjson_mut_strncpy(
        static_cast<yyjson_mut_doc*>(document_), bytes, value.size());
    if (!result) throw std::bad_alloc();
    return Value(result);
}

JsonWriter::Value JsonWriter::Copy(const JsonValue value) {
    Require(value.IsValid(), "cannot copy an empty JSON value");
    auto* const result = yyjson_val_mut_copy(
        static_cast<yyjson_mut_doc*>(document_), static_cast<yyjson_val*>(value.value_));
    if (!result) throw std::bad_alloc();
    return Value(result);
}

void JsonWriter::Add(const Value object, const std::string_view name, const Value value) {
    auto* const document = static_cast<yyjson_mut_doc*>(document_);
    auto* const raw_object = static_cast<yyjson_mut_val*>(object.value_);
    Require(object.IsValid(), "JSON member target is empty");
    Require(yyjson_mut_is_obj(raw_object), "JSON member target must be an object");
    Require(value.IsValid(), "cannot add an empty JSON value");
    const char* const name_bytes = name.data() ? name.data() : "";
    Require(yyjson_mut_obj_getn(raw_object, name_bytes, name.size()) == nullptr,
            "duplicate JSON object member");
    auto* const key = yyjson_mut_strncpy(document, name_bytes, name.size());
    Require(key != nullptr, "failed to allocate JSON object key");
    Require(yyjson_mut_obj_add(raw_object, key,
                               static_cast<yyjson_mut_val*>(value.value_)),
            "failed to add JSON object member");
}

void JsonWriter::AddNull(const Value object, const std::string_view name) {
    Add(object, name, Null());
}

void JsonWriter::AddBool(const Value object, const std::string_view name, const bool value) {
    Add(object, name, Bool(value));
}

void JsonWriter::AddInteger(const Value object, const std::string_view name,
                            const std::int64_t value) {
    Add(object, name, Integer(value));
}

void JsonWriter::AddUnsignedInteger(const Value object, const std::string_view name,
                                    const std::uint64_t value) {
    Add(object, name, UnsignedInteger(value));
}

void JsonWriter::AddReal(const Value object, const std::string_view name, const double value) {
    Add(object, name, Real(value));
}

void JsonWriter::AddString(const Value object, const std::string_view name,
                           const std::string_view value) {
    Add(object, name, String(value));
}

void JsonWriter::Append(const Value array, const Value value) {
    Require(array.IsValid(), "JSON array target is empty");
    Require(value.IsValid(), "cannot append an empty JSON value");
    Require(yyjson_mut_is_arr(static_cast<yyjson_mut_val*>(array.value_)),
            "JSON append target must be an array");
    Require(yyjson_mut_arr_append(static_cast<yyjson_mut_val*>(array.value_),
                                  static_cast<yyjson_mut_val*>(value.value_)),
            "failed to append JSON array value");
}

std::string JsonWriter::Serialize(const Value root) {
    Require(root.IsValid(), "cannot serialize an empty JSON value");
    auto* const document = static_cast<yyjson_mut_doc*>(document_);
    yyjson_mut_doc_set_root(document, static_cast<yyjson_mut_val*>(root.value_));
    std::size_t length = 0;
    yyjson_write_err error{};
    char* const bytes = yyjson_mut_write_opts(
        document, YYJSON_WRITE_NOFLAG, nullptr, &length, &error);
    if (!bytes) {
        throw std::runtime_error(error.msg ? error.msg : "failed to serialize JSON");
    }
    std::string result(bytes, length);
    std::free(bytes);
    return result;
}

}  // namespace ogplay::core

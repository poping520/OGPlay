#include "ogplay/app/json.h"
#include "ogplay/core/json.h"
namespace ogplay::app {
struct JsonValue::Impl { core::JsonValue value; };
struct JsonDocument::Impl { core::JsonDocument document; };
struct JsonWriter::Value::Impl { core::JsonWriter::Value value; };
struct JsonWriter::Impl { core::JsonWriter writer; };
JsonValue::JsonValue(std::shared_ptr<Impl> value) noexcept : impl_(std::move(value)) {}
bool JsonValue::IsValid() const noexcept { return impl_ && impl_->value.IsValid(); }
bool JsonValue::IsNull() const noexcept { return impl_ && impl_->value.IsNull(); }
bool JsonValue::IsBool() const noexcept { return impl_ && impl_->value.IsBool(); }
bool JsonValue::IsInteger() const noexcept { return impl_ && impl_->value.IsInteger(); }
bool JsonValue::IsString() const noexcept { return impl_ && impl_->value.IsString(); }
bool JsonValue::IsArray() const noexcept { return impl_ && impl_->value.IsArray(); }
bool JsonValue::IsObject() const noexcept { return impl_ && impl_->value.IsObject(); }
std::optional<bool> JsonValue::Bool() const noexcept { return impl_ ? impl_->value.Bool() : std::nullopt; }
std::optional<std::int64_t> JsonValue::Integer() const noexcept { return impl_ ? impl_->value.Integer() : std::nullopt; }
std::optional<std::uint64_t> JsonValue::UnsignedInteger() const noexcept { return impl_ ? impl_->value.UnsignedInteger() : std::nullopt; }
std::optional<std::string_view> JsonValue::String() const noexcept { return impl_ ? impl_->value.String() : std::nullopt; }
std::optional<double> JsonValue::Number() const noexcept { return impl_ ? impl_->value.Number() : std::nullopt; }
std::optional<JsonValue> JsonValue::Member(std::string_view name) const {
    if (!impl_) return {};
    const auto value = impl_->value.Member(name);
    if (!value) return {};
    return JsonValue(std::make_shared<Impl>(Impl{*value}));
}
std::optional<JsonValue> JsonValue::Element(std::size_t index) const {
    if (!impl_) return {};
    const auto value = impl_->value.Element(index);
    if (!value) return {};
    return JsonValue(std::make_shared<Impl>(Impl{*value}));
}
std::vector<std::string_view> JsonValue::Keys() const { return impl_ ? impl_->value.Keys() : std::vector<std::string_view>{}; }
std::size_t JsonValue::Size() const noexcept { return impl_ ? impl_->value.Size() : 0; }
JsonDocument::JsonDocument() = default;
JsonDocument::JsonDocument(std::unique_ptr<Impl> document) noexcept : impl_(std::move(document)) {}
JsonDocument::~JsonDocument() = default;
JsonDocument::JsonDocument(JsonDocument&&) noexcept = default;
JsonDocument& JsonDocument::operator=(JsonDocument&&) noexcept = default;
std::optional<JsonDocument> JsonDocument::ParseStrict(std::string_view text, JsonParseError& error, std::size_t bytes, std::size_t depth) {
    core::JsonParseError e;
    auto document = core::JsonDocument::ParseStrict(text, e, bytes, depth);
    if (!document) { error = {static_cast<JsonParseErrorKind>(e.kind), e.position, std::move(e.message)}; return {}; }
    return JsonDocument(std::make_unique<Impl>(Impl{std::move(*document)}));
}
JsonValue JsonDocument::Root() const { return impl_ ? JsonValue(std::make_shared<JsonValue::Impl>(JsonValue::Impl{impl_->document.Root()})) : JsonValue{}; }
JsonWriter::Value::Value(std::shared_ptr<Impl> value) noexcept : impl_(std::move(value)) {}
bool JsonWriter::Value::IsValid() const noexcept { return impl_ && impl_->value.IsValid(); }
JsonWriter::JsonWriter() : impl_(std::make_unique<Impl>()) {}
JsonWriter::~JsonWriter() = default;
JsonWriter::JsonWriter(JsonWriter&&) noexcept = default;
JsonWriter& JsonWriter::operator=(JsonWriter&&) noexcept = default;
JsonWriter::Value JsonWriter::Object() { return Value(std::make_shared<Value::Impl>(Value::Impl{impl_->writer.Object()})); }
JsonWriter::Value JsonWriter::Array() { return Value(std::make_shared<Value::Impl>(Value::Impl{impl_->writer.Array()})); }
JsonWriter::Value JsonWriter::Null() { return Value(std::make_shared<Value::Impl>(Value::Impl{impl_->writer.Null()})); }
JsonWriter::Value JsonWriter::Bool(bool value) { return Value(std::make_shared<Value::Impl>(Value::Impl{impl_->writer.Bool(value)})); }
JsonWriter::Value JsonWriter::Integer(std::int64_t value) { return Value(std::make_shared<Value::Impl>(Value::Impl{impl_->writer.Integer(value)})); }
JsonWriter::Value JsonWriter::UnsignedInteger(std::uint64_t value) { return Value(std::make_shared<Value::Impl>(Value::Impl{impl_->writer.UnsignedInteger(value)})); }
JsonWriter::Value JsonWriter::Real(double value) { return Value(std::make_shared<Value::Impl>(Value::Impl{impl_->writer.Real(value)})); }
JsonWriter::Value JsonWriter::String(std::string_view value) { return Value(std::make_shared<Value::Impl>(Value::Impl{impl_->writer.String(value)})); }
JsonWriter::Value JsonWriter::Copy(JsonValue value) { return Value(std::make_shared<Value::Impl>(Value::Impl{impl_->writer.Copy(value.impl_ ? value.impl_->value : core::JsonValue{})})); }
void JsonWriter::Add(Value object, std::string_view name, Value value) { impl_->writer.Add(object.impl_->value, name, value.impl_->value); }
void JsonWriter::Append(Value array, Value value) { impl_->writer.Append(array.impl_->value, value.impl_->value); }
std::string JsonWriter::Serialize(Value root) { return impl_->writer.Serialize(root.impl_->value); }
void JsonWriter::AddNull(Value object, std::string_view name) { Add(object, name, Null()); }
void JsonWriter::AddBool(Value object, std::string_view name, bool value) { Add(object, name, Bool(value)); }
void JsonWriter::AddInteger(Value object, std::string_view name, std::int64_t value) { Add(object, name, Integer(value)); }
void JsonWriter::AddUnsignedInteger(Value object, std::string_view name, std::uint64_t value) { Add(object, name, UnsignedInteger(value)); }
void JsonWriter::AddReal(Value object, std::string_view name, double value) { Add(object, name, Real(value)); }
void JsonWriter::AddString(Value object, std::string_view name, std::string_view value) { Add(object, name, String(value)); }
}

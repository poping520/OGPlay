#pragma once

#include "ogplay/app/export.h"
#include <memory>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::app {

enum class JsonParseErrorKind {
    syntax,
    size_limit,
    nesting_limit,
    duplicate_key,
};

struct JsonParseError {
    JsonParseErrorKind kind = JsonParseErrorKind::syntax;
    std::size_t position = 0;
    std::string message;
};

class OGPLAYCORE_API JsonValue final {
public:
    JsonValue() = default;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool IsNull() const noexcept;
    [[nodiscard]] bool IsBool() const noexcept;
    [[nodiscard]] bool IsInteger() const noexcept;
    [[nodiscard]] bool IsString() const noexcept;
    [[nodiscard]] bool IsArray() const noexcept;
    [[nodiscard]] bool IsObject() const noexcept;

    [[nodiscard]] std::optional<bool> Bool() const noexcept;
    [[nodiscard]] std::optional<std::int64_t> Integer() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> UnsignedInteger() const noexcept;
    [[nodiscard]] std::optional<std::string_view> String() const noexcept;
    // Integers and reals as double; other kinds yield none.
    [[nodiscard]] std::optional<double> Number() const noexcept;
    [[nodiscard]] std::optional<JsonValue> Member(std::string_view name) const;
    // Object member names in document order; empty for non-objects.
    [[nodiscard]] std::vector<std::string_view> Keys() const;
    [[nodiscard]] std::optional<JsonValue> Element(std::size_t index) const;
    [[nodiscard]] std::size_t Size() const noexcept;

private:
    friend class JsonDocument;
    friend class JsonWriter;
    struct Impl;
    explicit JsonValue(std::shared_ptr<Impl> value) noexcept;
    std::shared_ptr<Impl> impl_;
};

class OGPLAYCORE_API JsonDocument final {
public:
    static constexpr std::size_t kDefaultMaximumBytes = 1024U * 1024U;
    static constexpr std::size_t kDefaultMaximumDepth = 64U;

    JsonDocument();
    ~JsonDocument();
    JsonDocument(JsonDocument&& other) noexcept;
    JsonDocument& operator=(JsonDocument&& other) noexcept;
    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;

    [[nodiscard]] static std::optional<JsonDocument> ParseStrict(
        std::string_view text, JsonParseError& error,
        std::size_t maximum_bytes = kDefaultMaximumBytes,
        std::size_t maximum_depth = kDefaultMaximumDepth);

    [[nodiscard]] JsonValue Root() const;

private:
    struct Impl;
    explicit JsonDocument(std::unique_ptr<Impl> document) noexcept;
    std::unique_ptr<Impl> impl_;
};

class OGPLAYCORE_API JsonWriter final {
public:
    class OGPLAYCORE_API Value final {
    public:
        Value() = default;
        [[nodiscard]] bool IsValid() const noexcept;

    private:
        friend class JsonWriter;
        struct Impl;
        explicit Value(std::shared_ptr<Impl> value) noexcept;
        std::shared_ptr<Impl> impl_;
    };

    JsonWriter();
    ~JsonWriter();
    JsonWriter(JsonWriter&& other) noexcept;
    JsonWriter& operator=(JsonWriter&& other) noexcept;
    JsonWriter(const JsonWriter&) = delete;
    JsonWriter& operator=(const JsonWriter&) = delete;

    [[nodiscard]] Value Object();
    [[nodiscard]] Value Array();
    [[nodiscard]] Value Null();
    [[nodiscard]] Value Bool(bool value);
    [[nodiscard]] Value Integer(std::int64_t value);
    [[nodiscard]] Value UnsignedInteger(std::uint64_t value);
    [[nodiscard]] Value Real(double value);
    [[nodiscard]] Value String(std::string_view value);
    [[nodiscard]] Value Copy(JsonValue value);

    void Add(Value object, std::string_view name, Value value);
    void AddNull(Value object, std::string_view name);
    void AddBool(Value object, std::string_view name, bool value);
    void AddInteger(Value object, std::string_view name, std::int64_t value);
    void AddUnsignedInteger(Value object, std::string_view name, std::uint64_t value);
    void AddReal(Value object, std::string_view name, double value);
    void AddString(Value object, std::string_view name, std::string_view value);
    void Append(Value array, Value value);

    [[nodiscard]] std::string Serialize(Value root);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::app

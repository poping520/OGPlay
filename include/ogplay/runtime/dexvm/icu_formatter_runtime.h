#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace ogplay::runtime::dexvm {

// VM-owned backing store for libcore's NativeDecimalFormat logical handles.
// Guest longs are monotonically assigned tokens, never host addresses.
class IcuFormatterRuntime final {
public:
  IcuFormatterRuntime();
  ~IcuFormatterRuntime();
  IcuFormatterRuntime(const IcuFormatterRuntime&) = delete;
  IcuFormatterRuntime& operator=(const IcuFormatterRuntime&) = delete;
  struct Symbols final {
    std::u16string currency_symbol;
    char16_t decimal_separator{u'.'};
    char16_t digit{u'#'};
    std::u16string exponent_separator{u"E"};
    char16_t grouping_separator{u','};
    std::u16string infinity{u"∞"};
    std::u16string international_currency_symbol{u"USD"};
    char16_t minus_sign{u'-'};
    char16_t monetary_separator{u'.'};
    std::u16string nan{u"NaN"};
    char16_t pattern_separator{u';'};
    char16_t percent{u'%'};
    char16_t per_mill{u'‰'};
    char16_t zero_digit{u'0'};
  };

  struct FormatResult final {
    std::u16string text;
    std::int32_t integer_begin{};
    std::int32_t integer_end{};
    std::vector<std::int32_t> fields;
  };

  struct ParseResult final {
    std::int64_t value{};
    std::size_t end{};
  };

  [[nodiscard]] std::uint64_t Open(std::u16string pattern, Symbols symbols);
  [[nodiscard]] std::uint64_t Clone(std::uint64_t token);
  void Close(std::uint64_t token);
  void CloseIfPresent(std::uint64_t token) noexcept;
  void Clear() noexcept;

  void ApplyPattern(std::uint64_t token, std::u16string pattern,
                    bool localized);
  [[nodiscard]] std::u16string Pattern(std::uint64_t token,
                                       bool localized) const;
  [[nodiscard]] FormatResult FormatLong(std::uint64_t token,
                                        std::int64_t value) const;
  [[nodiscard]] std::optional<ParseResult> ParseInteger(
      std::uint64_t token, const std::u16string& text,
      std::size_t start, std::int32_t* error_index = nullptr) const;

  [[nodiscard]] std::int32_t GetAttribute(std::uint64_t token,
                                           std::int32_t attribute) const;
  void SetAttribute(std::uint64_t token, std::int32_t attribute,
                    std::int32_t value);
  [[nodiscard]] std::u16string GetTextAttribute(
      std::uint64_t token, std::int32_t attribute) const;
  void SetTextAttribute(std::uint64_t token, std::int32_t attribute,
                        std::u16string value);
  void SetSymbol(std::uint64_t token, std::int32_t symbol,
                 std::u16string value);
  void SetSymbols(std::uint64_t token, Symbols symbols);
  void SetRoundingMode(std::uint64_t token, std::int32_t mode,
                       double increment);

  [[nodiscard]] bool Contains(std::uint64_t token) const noexcept;
  [[nodiscard]] std::size_t Size() const noexcept;

private:
  struct Formatter;

  [[nodiscard]] Formatter& Require(std::uint64_t token);
  [[nodiscard]] const Formatter& Require(std::uint64_t token) const;

  std::uint64_t next_token_{1};
  std::unordered_map<std::uint64_t, std::unique_ptr<Formatter>> formatters_;
};

}  // namespace ogplay::runtime::dexvm

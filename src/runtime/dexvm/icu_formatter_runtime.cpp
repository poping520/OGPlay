#include "ogplay/runtime/dexvm/icu_formatter_runtime.h"
#include "icu_support.h"

#include <cmath>
#include <mutex>
#include <unicode/decimfmt.h>
#include <unicode/dcfmtsym.h>
#include <unicode/fpositer.h>
#include <unicode/icudataver.h>
#include <unicode/udata.h>
#include <unicode/unum.h>

namespace ogplay::runtime::dexvm {

extern const std::uint64_t kPinnedIcuData[];

void InitializePinnedIcu() {
  static std::once_flag initialized;
  std::call_once(initialized, [] {
    UErrorCode status = U_ZERO_ERROR;
    udata_setFileAccess(UDATA_ONLY_PACKAGES, &status);
    CheckIcu(status);
    udata_setCommonData(kPinnedIcuData, &status);
    CheckIcu(status);
    UVersionInfo version{};
    u_getDataVersion(version, &status);
    CheckIcu(status);
    if (version[0] != 51 || version[1] != 1)
      throw std::runtime_error("pinned ICU51 data version mismatch");
  });
}

struct IcuFormatterRuntime::Formatter final {
  std::unique_ptr<icu::DecimalFormat> value;
  UNumberFormat* CFormat() const {
    // ICU's C API accepts its C++ DecimalFormat, as in pinned libcore.
    return reinterpret_cast<UNumberFormat*>(value.get());
  }
};

namespace {
std::unique_ptr<icu::DecimalFormatSymbols> MakeSymbols(
    const IcuFormatterRuntime::Symbols& symbols) {
  UErrorCode status = U_ZERO_ERROR;
  auto result = std::make_unique<icu::DecimalFormatSymbols>(icu::Locale::getRoot(), status);
  CheckIcu(status);
  using S = icu::DecimalFormatSymbols;
  const auto set = [&](S::ENumberFormatSymbol symbol, std::u16string_view text) {
    result->setSymbol(symbol, IcuString(text));
  };
  const auto character = [&](S::ENumberFormatSymbol symbol, char16_t c) {
    set(symbol, std::u16string_view(&c, 1));
  };
  set(S::kCurrencySymbol, symbols.currency_symbol);
  set(S::kIntlCurrencySymbol, symbols.international_currency_symbol);
  set(S::kExponentialSymbol, symbols.exponent_separator);
  set(S::kInfinitySymbol, symbols.infinity);
  set(S::kNaNSymbol, symbols.nan);
  character(S::kDecimalSeparatorSymbol, symbols.decimal_separator);
  character(S::kDigitSymbol, symbols.digit);
  character(S::kGroupingSeparatorSymbol, symbols.grouping_separator);
  character(S::kMonetaryGroupingSeparatorSymbol, symbols.grouping_separator);
  character(S::kMinusSignSymbol, symbols.minus_sign);
  character(S::kMonetarySeparatorSymbol, symbols.monetary_separator);
  character(S::kPatternSeparatorSymbol, symbols.pattern_separator);
  character(S::kPercentSymbol, symbols.percent);
  character(S::kPerMillSymbol, symbols.per_mill);
  // The ICU enum has distinct (noncontiguous) zero and one..nine symbols.
  character(S::kZeroDigitSymbol, symbols.zero_digit);
  for (int i = 1; i <= 9; ++i)
    character(static_cast<S::ENumberFormatSymbol>(S::kOneDigitSymbol + i - 1),
              static_cast<char16_t>(symbols.zero_digit + i));
  return result;
}
void CheckAttribute(const std::int32_t attribute) {
  if (attribute < UNUM_PARSE_INT_ONLY || attribute > UNUM_LENIENT_PARSE)
    throw std::invalid_argument("unsupported decimal format attribute");
}
void CheckTextAttribute(const std::int32_t attribute) {
  if (attribute < UNUM_POSITIVE_PREFIX || attribute > UNUM_CURRENCY_CODE)
    throw std::invalid_argument("unsupported decimal text attribute");
}
}  // namespace

IcuFormatterRuntime::IcuFormatterRuntime() = default;
IcuFormatterRuntime::~IcuFormatterRuntime() = default;

std::uint64_t IcuFormatterRuntime::Open(std::u16string pattern, Symbols symbols) {
  InitializePinnedIcu();
  UErrorCode status = U_ZERO_ERROR;
  UParseError error{};
  auto formatter = std::make_unique<Formatter>();
  auto dfs = MakeSymbols(symbols);
  formatter->value = std::make_unique<icu::DecimalFormat>(IcuString(pattern), dfs.release(), error, status);
  CheckIcu(status);
  if (next_token_ == 0) throw std::overflow_error("formatter token space exhausted");
  auto token = next_token_++;
  formatters_.emplace(token, std::move(formatter));
  return token;
}
std::uint64_t IcuFormatterRuntime::Clone(std::uint64_t token) {
  auto clone = std::make_unique<Formatter>();
  clone->value.reset(static_cast<icu::DecimalFormat*>(Require(token).value->clone()));
  if (!clone->value) throw std::bad_alloc{};
  if (next_token_ == 0) throw std::overflow_error("formatter token space exhausted");
  auto result = next_token_++;
  formatters_.emplace(result, std::move(clone));
  return result;
}
void IcuFormatterRuntime::Close(std::uint64_t token) {
  static_cast<void>(Require(token));
  formatters_.erase(token);
}
void IcuFormatterRuntime::CloseIfPresent(std::uint64_t token) noexcept { formatters_.erase(token); }
void IcuFormatterRuntime::Clear() noexcept { formatters_.clear(); }
void IcuFormatterRuntime::ApplyPattern(std::uint64_t token, std::u16string pattern, bool localized) {
  auto& value = *Require(token).value;
  UErrorCode status = U_ZERO_ERROR;
  UParseError error{};
  if (localized) value.applyLocalizedPattern(IcuString(pattern), error, status);
  else value.applyPattern(IcuString(pattern), error, status);
  CheckIcu(status);
}
std::u16string IcuFormatterRuntime::Pattern(std::uint64_t token, bool localized) const {
  icu::UnicodeString result;
  if (localized) Require(token).value->toLocalizedPattern(result);
  else Require(token).value->toPattern(result);
  return FromIcu(result);
}
IcuFormatterRuntime::FormatResult IcuFormatterRuntime::FormatLong(std::uint64_t token, std::int64_t value) const {
  FormatResult result;
  icu::UnicodeString text;
  icu::FieldPositionIterator fields;
  UErrorCode status = U_ZERO_ERROR;
  Require(token).value->format(value, text, &fields, status);
  CheckIcu(status);
  result.text = FromIcu(text);
  icu::FieldPosition field;
  while (fields.next(field)) {
    result.fields.insert(result.fields.end(), {field.getField(), field.getBeginIndex(), field.getEndIndex()});
    if (field.getField() == UNUM_INTEGER_FIELD) {
      result.integer_begin = field.getBeginIndex();
      result.integer_end = field.getEndIndex();
    }
  }
  return result;
}
std::optional<IcuFormatterRuntime::ParseResult> IcuFormatterRuntime::ParseInteger(
    std::uint64_t token, const std::u16string& text, std::size_t start,
    std::int32_t* error_index) const {
  auto& value = *Require(token).value;
  if (start > text.size() || start > static_cast<std::size_t>(INT32_MAX)) return std::nullopt;
  icu::ParsePosition position(static_cast<std::int32_t>(start));
  icu::Formattable result;
  value.parse(IcuString(text), result, position);
  if (position.getErrorIndex() != -1) {
    if (error_index) *error_index = position.getErrorIndex();
    return std::nullopt;
  }
  if (result.getType() != icu::Formattable::kLong && result.getType() != icu::Formattable::kInt64)
    throw std::domain_error("non-integral/out-of-range decimal parsing is not provided");
  UErrorCode status = U_ZERO_ERROR;
  const auto integer = result.getInt64(status);
  CheckIcu(status);
  return ParseResult{integer, static_cast<std::size_t>(position.getIndex())};
}
std::int32_t IcuFormatterRuntime::GetAttribute(std::uint64_t token, std::int32_t attribute) const {
  auto& f = Require(token); CheckAttribute(attribute);
  return unum_getAttribute(f.CFormat(), static_cast<UNumberFormatAttribute>(attribute));
}
void IcuFormatterRuntime::SetAttribute(std::uint64_t token, std::int32_t attribute, std::int32_t value) {
  auto& f = Require(token); CheckAttribute(attribute);
  unum_setAttribute(f.CFormat(), static_cast<UNumberFormatAttribute>(attribute), value);
}
std::u16string IcuFormatterRuntime::GetTextAttribute(std::uint64_t token, std::int32_t attribute) const {
  auto& f = Require(token); CheckTextAttribute(attribute);
  UErrorCode status = U_ZERO_ERROR;
  const auto attr = static_cast<UNumberFormatTextAttribute>(attribute);
  auto length = unum_getTextAttribute(f.CFormat(), attr, nullptr, 0, &status);
  if (status == U_BUFFER_OVERFLOW_ERROR) status = U_ZERO_ERROR;
  CheckIcu(status);
  std::u16string result(static_cast<std::size_t>(length), u'\0');
  unum_getTextAttribute(f.CFormat(), attr, reinterpret_cast<UChar*>(result.data()), length, &status);
  CheckIcu(status);
  return result;
}
void IcuFormatterRuntime::SetTextAttribute(std::uint64_t token, std::int32_t attribute, std::u16string value) {
  auto& f = Require(token); CheckTextAttribute(attribute);
  UErrorCode status = U_ZERO_ERROR;
  const auto text = IcuString(value);
  unum_setTextAttribute(f.CFormat(), static_cast<UNumberFormatTextAttribute>(attribute), text.getBuffer(), text.length(), &status);
  CheckIcu(status);
}
void IcuFormatterRuntime::SetSymbol(std::uint64_t token, std::int32_t symbol, std::u16string value) {
  auto& f = Require(token);
  if (symbol < 0 || symbol >= UNUM_FORMAT_SYMBOL_COUNT) throw std::invalid_argument("invalid decimal symbol");
  UErrorCode status = U_ZERO_ERROR;
  const auto text = IcuString(value);
  unum_setSymbol(f.CFormat(), static_cast<UNumberFormatSymbol>(symbol), text.getBuffer(), text.length(), &status);
  CheckIcu(status);
}
void IcuFormatterRuntime::SetSymbols(std::uint64_t token, Symbols symbols) {
  Require(token).value->adoptDecimalFormatSymbols(MakeSymbols(symbols).release());
}
void IcuFormatterRuntime::SetRoundingMode(std::uint64_t token, std::int32_t mode, double increment) {
  auto& f = Require(token);
  if (mode < 0 || mode > UNUM_ROUND_UNNECESSARY || !std::isfinite(increment) || increment < 0)
    throw std::invalid_argument("invalid decimal rounding mode/increment");
  f.value->setRoundingMode(static_cast<icu::DecimalFormat::ERoundingMode>(mode));
  f.value->setRoundingIncrement(increment);
}
bool IcuFormatterRuntime::Contains(std::uint64_t token) const noexcept { return formatters_.contains(token); }
std::size_t IcuFormatterRuntime::Size() const noexcept { return formatters_.size(); }
IcuFormatterRuntime::Formatter& IcuFormatterRuntime::Require(std::uint64_t token) {
  const auto found = formatters_.find(token);
  if (!token || found == formatters_.end()) throw std::invalid_argument("invalid NativeDecimalFormat token");
  return *found->second;
}
const IcuFormatterRuntime::Formatter& IcuFormatterRuntime::Require(std::uint64_t token) const {
  const auto found = formatters_.find(token);
  if (!token || found == formatters_.end()) throw std::invalid_argument("invalid NativeDecimalFormat token");
  return *found->second;
}
}  // namespace ogplay::runtime::dexvm

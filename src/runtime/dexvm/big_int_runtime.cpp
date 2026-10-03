#include "ogplay/runtime/dexvm/big_int_runtime.h"

#include <limits>

#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm {
std::uint64_t BigIntRuntime::New() {
    if (next_ == std::numeric_limits<std::uint64_t>::max())
        throw VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "NativeBN token space exhausted"};
    const auto token = next_++;
    values_.emplace(token, Number{});
    return token;
}
BigIntRuntime::Number& BigIntRuntime::Require(std::uint64_t token) {
    const auto found = values_.find(token);
    if (found == values_.end())
        throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid NativeBN token"};
    return found->second;
}
void BigIntRuntime::Free(std::uint64_t token) {
    static_cast<void>(Require(token));
    values_.erase(token);
}
void BigIntRuntime::Sweep(std::uint64_t token) noexcept { values_.erase(token); }

void BigIntRuntime::Number::Normalize() {
    while (!words.empty() && words.back() == 0) words.pop_back();
    if (words.empty()) negative = false;
}
void BigIntRuntime::Number::SetLong(std::uint64_t magnitude, bool sign) {
    words = {static_cast<std::uint32_t>(magnitude), static_cast<std::uint32_t>(magnitude >> 32U)};
    negative = sign;
    Normalize();
}
int BigIntRuntime::Number::SetDecimal(const std::string_view text) {
    if (text.size() > 1048576)
        throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                          "integer input exceeds value codec limit"};
    const std::size_t begin = !text.empty() && text.front() == '-' ? 1 : 0;
    auto end = begin;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') ++end;
    if (end == begin) return 0;
    Number value;
    std::size_t work = 0;
    for (auto i = begin; i < end;) {
        std::uint32_t multiplier = 1;
        std::uint64_t carry = 0;
        // Nine decimal digits fit in one limb. Parse into a temporary so a
        // budget failure leaves the existing NativeBN value intact.
        for (unsigned count = 0; count < 9 && i < end; ++count, ++i) {
            multiplier *= 10;
            carry = carry * 10 + static_cast<unsigned>(text[i] - '0');
        }
        if (value.words.size() > 16777216 - work)
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "decimal conversion exceeds value codec work limit"};
        work += value.words.size();
        for (auto& word : value.words) {
            carry += static_cast<std::uint64_t>(word) * multiplier;
            word = static_cast<std::uint32_t>(carry);
            carry >>= 32U;
        }
        if (carry) value.words.push_back(static_cast<std::uint32_t>(carry));
    }
    value.negative = begin != 0;
    value.Normalize();
    *this = std::move(value);
    return static_cast<int>(end);
}
void BigIntRuntime::Number::SetBytes(std::span<const std::byte> bytes, bool sign,
                                     bool twos_complement) {
    words.assign((bytes.size() + 3) / 4, 0);
    negative =
        twos_complement ? (!bytes.empty() && (std::to_integer<unsigned>(bytes[0]) & 128U)) : sign;
    std::uint32_t carry = negative && twos_complement ? 1 : 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto value = std::to_integer<unsigned>(bytes[bytes.size() - 1 - i]);
        if (negative && twos_complement) {
            value = (value ^ 255U) + carry;
            carry = value >> 8U;
            value &= 255U;
        }
        words[i / 4] |= value << ((i % 4) * 8U);
    }
    Normalize();
}
std::uint64_t BigIntRuntime::Number::LowLong() const {
    return (words.empty() ? 0 : words[0]) |
           (words.size() < 2 ? 0 : static_cast<std::uint64_t>(words[1]) << 32U);
}
int BigIntRuntime::Number::Compare(const Number& other) const {
    if (negative != other.negative) return negative ? -1 : 1;
    int order = words.size() < other.words.size() ? -1 : words.size() > other.words.size() ? 1 : 0;
    if (!order)
        for (auto i = words.size(); i > 0; --i) {
            if (words[i - 1] != other.words[i - 1]) {
                order = words[i - 1] < other.words[i - 1] ? -1 : 1;
                break;
            }
        }
    return negative ? -order : order;
}
int BigIntRuntime::Number::BitLength() const {
    if (words.empty()) return 0;
    auto high = words.back();
    if (negative) {
        bool power = (high & (high - 1U)) == 0;
        for (std::size_t i = 0; i + 1 < words.size(); ++i) power = power && words[i] == 0;
        if (power) --high;
    }
    int count = static_cast<int>((words.size() - 1) * 32);
    while (high) {
        ++count;
        high >>= 1U;
    }
    return count;
}
std::vector<std::byte> BigIntRuntime::Number::Bytes() const {
    std::vector<std::byte> result;
    for (auto i = words.size(); i > 0; --i)
        for (int shift = 24; shift >= 0; shift -= 8) {
            auto b = static_cast<std::byte>((words[i - 1] >> shift) & 255U);
            if (!result.empty() || b != std::byte{}) result.push_back(b);
        }
    return result;
}
std::string BigIntRuntime::Number::String(unsigned radix) const {
    auto value = words;
    std::string result;
    do {
        std::uint64_t remainder = 0;
        for (auto i = value.size(); i > 0; --i) {
            const auto current = (remainder << 32U) | value[i - 1];
            value[i - 1] = static_cast<std::uint32_t>(current / radix);
            remainder = current % radix;
        }
        result.push_back("0123456789ABCDEF"[remainder]);
        while (!value.empty() && value.back() == 0) value.pop_back();
    } while (!value.empty());
    if (negative) result.push_back('-');
    return std::string(result.rbegin(), result.rend());
}
}  // namespace ogplay::runtime::dexvm

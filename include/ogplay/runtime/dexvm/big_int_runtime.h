#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace ogplay::runtime::dexvm {
// Checked logical NativeBN handles and integer value codecs; no crypto arithmetic.
class BigIntRuntime final {
   public:
    struct Number final {
        std::vector<std::uint32_t> words;  // Little endian magnitude, normalized zero is empty.
        bool negative{};
        void Normalize();
        void SetLong(std::uint64_t magnitude, bool sign);
        void SetBytes(std::span<const std::byte> bytes, bool sign, bool twos_complement);
        std::uint64_t LowLong() const;
        int Compare(const Number& other) const;
        int BitLength() const;
        std::vector<std::byte> Bytes() const;
        std::string String(unsigned radix) const;
    };
    std::uint64_t New();
    Number& Require(std::uint64_t token);
    void Free(std::uint64_t token);
    void Sweep(std::uint64_t token) noexcept;
    std::size_t Size() const noexcept { return values_.size(); }

   private:
    std::uint64_t next_{1};
    std::unordered_map<std::uint64_t, Number> values_;
};
}  // namespace ogplay::runtime::dexvm

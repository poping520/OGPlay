#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace ogplay::runtime::dexvm {
// Checked logical NativeBN handles. This boundary currently supports magnitudes
// up to 64 bits, sufficient for ASN.1 identifier keys; no host pointers escape.
class BigIntRuntime final {
public:
    struct Number final { std::uint64_t magnitude{}; bool negative{}; };
    std::uint64_t New();
    Number& Require(std::uint64_t token);
    void Free(std::uint64_t token);
    void Sweep(std::uint64_t token) noexcept;
    std::size_t Size() const noexcept { return values_.size(); }
private:
    std::uint64_t next_{1};
    std::unordered_map<std::uint64_t, Number> values_;
};
}

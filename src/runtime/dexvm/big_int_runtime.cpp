#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include <limits>

namespace ogplay::runtime::dexvm {
std::uint64_t BigIntRuntime::New() {
    if (next_ == std::numeric_limits<std::uint64_t>::max())
        throw VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "NativeBN token space exhausted"};
    const auto token = next_++; values_.emplace(token, Number{}); return token;
}
BigIntRuntime::Number& BigIntRuntime::Require(std::uint64_t token) {
    const auto found = values_.find(token);
    if (found == values_.end()) throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid NativeBN token"};
    return found->second;
}
void BigIntRuntime::Free(std::uint64_t token) {
    static_cast<void>(Require(token)); values_.erase(token);
}
void BigIntRuntime::Sweep(std::uint64_t token) noexcept { values_.erase(token); }
}

// Arithmetic, logic, shift, conversion and compare families.
// Deterministic edge semantics follow AOSP vm/mterp/c per opcode at the
// pinned baseline: cmpl/cmpg NaN bias, div/rem MIN/-1 and divide-by-zero,
// shift masking (5/6 bits), narrowing truncation and float->int saturation
// (05 §2 conformance fixtures assert each).

#include <cmath>
#include <cstring>
#include <limits>

#include "interpreter_internal.h"

namespace ogplay::runtime::dexvm {
namespace {

[[nodiscard]] float AsFloat(const std::uint32_t bits) {
    float value{};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
[[nodiscard]] std::uint32_t FloatBits(const float value) {
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
[[nodiscard]] double AsDouble(const std::uint64_t bits) {
    double value{};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
[[nodiscard]] std::uint64_t DoubleBits(const double value) {
    std::uint64_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

template <typename Integral, typename Float>
[[nodiscard]] Integral SaturateToInt(const Float value) {
    // Dalvik float->int conversion: NaN -> 0, out of range -> clamp
    // (OP_FLOAT_TO_INT.cpp / OP_DOUBLE_TO_LONG.cpp).
    if (std::isnan(value)) return 0;
    constexpr auto minimum = std::numeric_limits<Integral>::min();
    constexpr auto maximum = std::numeric_limits<Integral>::max();
    if (value >= static_cast<Float>(maximum)) return maximum;
    if (value <= static_cast<Float>(minimum)) return minimum;
    return static_cast<Integral>(value);
}

}  // namespace

bool Interpreter::Impl::ExecuteArithmetic(Frame& frame,
                                          const std::uint8_t opcode,
                                          const std::uint16_t unit) {
    const auto& units = frame.method->code->instructions;
    const auto vAA = static_cast<std::uint32_t>((unit >> 8U) & 0xffU);
    const auto vA = static_cast<std::uint32_t>((unit >> 8U) & 0xfU);
    const auto vB4 = static_cast<std::uint32_t>((unit >> 12U) & 0xfU);

    // ---- cmp family (0x2d..0x31) -----------------------------------------
    if (opcode >= 0x2d && opcode <= 0x31) {
        const auto second = units[frame.pc + 1];
        const auto src1 = static_cast<std::uint32_t>(second & 0xffU);
        const auto src2 = static_cast<std::uint32_t>((second >> 8U) & 0xffU);
        std::int32_t result{};
        switch (opcode) {
            case 0x2d: {  // cmpl-float: NaN -> -1
                const auto lhs = AsFloat(GetCat1(frame, src1));
                const auto rhs = AsFloat(GetCat1(frame, src2));
                result = lhs > rhs ? 1 : (lhs == rhs ? 0 : -1);
                break;
            }
            case 0x2e: {  // cmpg-float: NaN -> 1
                const auto lhs = AsFloat(GetCat1(frame, src1));
                const auto rhs = AsFloat(GetCat1(frame, src2));
                result = lhs < rhs ? -1 : (lhs == rhs ? 0 : 1);
                break;
            }
            case 0x2f: {  // cmpl-double
                const auto lhs = AsDouble(GetWide(frame, src1));
                const auto rhs = AsDouble(GetWide(frame, src2));
                result = lhs > rhs ? 1 : (lhs == rhs ? 0 : -1);
                break;
            }
            case 0x30: {  // cmpg-double
                const auto lhs = AsDouble(GetWide(frame, src1));
                const auto rhs = AsDouble(GetWide(frame, src2));
                result = lhs < rhs ? -1 : (lhs == rhs ? 0 : 1);
                break;
            }
            case 0x31: {  // cmp-long
                const auto lhs = static_cast<std::int64_t>(
                    GetWide(frame, src1));
                const auto rhs = static_cast<std::int64_t>(
                    GetWide(frame, src2));
                result = lhs > rhs ? 1 : (lhs == rhs ? 0 : -1);
                break;
            }
            default:
                break;
        }
        SetCat1(frame, vAA, static_cast<std::uint32_t>(result));
        return true;
    }

    // ---- unary ops (0x7b..0x8f) --------------------------------------------
    if (opcode >= 0x7b && opcode <= 0x8f) {
        switch (opcode) {
            case 0x7b:  // neg-int
                SetCat1(frame, vA, static_cast<std::uint32_t>(
                    -static_cast<std::int32_t>(GetCat1(frame, vB4))));
                break;
            case 0x7c:  // not-int
                SetCat1(frame, vA, ~GetCat1(frame, vB4));
                break;
            case 0x7d:  // neg-long
                SetWide(frame, vA, static_cast<std::uint64_t>(
                    -static_cast<std::int64_t>(GetWide(frame, vB4))));
                break;
            case 0x7e:  // not-long
                SetWide(frame, vA, ~GetWide(frame, vB4));
                break;
            case 0x7f:  // neg-float
                SetCat1(frame, vA, FloatBits(-AsFloat(GetCat1(frame, vB4))));
                break;
            case 0x80:  // neg-double
                SetWide(frame, vA, DoubleBits(-AsDouble(GetWide(frame, vB4))));
                break;
            case 0x81:  // int-to-long
                SetWide(frame, vA, static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(
                        static_cast<std::int32_t>(GetCat1(frame, vB4)))));
                break;
            case 0x82:  // int-to-float
                SetCat1(frame, vA, FloatBits(static_cast<float>(
                    static_cast<std::int32_t>(GetCat1(frame, vB4)))));
                break;
            case 0x83:  // int-to-double
                SetWide(frame, vA, DoubleBits(static_cast<double>(
                    static_cast<std::int32_t>(GetCat1(frame, vB4)))));
                break;
            case 0x84:  // long-to-int
                SetCat1(frame, vA, static_cast<std::uint32_t>(
                    GetWide(frame, vB4)));
                break;
            case 0x85:  // long-to-float
                SetCat1(frame, vA, FloatBits(static_cast<float>(
                    static_cast<std::int64_t>(GetWide(frame, vB4)))));
                break;
            case 0x86:  // long-to-double
                SetWide(frame, vA, DoubleBits(static_cast<double>(
                    static_cast<std::int64_t>(GetWide(frame, vB4)))));
                break;
            case 0x87:  // float-to-int
                SetCat1(frame, vA, static_cast<std::uint32_t>(
                    SaturateToInt<std::int32_t>(
                        AsFloat(GetCat1(frame, vB4)))));
                break;
            case 0x88:  // float-to-long
                SetWide(frame, vA, static_cast<std::uint64_t>(
                    SaturateToInt<std::int64_t>(
                        AsFloat(GetCat1(frame, vB4)))));
                break;
            case 0x89:  // float-to-double
                SetWide(frame, vA, DoubleBits(static_cast<double>(
                    AsFloat(GetCat1(frame, vB4)))));
                break;
            case 0x8a:  // double-to-int
                SetCat1(frame, vA, static_cast<std::uint32_t>(
                    SaturateToInt<std::int32_t>(
                        AsDouble(GetWide(frame, vB4)))));
                break;
            case 0x8b:  // double-to-long
                SetWide(frame, vA, static_cast<std::uint64_t>(
                    SaturateToInt<std::int64_t>(
                        AsDouble(GetWide(frame, vB4)))));
                break;
            case 0x8c:  // double-to-float
                SetCat1(frame, vA, FloatBits(static_cast<float>(
                    AsDouble(GetWide(frame, vB4)))));
                break;
            case 0x8d:  // int-to-byte
                SetCat1(frame, vA, static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(static_cast<std::int8_t>(
                        GetCat1(frame, vB4) & 0xffU))));
                break;
            case 0x8e:  // int-to-char
                SetCat1(frame, vA, GetCat1(frame, vB4) & 0xffffU);
                break;
            case 0x8f:  // int-to-short
                SetCat1(frame, vA, static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(static_cast<std::int16_t>(
                        GetCat1(frame, vB4) & 0xffffU))));
                break;
            default:
                return false;
        }
        return true;
    }

    // ---- int/long/float/double binops -------------------------------------
    const auto int_binop = [&](const std::uint32_t dest,
                               const std::int32_t lhs, const std::int32_t rhs,
                               const std::uint8_t operation) -> bool {
        std::int32_t result{};
        switch (operation) {
            case 0: result = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) +
                static_cast<std::uint32_t>(rhs)); break;
            case 1: result = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) -
                static_cast<std::uint32_t>(rhs)); break;
            case 2: result = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) *
                static_cast<std::uint32_t>(rhs)); break;
            case 3:
                if (rhs == 0) {
                    ThrowJava("Ljava/lang/ArithmeticException;",
                              "divide by zero");
                    return true;
                }
                if (lhs == std::numeric_limits<std::int32_t>::min() &&
                    rhs == -1) {
                    result = lhs;  // OP_DIV_INT.cpp overflow rule
                } else {
                    result = lhs / rhs;
                }
                break;
            case 4:
                if (rhs == 0) {
                    ThrowJava("Ljava/lang/ArithmeticException;",
                              "modulo by zero");
                    return true;
                }
                if (lhs == std::numeric_limits<std::int32_t>::min() &&
                    rhs == -1) {
                    result = 0;
                } else {
                    result = lhs % rhs;
                }
                break;
            case 5: result = lhs & rhs; break;
            case 6: result = lhs | rhs; break;
            case 7: result = lhs ^ rhs; break;
            case 8: result = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs)
                << (static_cast<std::uint32_t>(rhs) & 0x1fU)); break;
            case 9: result = lhs >> (static_cast<std::uint32_t>(rhs) & 0x1fU);
                break;
            case 10: result = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) >>
                (static_cast<std::uint32_t>(rhs) & 0x1fU)); break;
            default: return false;
        }
        SetCat1(frame, dest, static_cast<std::uint32_t>(result));
        return true;
    };

    const auto long_binop = [&](const std::uint32_t dest,
                                const std::int64_t lhs,
                                const std::int64_t rhs,
                                const std::uint8_t operation) -> bool {
        std::int64_t result{};
        switch (operation) {
            case 0: result = static_cast<std::int64_t>(
                static_cast<std::uint64_t>(lhs) +
                static_cast<std::uint64_t>(rhs)); break;
            case 1: result = static_cast<std::int64_t>(
                static_cast<std::uint64_t>(lhs) -
                static_cast<std::uint64_t>(rhs)); break;
            case 2: result = static_cast<std::int64_t>(
                static_cast<std::uint64_t>(lhs) *
                static_cast<std::uint64_t>(rhs)); break;
            case 3:
                if (rhs == 0) {
                    ThrowJava("Ljava/lang/ArithmeticException;",
                              "divide by zero");
                    return true;
                }
                if (lhs == std::numeric_limits<std::int64_t>::min() &&
                    rhs == -1) {
                    result = lhs;
                } else {
                    result = lhs / rhs;
                }
                break;
            case 4:
                if (rhs == 0) {
                    ThrowJava("Ljava/lang/ArithmeticException;",
                              "modulo by zero");
                    return true;
                }
                if (lhs == std::numeric_limits<std::int64_t>::min() &&
                    rhs == -1) {
                    result = 0;
                } else {
                    result = lhs % rhs;
                }
                break;
            case 5: result = lhs & rhs; break;
            case 6: result = lhs | rhs; break;
            case 7: result = lhs ^ rhs; break;
            case 8: result = static_cast<std::int64_t>(
                static_cast<std::uint64_t>(lhs)
                << (static_cast<std::uint64_t>(rhs) & 0x3fU)); break;
            case 9: result = lhs >> (static_cast<std::uint64_t>(rhs) & 0x3fU);
                break;
            case 10: result = static_cast<std::int64_t>(
                static_cast<std::uint64_t>(lhs) >>
                (static_cast<std::uint64_t>(rhs) & 0x3fU)); break;
            default: return false;
        }
        SetWide(frame, dest, static_cast<std::uint64_t>(result));
        return true;
    };

    const auto float_binop = [&](const std::uint32_t dest, const float lhs,
                                 const float rhs,
                                 const std::uint8_t operation) -> bool {
        float result{};
        switch (operation) {
            case 0: result = lhs + rhs; break;
            case 1: result = lhs - rhs; break;
            case 2: result = lhs * rhs; break;
            case 3: result = lhs / rhs; break;
            case 4: result = std::fmod(lhs, rhs); break;
            default: return false;
        }
        SetCat1(frame, dest, FloatBits(result));
        return true;
    };

    const auto double_binop = [&](const std::uint32_t dest, const double lhs,
                                  const double rhs,
                                  const std::uint8_t operation) -> bool {
        double result{};
        switch (operation) {
            case 0: result = lhs + rhs; break;
            case 1: result = lhs - rhs; break;
            case 2: result = lhs * rhs; break;
            case 3: result = lhs / rhs; break;
            case 4: result = std::fmod(lhs, rhs); break;
            default: return false;
        }
        SetWide(frame, dest, DoubleBits(result));
        return true;
    };

    // binop vAA, vBB, vCC (0x90..0xaf)
    if (opcode >= 0x90 && opcode <= 0xaf) {
        const auto second = units[frame.pc + 1];
        const auto src1 = static_cast<std::uint32_t>(second & 0xffU);
        const auto src2 = static_cast<std::uint32_t>((second >> 8U) & 0xffU);
        if (opcode <= 0x9a) {  // int
            return int_binop(vAA,
                             static_cast<std::int32_t>(GetCat1(frame, src1)),
                             static_cast<std::int32_t>(GetCat1(frame, src2)),
                             static_cast<std::uint8_t>(opcode - 0x90));
        }
        if (opcode <= 0xa5) {  // long
            return long_binop(vAA,
                              static_cast<std::int64_t>(GetWide(frame, src1)),
                              static_cast<std::int64_t>(GetWide(frame, src2)),
                              static_cast<std::uint8_t>(opcode - 0x9b));
        }
        if (opcode <= 0xaa) {  // float
            return float_binop(vAA, AsFloat(GetCat1(frame, src1)),
                               AsFloat(GetCat1(frame, src2)),
                               static_cast<std::uint8_t>(opcode - 0xa6));
        }
        return double_binop(vAA, AsDouble(GetWide(frame, src1)),
                            AsDouble(GetWide(frame, src2)),
                            static_cast<std::uint8_t>(opcode - 0xab));
    }

    // binop/2addr vA, vB (0xb0..0xcf)
    if (opcode >= 0xb0 && opcode <= 0xcf) {
        if (opcode <= 0xba) {  // int
            return int_binop(vA,
                             static_cast<std::int32_t>(GetCat1(frame, vA)),
                             static_cast<std::int32_t>(GetCat1(frame, vB4)),
                             static_cast<std::uint8_t>(opcode - 0xb0));
        }
        if (opcode <= 0xc5) {  // long
            return long_binop(vA,
                              static_cast<std::int64_t>(GetWide(frame, vA)),
                              static_cast<std::int64_t>(GetWide(frame, vB4)),
                              static_cast<std::uint8_t>(opcode - 0xbb));
        }
        if (opcode <= 0xca) {  // float
            return float_binop(vA, AsFloat(GetCat1(frame, vA)),
                               AsFloat(GetCat1(frame, vB4)),
                               static_cast<std::uint8_t>(opcode - 0xc6));
        }
        return double_binop(vA, AsDouble(GetWide(frame, vA)),
                            AsDouble(GetWide(frame, vB4)),
                            static_cast<std::uint8_t>(opcode - 0xcb));
    }

    // binop/lit16 vA, vB, #+CCCC (0xd0..0xd7)
    if (opcode >= 0xd0 && opcode <= 0xd7) {
        const auto literal = static_cast<std::int32_t>(
            static_cast<std::int16_t>(units[frame.pc + 1]));
        const auto lhs = static_cast<std::int32_t>(GetCat1(frame, vB4));
        switch (opcode) {
            case 0xd0: return int_binop(vA, lhs, literal, 0);  // add
            case 0xd1: return int_binop(vA, literal, lhs, 1);  // rsub
            case 0xd2: return int_binop(vA, lhs, literal, 2);  // mul
            case 0xd3: return int_binop(vA, lhs, literal, 3);  // div
            case 0xd4: return int_binop(vA, lhs, literal, 4);  // rem
            case 0xd5: return int_binop(vA, lhs, literal, 5);  // and
            case 0xd6: return int_binop(vA, lhs, literal, 6);  // or
            case 0xd7: return int_binop(vA, lhs, literal, 7);  // xor
            default: break;
        }
        return false;
    }

    // binop/lit8 vAA, vBB, #+CC (0xd8..0xe2)
    if (opcode >= 0xd8 && opcode <= 0xe2) {
        const auto second = units[frame.pc + 1];
        const auto src = static_cast<std::uint32_t>(second & 0xffU);
        const auto literal = static_cast<std::int32_t>(
            static_cast<std::int8_t>((second >> 8U) & 0xffU));
        const auto lhs = static_cast<std::int32_t>(GetCat1(frame, src));
        switch (opcode) {
            case 0xd8: return int_binop(vAA, lhs, literal, 0);   // add
            case 0xd9: return int_binop(vAA, literal, lhs, 1);   // rsub
            case 0xda: return int_binop(vAA, lhs, literal, 2);   // mul
            case 0xdb: return int_binop(vAA, lhs, literal, 3);   // div
            case 0xdc: return int_binop(vAA, lhs, literal, 4);   // rem
            case 0xdd: return int_binop(vAA, lhs, literal, 5);   // and
            case 0xde: return int_binop(vAA, lhs, literal, 6);   // or
            case 0xdf: return int_binop(vAA, lhs, literal, 7);   // xor
            case 0xe0: return int_binop(vAA, lhs, literal, 8);   // shl
            case 0xe1: return int_binop(vAA, lhs, literal, 9);   // shr
            case 0xe2: return int_binop(vAA, lhs, literal, 10);  // ushr
            default: break;
        }
        return false;
    }

    return false;
}

}  // namespace ogplay::runtime::dexvm

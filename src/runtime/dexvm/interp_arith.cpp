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
                                          const std::uint16_t unit,
                                          const FastInstruction* decoded) {
    const auto& units = frame.method->code->instructions;
    const auto vAA = decoded != nullptr
                         ? decoded->a
                         : static_cast<std::uint32_t>((unit >> 8U) & 0xffU);
    const auto vA = decoded != nullptr
                        ? decoded->a
                        : static_cast<std::uint32_t>((unit >> 8U) & 0xfU);
    const auto vB4 = decoded != nullptr
                         ? decoded->b
                         : static_cast<std::uint32_t>((unit >> 12U) & 0xfU);
    const auto get_cat1 = [&](const std::uint32_t reg) {
        return decoded != nullptr ? GetFastCat1(frame, reg)
                                  : GetCat1(frame, reg);
    };
    const auto get_wide = [&](const std::uint32_t reg) {
        return decoded != nullptr ? GetFastWide(frame, reg)
                                  : GetWide(frame, reg);
    };
    const auto set_cat1 = [&](const std::uint32_t reg,
                              const std::uint32_t bits) {
        if (decoded != nullptr) {
            SetFastCat1(frame, reg, bits);
        } else {
            SetCat1(frame, reg, bits);
        }
    };
    const auto set_wide = [&](const std::uint32_t reg,
                              const std::uint64_t bits) {
        if (decoded != nullptr) {
            SetFastWide(frame, reg, bits);
        } else {
            SetWide(frame, reg, bits);
        }
    };

    // ---- cmp family (0x2d..0x31) -----------------------------------------
    if (opcode >= 0x2d && opcode <= 0x31) {
        const auto second = decoded == nullptr ? units[frame.pc + 1] : 0U;
        const auto src1 = decoded != nullptr
                              ? decoded->b
                              : static_cast<std::uint32_t>(second & 0xffU);
        const auto src2 = decoded != nullptr
                              ? decoded->c
                              : static_cast<std::uint32_t>((second >> 8U) & 0xffU);
        std::int32_t result{};
        switch (opcode) {
            case 0x2d: {  // cmpl-float: NaN -> -1
                const auto lhs = AsFloat(get_cat1(src1));
                const auto rhs = AsFloat(get_cat1(src2));
                result = lhs > rhs ? 1 : (lhs == rhs ? 0 : -1);
                break;
            }
            case 0x2e: {  // cmpg-float: NaN -> 1
                const auto lhs = AsFloat(get_cat1(src1));
                const auto rhs = AsFloat(get_cat1(src2));
                result = lhs < rhs ? -1 : (lhs == rhs ? 0 : 1);
                break;
            }
            case 0x2f: {  // cmpl-double
                const auto lhs = AsDouble(get_wide(src1));
                const auto rhs = AsDouble(get_wide(src2));
                result = lhs > rhs ? 1 : (lhs == rhs ? 0 : -1);
                break;
            }
            case 0x30: {  // cmpg-double
                const auto lhs = AsDouble(get_wide(src1));
                const auto rhs = AsDouble(get_wide(src2));
                result = lhs < rhs ? -1 : (lhs == rhs ? 0 : 1);
                break;
            }
            case 0x31: {  // cmp-long
                const auto lhs = static_cast<std::int64_t>(
                    get_wide(src1));
                const auto rhs = static_cast<std::int64_t>(
                    get_wide(src2));
                result = lhs > rhs ? 1 : (lhs == rhs ? 0 : -1);
                break;
            }
            default:
                break;
        }
        set_cat1(vAA, static_cast<std::uint32_t>(result));
        return true;
    }

    // ---- unary ops (0x7b..0x8f) --------------------------------------------
    if (opcode >= 0x7b && opcode <= 0x8f) {
        switch (opcode) {
            case 0x7b:  // neg-int
                set_cat1(vA, static_cast<std::uint32_t>(
                    -static_cast<std::int32_t>(get_cat1(vB4))));
                break;
            case 0x7c:  // not-int
                set_cat1(vA, ~get_cat1(vB4));
                break;
            case 0x7d:  // neg-long
                set_wide(vA, static_cast<std::uint64_t>(
                    -static_cast<std::int64_t>(get_wide(vB4))));
                break;
            case 0x7e:  // not-long
                set_wide(vA, ~get_wide(vB4));
                break;
            case 0x7f:  // neg-float
                set_cat1(vA, FloatBits(-AsFloat(get_cat1(vB4))));
                break;
            case 0x80:  // neg-double
                set_wide(vA, DoubleBits(-AsDouble(get_wide(vB4))));
                break;
            case 0x81:  // int-to-long
                set_wide(vA, static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(
                        static_cast<std::int32_t>(get_cat1(vB4)))));
                break;
            case 0x82:  // int-to-float
                set_cat1(vA, FloatBits(static_cast<float>(
                    static_cast<std::int32_t>(get_cat1(vB4)))));
                break;
            case 0x83:  // int-to-double
                set_wide(vA, DoubleBits(static_cast<double>(
                    static_cast<std::int32_t>(get_cat1(vB4)))));
                break;
            case 0x84:  // long-to-int
                set_cat1(vA, static_cast<std::uint32_t>(
                    get_wide(vB4)));
                break;
            case 0x85:  // long-to-float
                set_cat1(vA, FloatBits(static_cast<float>(
                    static_cast<std::int64_t>(get_wide(vB4)))));
                break;
            case 0x86:  // long-to-double
                set_wide(vA, DoubleBits(static_cast<double>(
                    static_cast<std::int64_t>(get_wide(vB4)))));
                break;
            case 0x87:  // float-to-int
                set_cat1(vA, static_cast<std::uint32_t>(
                    SaturateToInt<std::int32_t>(
                        AsFloat(get_cat1(vB4)))));
                break;
            case 0x88:  // float-to-long
                set_wide(vA, static_cast<std::uint64_t>(
                    SaturateToInt<std::int64_t>(
                        AsFloat(get_cat1(vB4)))));
                break;
            case 0x89:  // float-to-double
                set_wide(vA, DoubleBits(static_cast<double>(
                    AsFloat(get_cat1(vB4)))));
                break;
            case 0x8a:  // double-to-int
                set_cat1(vA, static_cast<std::uint32_t>(
                    SaturateToInt<std::int32_t>(
                        AsDouble(get_wide(vB4)))));
                break;
            case 0x8b:  // double-to-long
                set_wide(vA, static_cast<std::uint64_t>(
                    SaturateToInt<std::int64_t>(
                        AsDouble(get_wide(vB4)))));
                break;
            case 0x8c:  // double-to-float
                set_cat1(vA, FloatBits(static_cast<float>(
                    AsDouble(get_wide(vB4)))));
                break;
            case 0x8d:  // int-to-byte
                set_cat1(vA, static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(static_cast<std::int8_t>(
                        get_cat1(vB4) & 0xffU))));
                break;
            case 0x8e:  // int-to-char
                set_cat1(vA, get_cat1(vB4) & 0xffffU);
                break;
            case 0x8f:  // int-to-short
                set_cat1(vA, static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(static_cast<std::int16_t>(
                        get_cat1(vB4) & 0xffffU))));
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
        set_cat1(dest, static_cast<std::uint32_t>(result));
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
        set_wide(dest, static_cast<std::uint64_t>(result));
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
        set_cat1(dest, FloatBits(result));
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
        set_wide(dest, DoubleBits(result));
        return true;
    };

    // binop vAA, vBB, vCC (0x90..0xaf)
    if (opcode >= 0x90 && opcode <= 0xaf) {
        const auto second = decoded == nullptr ? units[frame.pc + 1] : 0U;
        const auto src1 = decoded != nullptr
                              ? decoded->b
                              : static_cast<std::uint32_t>(second & 0xffU);
        const auto src2 = decoded != nullptr
                              ? decoded->c
                              : static_cast<std::uint32_t>((second >> 8U) & 0xffU);
        if (opcode <= 0x9a) {  // int
            return int_binop(vAA,
                             static_cast<std::int32_t>(get_cat1(src1)),
                             static_cast<std::int32_t>(get_cat1(src2)),
                             static_cast<std::uint8_t>(opcode - 0x90));
        }
        if (opcode <= 0xa5) {  // long
            return long_binop(vAA,
                              static_cast<std::int64_t>(get_wide(src1)),
                              static_cast<std::int64_t>(get_wide(src2)),
                              static_cast<std::uint8_t>(opcode - 0x9b));
        }
        if (opcode <= 0xaa) {  // float
            return float_binop(vAA, AsFloat(get_cat1(src1)),
                               AsFloat(get_cat1(src2)),
                               static_cast<std::uint8_t>(opcode - 0xa6));
        }
        return double_binop(vAA, AsDouble(get_wide(src1)),
                            AsDouble(get_wide(src2)),
                            static_cast<std::uint8_t>(opcode - 0xab));
    }

    // binop/2addr vA, vB (0xb0..0xcf)
    if (opcode >= 0xb0 && opcode <= 0xcf) {
        if (opcode <= 0xba) {  // int
            return int_binop(vA,
                             static_cast<std::int32_t>(get_cat1(vA)),
                             static_cast<std::int32_t>(get_cat1(vB4)),
                             static_cast<std::uint8_t>(opcode - 0xb0));
        }
        if (opcode <= 0xc5) {  // long
            return long_binop(vA,
                              static_cast<std::int64_t>(get_wide(vA)),
                              static_cast<std::int64_t>(get_wide(vB4)),
                              static_cast<std::uint8_t>(opcode - 0xbb));
        }
        if (opcode <= 0xca) {  // float
            return float_binop(vA, AsFloat(get_cat1(vA)),
                               AsFloat(get_cat1(vB4)),
                               static_cast<std::uint8_t>(opcode - 0xc6));
        }
        return double_binop(vA, AsDouble(get_wide(vA)),
                            AsDouble(get_wide(vB4)),
                            static_cast<std::uint8_t>(opcode - 0xcb));
    }

    // binop/lit16 vA, vB, #+CCCC (0xd0..0xd7)
    if (opcode >= 0xd0 && opcode <= 0xd7) {
        const auto literal = decoded != nullptr
                                 ? static_cast<std::int32_t>(
                                       static_cast<std::int64_t>(decoded->extra))
                                 : static_cast<std::int32_t>(
                                       static_cast<std::int16_t>(units[frame.pc + 1]));
        const auto lhs = static_cast<std::int32_t>(get_cat1(vB4));
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
        const auto second = decoded == nullptr ? units[frame.pc + 1] : 0U;
        const auto src = decoded != nullptr
                             ? decoded->b
                             : static_cast<std::uint32_t>(second & 0xffU);
        const auto literal = decoded != nullptr
                                 ? static_cast<std::int32_t>(
                                       static_cast<std::int64_t>(decoded->extra))
                                 : static_cast<std::int32_t>(
                                       static_cast<std::int8_t>(
                                           (second >> 8U) & 0xffU));
        const auto lhs = static_cast<std::int32_t>(get_cat1(src));
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

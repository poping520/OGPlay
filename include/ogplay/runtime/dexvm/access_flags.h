#pragma once

#include <cstdint>

namespace ogplay::runtime::dexvm {

// DEX access flags from the pinned Dalvik libdex/DexFile.h. Some bits have
// different names depending on whether they describe a class, field, or method.
inline constexpr std::uint32_t kAccNone = 0U;
inline constexpr std::uint32_t kAccPublic = 0x00000001U;
inline constexpr std::uint32_t kAccPrivate = 0x00000002U;
inline constexpr std::uint32_t kAccProtected = 0x00000004U;
inline constexpr std::uint32_t kAccStatic = 0x00000008U;
inline constexpr std::uint32_t kAccFinal = 0x00000010U;
inline constexpr std::uint32_t kAccSynchronized = 0x00000020U;
inline constexpr std::uint32_t kAccSuper = kAccSynchronized;
inline constexpr std::uint32_t kAccVolatile = 0x00000040U;
inline constexpr std::uint32_t kAccBridge = kAccVolatile;
inline constexpr std::uint32_t kAccTransient = 0x00000080U;
inline constexpr std::uint32_t kAccVarArgs = kAccTransient;
inline constexpr std::uint32_t kAccNative = 0x00000100U;
inline constexpr std::uint32_t kAccInterface = 0x00000200U;
inline constexpr std::uint32_t kAccAbstract = 0x00000400U;
inline constexpr std::uint32_t kAccStrict = 0x00000800U;
inline constexpr std::uint32_t kAccSynthetic = 0x00001000U;
inline constexpr std::uint32_t kAccAnnotation = 0x00002000U;
inline constexpr std::uint32_t kAccEnum = 0x00004000U;
inline constexpr std::uint32_t kAccMiranda = 0x00008000U;
inline constexpr std::uint32_t kAccConstructor = 0x00010000U;
inline constexpr std::uint32_t kAccDeclaredSynchronized = 0x00020000U;

inline constexpr std::uint32_t kAccVisibilityMask =
    kAccPublic | kAccPrivate | kAccProtected;
inline constexpr std::uint32_t kAccClassMask =
    kAccPublic | kAccFinal | kAccInterface | kAccAbstract | kAccSynthetic |
    kAccAnnotation | kAccEnum;
inline constexpr std::uint32_t kAccInnerClassMask =
    kAccClassMask | kAccPrivate | kAccProtected | kAccStatic;
inline constexpr std::uint32_t kAccFieldMask =
    kAccPublic | kAccPrivate | kAccProtected | kAccStatic | kAccFinal |
    kAccVolatile | kAccTransient | kAccSynthetic | kAccEnum;
inline constexpr std::uint32_t kAccMethodMask =
    kAccPublic | kAccPrivate | kAccProtected | kAccStatic | kAccFinal |
    kAccSynchronized | kAccBridge | kAccVarArgs | kAccNative | kAccAbstract |
    kAccStrict | kAccSynthetic | kAccConstructor | kAccDeclaredSynchronized;
inline constexpr std::uint32_t kAccJavaFlagsMask = 0x0000ffffU;

// Java 7/API 19 reflection masks from java.lang.reflect.Modifier. These are
// intentionally narrower than the DEX validation masks above.
inline constexpr std::uint32_t kJavaClassModifierMask =
    kAccVisibilityMask | kAccAbstract | kAccStatic | kAccFinal | kAccStrict;
inline constexpr std::uint32_t kJavaConstructorModifierMask =
    kAccVisibilityMask;
inline constexpr std::uint32_t kJavaFieldModifierMask =
    kAccVisibilityMask | kAccStatic | kAccFinal | kAccTransient | kAccVolatile;
inline constexpr std::uint32_t kJavaInterfaceModifierMask =
    kAccVisibilityMask | kAccAbstract | kAccStatic | kAccStrict;
inline constexpr std::uint32_t kJavaMethodModifierMask =
    kAccVisibilityMask | kAccAbstract | kAccStatic | kAccFinal |
    kAccSynchronized | kAccNative | kAccStrict;

static_assert(kAccClassMask == 0x00007611U);
static_assert(kAccInnerClassMask == 0x0000761fU);
static_assert(kAccFieldMask == 0x000050dfU);
static_assert(kAccMethodMask == 0x00031dffU);
static_assert(kJavaClassModifierMask == 0x00000c1fU);
static_assert(kJavaConstructorModifierMask == 0x00000007U);
static_assert(kJavaFieldModifierMask == 0x000000dfU);
static_assert(kJavaInterfaceModifierMask == 0x00000c0fU);
static_assert(kJavaMethodModifierMask == 0x00000d3fU);

}  // namespace ogplay::runtime::dexvm

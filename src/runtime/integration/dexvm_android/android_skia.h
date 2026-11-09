#pragma once

#include "catalog.h"
#include "runtime/raster/objects.h"

#include <bit>

namespace ogplay::runtime::android_intrinsics::skia {

constexpr auto kStaticNative = dx::kAccNative | dx::kAccPrivate | dx::kAccStatic;

template <class T>
raster::Token<T> NativeToken(std::int32_t bits) {
    return {std::bit_cast<std::uint32_t>(bits)};
}

template <class T>
dx::VmValue JavaToken(raster::Token<T> token) {
    return dx::VmValue::Int(std::bit_cast<std::int32_t>(token.bits));
}

template <class T>
T& Require(raster::Objects<T>& objects, std::int32_t bits) {
    if (auto* object = objects.Find(NativeToken<T>(bits))) return *object;
    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid or retired graphics native handle"};
}

dx::Slot& Field(dx::Interpreter& vm, dx::VmObjectRef owner, const char* name, const char* descriptor);
std::int32_t IntField(dx::Interpreter& vm, dx::VmObjectRef owner, const char* name);
SkRect ReadRectF(dx::Interpreter& vm, dx::VmObjectRef owner);
SkIRect ReadRect(dx::Interpreter& vm, dx::VmObjectRef owner);
void WriteRectF(dx::Interpreter& vm, dx::VmObjectRef owner, const SkRect& rect);
void WriteRect(dx::Interpreter& vm, dx::VmObjectRef owner, const SkIRect& rect);
void CheckRange(dx::Interpreter& vm, dx::VmObjectRef array, int start, std::int64_t length);
float ReadFloat(dx::Interpreter& vm, dx::VmObjectRef array, int index);
void WriteFloat(dx::Interpreter& vm, dx::VmObjectRef array, int index, float value);
dx::VmValue CallVirtual(dx::Interpreter& vm, dx::VmObjectRef receiver, const char* name, const char* signature,
                        std::initializer_list<dx::VmValue> arguments = {});

}  // namespace ogplay::runtime::android_intrinsics::skia

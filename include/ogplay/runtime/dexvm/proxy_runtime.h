#pragma once

#include <string>

#include "ogplay/runtime/dexvm/dexvm_types.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm {

// Dalvik 4.4 dvmGenerateProxyClass: a final public subclass of java.lang.reflect.Proxy
// implementing `interfaces` in order. Every generated method forwards to
// InvocationHandler.invoke; java.lang.reflect.Proxy keeps the class cache and the
// visibility checks. `name` uses slashes, as Proxy.getProxyClass passes it.
[[nodiscard]] DexClassId GenerateProxyClass(IntrinsicContext& call, const std::string& name, VmObjectRef interfaces,
                                            VmObjectRef loader);

}  // namespace ogplay::runtime::dexvm

#pragma once

#include "ogplay/runtime/dexvm/interpreter.h"
#include <exception>
#include <stdexcept>
#include <string>

namespace ogplay::app::detail {
inline std::string DescribeRuntimeFailure(std::exception_ptr failure) {
    if (!failure) return "unknown CLI failure";
    try {
        std::rethrow_exception(failure);
    } catch (const runtime::dexvm::VmJavaThrow& error) {
        return "dexvm::VmJavaThrow\n  exception: " + error.descriptor + "\n  message: " + error.message;
    } catch (const std::exception& error) {
        return error.what();
    } catch (...) {
        return "unknown CLI failure";
    }
}

// Kernel RTTI is private to ogplaycore. Convert before crossing the app DSO.
[[noreturn]] inline void RethrowRuntimeFailure(std::exception_ptr failure) {
    try {
        std::rethrow_exception(failure);
    } catch (const runtime::dexvm::VmJavaThrow&) {
        throw std::runtime_error(DescribeRuntimeFailure(failure));
    }
}
}  // namespace ogplay::app::detail

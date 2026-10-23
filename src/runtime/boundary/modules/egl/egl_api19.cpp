#include "runtime/boundary/modules/egl/egl_module.h"

#include <bit>
#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"

namespace ogplay::runtime {

BoundaryResult EglModule::SurfaceExtension(const A32CallFrame& call, const bool presentation) {
    std::scoped_lock lock(mutex_);
    auto& error = threads_[call.ThreadId()].error;
    error = 0x3000U;
    if (call.Argument(0) != 1U) {
        error = 0x3008U;
        return 0U;
    }
    if (!initialized_) {
        error = 0x3001U;
        return 0U;
    }
    const auto surface = surfaces_.find(call.Argument(1));
    if (surface == surfaces_.end() || surface->second.display != call.Argument(0)) {
        error = 0x300DU;
        return 0U;
    }
    if (presentation) {
        // 4.4 eglApi.cpp validates display/surface, sets the window timestamp
        // and returns TRUE (including when the native window rejects it).
        // This is metadata for the producer, not a host sleep/deadline.
        const auto bits =
            static_cast<std::uint64_t>(call.Argument(2)) | (static_cast<std::uint64_t>(call.Argument(3)) << 32U);
        surface->second.presentation_time_ns = std::bit_cast<std::int64_t>(bits);
        return 1U;
    }
    // ANGLE has no guest lock-surface backing. As in 4.4, an absent driver
    // entry returns FALSE/BAD_DISPLAY; the attribute pointer is not read.
    if (context_.ledger) context_.ledger->RecordUnimplemented("runtime.egl_api19_exports", call.LinkRegister());
    if (calls_.logger)
        calls_.logger->Write(core::LogLevel::error, "guest.egl", "EGL lock surface driver entry is unavailable",
                             {.guest_thread = call.ThreadId()});
    error = 0x3008U;
    return 0U;
}

BoundaryResult EglModule::LockSurface(const A32CallFrame& call) {
    return SurfaceExtension(call, false);
}

BoundaryResult EglModule::UnlockSurface(const A32CallFrame& call) {
    return SurfaceExtension(call, false);
}

BoundaryResult EglModule::PresentationTime(const A32CallFrame& call) {
    return SurfaceExtension(call, true);
}

BoundaryResult EglModule::UnsupportedClock(const A32CallFrame& call) {
    // ANGLE does not provide the NV clock extension. Match the absent-driver
    // branch of eglGetSystemTime{Frequency,}NV, never substitute host wall time.
    SetError(call.ThreadId(), 0x3008U);
    if (context_.ledger) context_.ledger->RecordUnimplemented("runtime.egl_api19_exports", call.LinkRegister());
    if (calls_.logger)
        calls_.logger->Write(core::LogLevel::error, "guest.egl", "EGL NV system clock driver entry is unavailable",
                             {.guest_thread = call.ThreadId()});
    return BoundaryResult::Wide(0U);
}

BoundaryResult EglModule::GetSystemTimeFrequency(const A32CallFrame& call) {
    return UnsupportedClock(call);
}

BoundaryResult EglModule::GetSystemTime(const A32CallFrame& call) {
    return UnsupportedClock(call);
}

}  // namespace ogplay::runtime

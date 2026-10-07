#pragma once
#include <mutex>
#include "ogplay/runtime/boundary/bitmap_boundary.h"
#include "runtime/boundary/core/boundary_binding.h"
namespace ogplay::runtime {
class BitmapModule final {
public:
    explicit BitmapModule(BoundaryCallServices& calls) : calls_(calls) {}
    BoundaryCallServices& CallServices() noexcept { return calls_; }
    void SetHooks(AndroidBitmapHooks hooks) { std::scoped_lock guard(mutex_); hooks_ = hooks; }
    BoundaryResult GetInfo(const A32CallFrame& call);
    BoundaryResult LockPixels(const A32CallFrame& call);
    BoundaryResult UnlockPixels(const A32CallFrame& call);
private:
    BoundaryCallServices& calls_;
    AndroidBitmapHooks hooks_;
    std::recursive_mutex mutex_;
};
}

#include "bitmap_module.h"
#include <array>
#include <optional>
#include "ogplay/memory/address_space.h"

namespace ogplay::runtime {
namespace {
BoundaryResult Result(std::int32_t value) { return static_cast<std::uint32_t>(value); }
template<std::size_t N>
std::array<std::byte, N * 4> Words(const std::array<std::uint32_t, N>& words) {
    std::array<std::byte, N * 4> bytes{};
    for (std::size_t i = 0; i < N; ++i)
        for (std::size_t b = 0; b < 4; ++b)
            bytes[i * 4 + b] = static_cast<std::byte>(words[i] >> (b * 8));
    return bytes;
}
}
BoundaryResult BitmapModule::GetInfo(const A32CallFrame& call) {
    const std::scoped_lock guard(mutex_);
    const auto env = call.Pointer<void>(0).Address();
    const BitmapJavaReference ref{call.Argument(1)};
    if (env.IsNull() || ref.token == 0) return Result(-1);
    const auto out = call.Pointer<AndroidBitmapInfo>(2);
    std::optional<memory::ValidatedGuestWrite> write;
    try {
        if (!out.IsNull()) write = calls_.address_space.PreflightWrite(
            memory::GuestRange(out.Address(), 20), call.ThreadId());
    } catch (const std::exception&) { return Result(-1); }
    if (!hooks_.info) return Result(-2);
    AndroidBitmapInfo info;
    const auto status = hooks_.info(hooks_.owner, call.ThreadId(), env, ref, info);
    if (status == 0 && write) {
        const auto bytes = Words<5>({info.width, info.height, info.stride,
                                    static_cast<std::uint32_t>(info.format), info.flags});
        calls_.address_space.WritePrevalidated(*write, bytes);
    }
    return Result(status);
}
BoundaryResult BitmapModule::LockPixels(const A32CallFrame& call) {
    const std::scoped_lock guard(mutex_);
    const auto env = call.Pointer<void>(0).Address();
    const BitmapJavaReference ref{call.Argument(1)};
    if (env.IsNull() || ref.token == 0) return Result(-1);
    const auto out = call.Pointer<memory::GuestAddress>(2);
    std::optional<memory::ValidatedGuestWrite> write;
    try {
        if (!out.IsNull()) write = calls_.address_space.PreflightWrite(
            memory::GuestRange(out.Address(), 4), call.ThreadId());
    } catch (const std::exception&) { return Result(-1); }
    if (!hooks_.lock) return Result(-2);
    memory::GuestAddress address;
    const auto status = hooks_.lock(hooks_.owner, call.ThreadId(), env, ref, address);
    if (status == 0 && write) {
        const auto bytes = Words<1>({address.Value()});
        calls_.address_space.WritePrevalidated(*write, bytes);
    }
    return Result(status);
}
BoundaryResult BitmapModule::UnlockPixels(const A32CallFrame& call) {
    const std::scoped_lock guard(mutex_);
    const auto env = call.Pointer<void>(0).Address();
    const BitmapJavaReference ref{call.Argument(1)};
    if (env.IsNull() || ref.token == 0) return Result(-1);
    if (!hooks_.unlock) return Result(-2);
    return Result(hooks_.unlock(hooks_.owner, call.ThreadId(), env, ref));
}
}

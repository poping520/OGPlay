#include "rom_input.h"
#include "velocity_tracker.h"

#include <bit>

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_VelocityTracker(const Context& context) {
    using V = dx::VmValue;
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/view/VelocityTracker;");
    b.MergesBootClass();
    constexpr auto native = dx::kAccPrivate | dx::kAccStatic | dx::kAccNative;
    const auto tracker = [context](int id) -> VelocityTracker& {
        const auto found = RomInput(context).velocities.find(id);
        if (found == RomInput(context).velocities.end())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid VelocityTracker handle"};
        return *found->second;
    };
    b.StaticMethod(
        "nativeInitialize", "(Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& c) {
            if (c.arguments[0].ref.IsValid() && c.vm.StringUtf8(c.arguments[0].ref) != "lsq2") {
                if (auto* ledger = c.vm.Ledger())
                    ledger->RecordUnimplemented("android.view.VelocityTracker.nativeInitialize(Ljava/lang/String;)I",
                                                0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "VelocityTracker supports the API19 default lsq2 strategy"};
            }
            auto& s = RomInput(context);
            const int id = AllocateRomInputHandle(s);
            s.velocities.emplace(id, std::make_shared<VelocityTracker>());
            return V::Int(id);
        },
        native);
    b.StaticMethod(
        "nativeDispose", "(I)V",
        [context](dx::IntrinsicContext& c) {
            RomInput(context).velocities.erase(c.arguments[0].AsInt());
            return V::Void();
        },
        native);
    b.StaticMethod(
        "nativeClear", "(I)V",
        [tracker](dx::IntrinsicContext& c) {
            tracker(c.arguments[0].AsInt()).Clear();
            return V::Void();
        },
        native);
    b.StaticMethod(
        "nativeAddMovement", "(ILandroid/view/MotionEvent;)V",
        [tracker](dx::IntrinsicContext& c) {
            tracker(c.arguments[0].AsInt()).Add(ReadMotionEvent(c.vm, c.arguments[1].ref));
            return V::Void();
        },
        native);
    b.StaticMethod(
        "nativeComputeCurrentVelocity", "(IIF)V",
        [tracker](dx::IntrinsicContext& c) {
            tracker(c.arguments[0].AsInt()).Compute(c.arguments[1].AsInt(), c.arguments[2].AsFloat());
            return V::Void();
        },
        native);
    for (int axis = 0; axis < 2; ++axis)
        b.StaticMethod(
            axis == 0 ? "nativeGetXVelocity" : "nativeGetYVelocity", "(II)F",
            [tracker, axis](dx::IntrinsicContext& c) {
                return V::Float(
                    tracker(c.arguments[0].AsInt()).Velocity(c.arguments[1].AsInt())[static_cast<std::size_t>(axis)]);
            },
            native);
    b.StaticMethod(
        "nativeGetEstimator", "(IILandroid/view/VelocityTracker$Estimator;)Z",
        [tracker](dx::IntrinsicContext& c) {
            const auto e = tracker(c.arguments[0].AsInt()).Estimate(c.arguments[1].AsInt());
            if (!e) return V::Int(0);
            const auto out = dx::IntrinsicCall(c).NonNullRef(2, "estimator");
            WindowField(c.vm, out, "degree", "I") = {static_cast<unsigned>(e->degree), dx::SlotTag::cat1};
            WindowField(c.vm, out, "confidence", "F") = {std::bit_cast<unsigned>(e->confidence), dx::SlotTag::cat1};
            for (bool y : {false, true}) {
                const auto array = dx::VmObjectRef{WindowField(c.vm, out, y ? "yCoeff" : "xCoeff", "[F").bits};
                for (int i = 0; i < 5; ++i)
                    c.vm.Model().SetPrimitiveElement(
                        array, i, std::bit_cast<unsigned>((y ? e->y : e->x)[static_cast<std::size_t>(i)]));
            }
            return V::Int(1);
        },
        native);
    return std::move(b).Build();
}
}  // namespace ogplay::runtime::android_intrinsics

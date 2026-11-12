#include "android_skia.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Camera_Natives(const Context& context) {
    using namespace skia;
    using V = dx::VmValue;
    using Camera = raster::Camera;
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Camera;");
    b.MergesBootClass();
    constexpr auto private_native = dx::kAccPrivate | dx::kAccNative;
    constexpr auto public_native = dx::kAccPublic | dx::kAccNative;
    const auto peer = [context](dx::IntrinsicContext& c) -> Camera& {
        return Require(context->skia_objects->cameras, IntField(c.vm, c.receiver, "native_instance"));
    };
    b.DirectMethod(
        "nativeConstructor", "()V",
        [context](dx::IntrinsicContext& c) {
            auto& field = Field(c.vm, c.receiver, "native_instance", "I");
            field = {context->skia_objects->cameras.Create().bits, dx::SlotTag::cat1};
            return V::Void();
        },
        private_native);
    b.DirectMethod(
        "nativeDestructor", "()V",
        [context](dx::IntrinsicContext& c) {
            auto& field = Field(c.vm, c.receiver, "native_instance", "I");
            context->skia_objects->cameras.Retire({field.bits});
            field = {0, dx::SlotTag::cat1};
            return V::Void();
        },
        private_native);
    b.VirtualMethod(
        "save", "()V",
        [peer](dx::IntrinsicContext& c) {
            auto& camera = peer(c);
            const auto transform = camera.transforms.back();
            camera.transforms.push_back(transform);
            try {
                camera.view.save();
            } catch (...) {
                camera.transforms.pop_back();
                throw;
            }
            return V::Void();
        },
        public_native);
    b.VirtualMethod(
        "restore", "()V",
        [peer](dx::IntrinsicContext& c) {
            auto& camera = peer(c);
            if (camera.transforms.size() > 1) {
                camera.view.restore();
                camera.transforms.pop_back();
            }
            return V::Void();
        },
        public_native);
    b.VirtualMethod(
        "translate", "(FFF)V",
        [peer](dx::IntrinsicContext& c) {
            auto& camera = peer(c);
            const dx::IntrinsicCall a(c);
            camera.view.translate(a.Float(0), a.Float(1), a.Float(2));
            camera.transforms.back().preTranslate(a.Float(0), a.Float(1), a.Float(2));
            return V::Void();
        },
        public_native);
    constexpr auto rotate = [](Camera& camera, int axis, float degrees) {
        constexpr void (Sk3DView::* methods[])(float){&Sk3DView::rotateX, &Sk3DView::rotateY, &Sk3DView::rotateZ};
        constexpr SkV3 axes[]{{1, 0, 0}, {0, -1, 0}, {0, 0, 1}};
        (camera.view.*methods[axis])(degrees);
        camera.transforms.back().preConcat(SkM44::Rotate(axes[axis], degrees * SK_ScalarPI / 180));
    };
    constexpr const char* rotations[]{"rotateX", "rotateY", "rotateZ"};
    for (int i = 0; i < 3; ++i)
        b.VirtualMethod(
            rotations[i], "(F)V",
            [peer, i, rotate](dx::IntrinsicContext& c) {
                rotate(peer(c), i, c.arguments[0].AsFloat());
                return V::Void();
            },
            public_native);
    b.VirtualMethod(
        "rotate", "(FFF)V",
        [peer, rotate](dx::IntrinsicContext& c) {
            auto& camera = peer(c);
            for (int i = 0; i < 3; ++i)
                rotate(camera, i, c.arguments[static_cast<std::size_t>(i)].AsFloat());
            return V::Void();
        },
        public_native);
    b.VirtualMethod(
        "setLocation", "(FFF)V",
        [peer](dx::IntrinsicContext& c) {
            auto& projection = peer(c).projection;
            const dx::IntrinsicCall a(c);
            projection.fLocation = {72 * a.Float(0), 72 * a.Float(1), 72 * a.Float(2)};
            projection.fObserver = {0, 0, projection.fLocation.z};
            projection.update();
            return V::Void();
        },
        public_native);
    constexpr const char* locations[]{"getLocationX", "getLocationY", "getLocationZ"};
    constexpr float SkV3::* axes[]{&SkV3::x, &SkV3::y, &SkV3::z};
    for (int i = 0; i < 3; ++i)
        b.VirtualMethod(
            locations[i], "()F",
            [peer, member = axes[i]](dx::IntrinsicContext& c) {
                return V::Float((peer(c).projection.fLocation.*member) / 72.F);
            },
            public_native);
    b.DirectMethod(
        "nativeGetMatrix", "(I)V",
        [context, peer](dx::IntrinsicContext& c) {
            peer(c).GetMatrix(Require(context->skia_objects->matrices, c.arguments[0].AsInt()));
            return V::Void();
        },
        private_native);
    b.DirectMethod(
        "nativeApplyToCanvas", "(I)V",
        [context, peer](dx::IntrinsicContext& c) {
            SkMatrix matrix;
            peer(c).GetMatrix(matrix);
            Require(context->skia_objects->canvases, c.arguments[0].AsInt()).Matrix().preConcat(matrix);
            return V::Void();
        },
        private_native);
    b.VirtualMethod(
        "dotWithNormal", "(FFF)F",
        [peer](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Float(peer(c).view.dotWithNormal(a.Float(0), a.Float(1), a.Float(2)));
        },
        public_native);
    return std::move(b).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

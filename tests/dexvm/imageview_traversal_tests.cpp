#include "container_traversal_fixture.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"

using ogplay::test::container_traversal::ContainerVm;

namespace {
VmObjectRef Owner(ContainerVm& f) {
    f.context->ui_test_accessibility_disabled = true;
    f.context->target_sdk_version = 19;
    f.context->arsc.package_id = 0x7f;
    ogplay::loader::ArscEntry theme;
    theme.resource_id = 0x7f020001;
    theme.type_name = "style";
    theme.is_complex = true;
    theme.parent = 0x01030005;
    theme.bag = {{0x01010064, 1, 0, {}},
                 {0x01010065, 1, 0, {}},
                 {0x01010034, 1, 0, {}},
                 {0x010100d4, 1, 0, {}},
                 {0x01010072, 1, 0x7f020002, {}}};
    auto style = theme;
    style.resource_id = 0x7f020002;
    style.parent = 0;
    style.bag.clear();
    f.context->arsc.entries = {theme, style};
    f.context->application_theme = theme.resource_id;
    const auto owner = f.New("Landroid/app/Activity;");
    ogplay::test::AttachViewTestContext(f.vm, owner);
    f.On(owner, "setTheme", "(I)V", {VmValue::Int(static_cast<int>(theme.resource_id))});
    return owner;
}

VmObjectRef Scale(ContainerVm& f, int index) {
    const auto values =
        f.Static("Landroid/widget/ImageView$ScaleType;", "values", "()[Landroid/widget/ImageView$ScaleType;").ref;
    REQUIRE(f.model.ArrayLength(values) == 8);
    return f.model.GetObjectElement(values, index);
}

VmObjectRef Image(ContainerVm& f, VmObjectRef owner) {
    return f.New("Landroid/widget/ImageView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
}

void PaintBitmap(ContainerVm& f, VmObjectRef bitmap, std::uint32_t color) {
    const auto pixels = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 72);
    const auto root = f.vm.ProtectReferences(std::array{pixels});
    for (int i = 0; i < 72; ++i)
        f.model.SetPrimitiveElement(pixels, i, color);
    f.On(bitmap, "setPixels", "([IIIIIII)V",
         {VmValue::Ref(pixels), VmValue::Int(0), VmValue::Int(12), VmValue::Int(0), VmValue::Int(0), VmValue::Int(12),
          VmValue::Int(6)});
}
}  // namespace

TEST_CASE("DVM-296 ImageView ROM shape and leaf measurement both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto image = Image(f, owner);
        const auto root = f.vm.ProtectReferences(std::array{image});
        const auto image_class = f.linker.ResolveDescriptor("Landroid/widget/ImageView;");
        const auto set_bitmap = f.linker.FindVtableIndex(image_class, "setImageBitmap", "(Landroid/graphics/Bitmap;)V");
        REQUIRE(set_bitmap);
        CHECK(f.linker.Method(f.linker.Class(image_class).vtable[*set_bitmap]).code.has_value());
        CHECK_FALSE(static_cast<bool>(f.linker.Method(f.linker.Class(image_class).vtable[*set_bitmap]).implementation));
        const auto color =
            f.New("Landroid/graphics/drawable/ColorDrawable;", "(I)V", {VmValue::Int(static_cast<int>(0xff123456U))});
        f.On(image, "setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(color)});
        CHECK(f.On(image, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref == color);
        CHECK(f.On(color, "getIntrinsicWidth", "()I").AsInt() == -1);
        CHECK(f.On(color, "getIntrinsicHeight", "()I").AsInt() == -1);
        const auto node = *FindViewUiNode(*f.context, image.Value());
        CHECK(f.context->ui_tree.Get(node)->intrinsic == ui::Size{-1, -1});
        f.On(image, "setPadding", "(IIII)V", {VmValue::Int(1), VmValue::Int(2), VmValue::Int(3), VmValue::Int(4)});
        const auto before = f.context->ui_tree.Get(node)->measured;
        for (auto st : {1, 3, 5, 6, 7}) {
            f.On(image, "setScaleType", "(Landroid/widget/ImageView$ScaleType;)V", {VmValue::Ref(Scale(f, st))});
            CHECK(f.On(image, "getScaleType", "()Landroid/widget/ImageView$ScaleType;").ref == Scale(f, st));
            for (auto mode : {0U, 0x40000000U, 0x80000000U}) {
                f.On(image, "requestLayout", "()V");
                f.On(image, "measure", "(II)V",
                     {VmValue::Int(static_cast<int>(mode | 3)), VmValue::Int(static_cast<int>(mode | 5))});
                CHECK(f.On(image, "getMeasuredWidth", "()I").AsInt() == (mode ? 3 : 5));
                CHECK(f.On(image, "getMeasuredHeight", "()I").AsInt() == (mode ? 5 : 7));
                CHECK(f.On(image, "getMeasuredState", "()I").cat1 == (mode == 0x80000000 ? 0x01000100U : 0U));
                CHECK(f.context->ui_tree.Get(node)->measured == before);
            }
        }
        // Virtual onMeasure overrides and invoke-super both traverse the actual ROM View.measure.
        for (auto type : {"Ltest/ImageMeasureOverride;", "Ltest/ImageMeasureSuper;"}) {
            const auto custom = f.New(type, "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            const auto custom_root = f.vm.ProtectReferences(std::array{custom});
            f.On(custom, "setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(color)});
            f.On(custom, "measure", "(II)V", {VmValue::Int(0), VmValue::Int(0)});
            const bool super = std::string_view(type) == "Ltest/ImageMeasureSuper;";
            CHECK(f.On(custom, "getMeasuredWidth", "()I").AsInt() == (super ? 1 : 37));
            CHECK(f.On(custom, "getMeasuredHeight", "()I").AsInt() == (super ? 1 : 19));
            CHECK(f.On(custom, "calls", "()I").AsInt() == 1);
        }
        f.context->content_view = image;
        ++f.context->ui_content_revision;
        f.On(image, "setImageAlpha", "(I)V", {VmValue::Int(128)});
        CHECK(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                       AndroidUiTraversalPhase::ResolveOnly, ui::Size{20, 18}));
        const auto snapshot = f.context->ui_tree.PublishedProjection();
        CHECK(snapshot->Get(node)->intrinsic == ui::Size{-1, -1});
        bool solid{};
        for (const auto& command : ui::BuildUiRenderList(*snapshot, {}))
            if (const auto* draw = std::get_if<ui::DrawSolidRect>(&command); draw && draw->rgba == 0x123456ff) {
                CHECK(draw->rect == ui::Rect{1, 2, 17, 14});
                CHECK(draw->alpha == doctest::Approx(128.0F / 255.0F));
                solid = true;
            }
        CHECK(solid);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-296 bitmap specs five raster targets immutable epochs versions and GC") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto image = Image(f, owner);
        auto image_root = f.vm.ProtectReferences(std::array{image});
        const auto config = f.Static("Landroid/graphics/Bitmap$Config;", "valueOf",
                                     "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;",
                                     {VmValue::Ref(f.vm.NewStringUtf8("ARGB_8888"))})
                                .ref;
        const auto bitmap = f.Static("Landroid/graphics/Bitmap;", "createBitmap",
                                     "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                                     {VmValue::Int(12), VmValue::Int(6), VmValue::Ref(config)})
                                .ref;
        PaintBitmap(f, bitmap, 0xffff0000U);
        f.On(image, "setImageBitmap", "(Landroid/graphics/Bitmap;)V", {VmValue::Ref(bitmap)});
        const auto drawable = f.On(image, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        CHECK(f.On(drawable, "getBitmap", "()Landroid/graphics/Bitmap;").ref == bitmap);
        f.On(image, "setPadding", "(IIII)V", {VmValue::Int(1), VmValue::Int(2), VmValue::Int(3), VmValue::Int(4)});
        f.On(image, "setClickable", "(Z)V", {VmValue::Int(1)});
        f.context->content_view = image;
        ++f.context->ui_content_revision;
        const auto node = *FindViewUiNode(*f.context, image.Value());
        const auto tick = [&] {
            return EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                            AndroidUiTraversalPhase::ResolveOnly, ui::Size{20, 18});
        };
        const std::array expected{ui::Rect{1, 2, 17, 14}, ui::Rect{1, 4, 17, 12}, ui::Rect{3, 5, 15, 11},
                                  ui::Rect{-2, 2, 22, 14}, ui::Rect{3, 5, 15, 11}};
        std::size_t index{};
        for (auto st : {1, 3, 5, 6, 7}) {
            f.On(image, "setScaleType", "(Landroid/widget/ImageView$ScaleType;)V", {VmValue::Ref(Scale(f, st))});
            for (auto mode : {0U, 0x40000000U, 0x80000000U}) {
                f.On(image, "requestLayout", "()V");
                f.On(image, "measure", "(II)V",
                     {VmValue::Int(static_cast<int>(mode | 10)), VmValue::Int(static_cast<int>(mode | 9))});
                CHECK(f.On(image, "getMeasuredWidth", "()I").AsInt() == (mode ? 10 : 16));
                CHECK(f.On(image, "getMeasuredHeight", "()I").AsInt() == (mode ? 9 : 12));
                CHECK(f.On(image, "getMeasuredState", "()I").cat1 == (mode == 0x80000000U ? 0x01000100U : 0U));
                const auto mw = f.On(image, "getMeasuredWidth", "()I").AsInt();
                const auto mh = f.On(image, "getMeasuredHeight", "()I").AsInt();
                f.On(image, "layout", "(IIII)V",
                     {VmValue::Int(0), VmValue::Int(0), VmValue::Int(mw), VmValue::Int(mh)});
                const auto epoch = f.context->ui_measure_epoch + 1;
                const ui::UiProjectionTarget target{f.context->ui_tree.Root(),
                                                    f.context->ui_tree.Generation(),
                                                    f.context->ui_content_revision,
                                                    epoch,
                                                    {mw, mh}};
                f.context->ui_tree.SetProjectionTarget(target);
                CommitAndroidMeasuredRootProjection(f.vm, *f.context, image, target, epoch);
                const auto measured_snapshot = f.context->ui_tree.PublishedProjection();
                const ui::Rect expected_spec = mode == 0 ? ui::Rect{1, 2, 13, 8}
                                               : st == 5 ? ui::Rect{-1, 1, 11, 7}
                                                         : ui::Rect{1, 2, 7, 5};
                std::size_t draws{};
                for (const auto& command : ui::BuildUiRenderList(*measured_snapshot, {}))
                    if (const auto* draw = std::get_if<ui::DrawBitmap>(&command)) {
                        CHECK(draw->rect == expected_spec);
                        ++draws;
                    }
                CHECK(draws == 1);
            }
            CHECK(tick());
            const auto snapshot = f.context->ui_tree.PublishedProjection();
            REQUIRE(snapshot);
            const auto commands = ui::BuildUiRenderList(*snapshot, {});
            std::size_t images{};
            for (const auto& command : commands)
                if (const auto* draw = std::get_if<ui::DrawBitmap>(&command)) {
                    CHECK(draw->rect == expected[index]);
                    CHECK(draw->bitmap == snapshot->Get(node)->image_bitmap);
                    ++images;
                }
            CHECK(images == 1);
            ++index;
        }
        const auto old = f.context->ui_tree.PublishedProjection();
        const auto old_pixels = old->Get(node)->image_bitmap;
        REQUIRE(old_pixels);
        CHECK(old_pixels->rgba8[0] == 255);
        PaintBitmap(f, bitmap, 0xff00ff00U);
        f.On(image, "setImageAlpha", "(I)V", {VmValue::Int(128)});
        CHECK(f.context->ui_tree.PublishedProjection() == old);
        // Pixel/alpha changes freeze a new render snapshot without resolving layout.
        CHECK_FALSE(tick());
        const auto next = f.context->ui_tree.PublishedProjection();
        CHECK(next->Get(node)->image_bitmap != old_pixels);
        CHECK(next->Get(node)->image_bitmap->rgba8[1] == 255);
        CHECK(old_pixels->rgba8[0] == 255);
        CHECK(old->Get(node)->image_alpha == 1.0F);
        CHECK(next->Get(node)->image_alpha == doctest::Approx(128.0F / 255.0F));
        f.context->ui_view_images.clear();  // Derived index is neither setter nor GC authority.
        static_cast<void>(f.vm.CollectGarbage("image-java-field"));
        CHECK(f.model.IsValidRef(drawable));
        CHECK(f.model.IsValidRef(bitmap));
        CHECK(f.On(image, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref == drawable);
        CHECK(tick());
        CHECK(f.context->ui_view_images.at(image.Value()) == drawable);
        f.On(bitmap, "recycle", "()V");
        bool rejected{};
        const auto before_recycle = f.context->ui_tree.PublishedProjection();
        try {
            static_cast<void>(tick());
        } catch (const VmJavaThrow&) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK(f.context->ui_tree.PublishedProjection() == before_recycle);
        f.On(image, "setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(VmObjectRef{})});
        static_cast<void>(f.vm.CollectGarbage("image-released-backing"));
        CHECK_FALSE(f.model.IsValidRef(drawable));
        CHECK_FALSE(f.context->ui_drawables.contains(drawable.Value()));
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-296 unsupported image APIs fail and ImageButton uses real default style") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto image = Image(f, owner);
        const auto root = f.vm.ProtectReferences(std::array{image});
        const auto before = f.On(image, "getScaleType", "()Landroid/widget/ImageView$ScaleType;").ref;
        CHECK(f.ledger.Unimplemented().empty());
        for (auto st : {0, 2, 4}) {
            CHECK(f.OnOutcome(image, "setScaleType", "(Landroid/widget/ImageView$ScaleType;)V",
                              {VmValue::Ref(Scale(f, st))})
                      .exception.IsValid());
            CHECK(f.On(image, "getScaleType", "()Landroid/widget/ImageView$ScaleType;").ref == before);
        }
        for (const auto& [name, sig] :
             std::array<std::pair<const char*, const char*>, 8>{{{"setAdjustViewBounds", "(Z)V"},
                                                                 {"setMaxWidth", "(I)V"},
                                                                 {"setMaxHeight", "(I)V"},
                                                                 {"setBaseline", "(I)V"},
                                                                 {"setBaselineAlignBottom", "(Z)V"},
                                                                 {"setCropToPadding", "(Z)V"},
                                                                 {"setColorFilter", "(I)V"},
                                                                 {"setImageLevel", "(I)V"}}})
            CHECK(f.OnOutcome(image, name, sig, {VmValue::Int(1)}).exception.IsValid());
        CHECK(f.OnOutcome(image, "setImageMatrix", "(Landroid/graphics/Matrix;)V", {VmValue::Ref(VmObjectRef{})})
                  .exception.IsValid());
        CHECK_FALSE(f.ledger.Unimplemented().empty());
        const auto button =
            f.New("Landroid/widget/ImageButton;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto button_root = f.vm.ProtectReferences(std::array{button});
        CHECK(f.On(button, "isFocusable", "()Z").AsInt() == 1);
        CHECK(f.On(button, "getScaleType", "()Landroid/widget/ImageView$ScaleType;").ref == before);
        const auto ctor =
            f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Landroid/widget/ImageButton;"), "<init>",
                                      "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
        REQUIRE(ctor);
        CHECK(f.linker.Class(f.linker.Method(*ctor).owner).is_boot_dex);
        // Use the real registered system default style, only masking unrelated scrollbar demands.
        f.context->arsc.entries.front().bag.pop_back();
        const auto default_owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, default_owner);
        f.On(default_owner, "setTheme", "(I)V", {VmValue::Int(0x7f020001)});
        const auto cls = f.linker.ResolveDescriptor("Landroid/widget/ImageButton;");
        const auto failed_button = f.vm.NewIntrinsicInstance("Landroid/widget/ImageButton;");
        const auto failed = f.vm.Call(*f.linker.FindDirectMethod(cls, "<init>", "(Landroid/content/Context;)V"),
                                      std::array{VmValue::Ref(failed_button), VmValue::Ref(default_owner)});
        INFO(failed.exception_message);
        CHECK(failed.exception.IsValid());
        CHECK(failed.exception_message.find("btn_default") != std::string::npos);
    }
}

TEST_CASE("DVM-296 Drawable density drives CENTER and backing follows Java GC") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_density = 2.0F;
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto image = Image(f, owner);
        VmObjectRef image_matrix;
        {
            const auto root = f.vm.ProtectReferences(std::array{image});
            const auto config = f.Static("Landroid/graphics/Bitmap$Config;", "valueOf",
                                         "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;",
                                         {VmValue::Ref(f.vm.NewStringUtf8("ARGB_8888"))})
                                    .ref;
            const auto bitmap = f.Static("Landroid/graphics/Bitmap;", "createBitmap",
                                         "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                                         {VmValue::Int(12), VmValue::Int(6), VmValue::Ref(config)})
                                    .ref;
            f.On(image, "setImageBitmap", "(Landroid/graphics/Bitmap;)V", {VmValue::Ref(bitmap)});
            const auto d = f.On(image, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
            CHECK(f.On(d, "getIntrinsicWidth", "()I").AsInt() == 24);
            CHECK(f.On(d, "getIntrinsicHeight", "()I").AsInt() == 12);
            f.On(image, "setScaleType", "(Landroid/widget/ImageView$ScaleType;)V", {VmValue::Ref(Scale(f, 5))});
            f.On(image, "setMinimumWidth", "(I)V", {VmValue::Int(30)});
            f.On(image, "setMinimumHeight", "(I)V", {VmValue::Int(20)});
            f.On(image, "measure", "(II)V", {VmValue::Int(0), VmValue::Int(0)});
            CHECK(f.On(image, "getMeasuredWidth", "()I").AsInt() == 30);
            CHECK(f.On(image, "getMeasuredHeight", "()I").AsInt() == 20);
            f.context->content_view = image;
            ++f.context->ui_content_revision;
            CHECK(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                           AndroidUiTraversalPhase::ResolveOnly, ui::Size{40, 30}));
            const auto snapshot = f.context->ui_tree.PublishedProjection();
            bool drawn{};
            for (const auto& command : ui::BuildUiRenderList(*snapshot, {}))
                if (const auto* draw = std::get_if<ui::DrawBitmap>(&command)) {
                    CHECK(draw->rect == ui::Rect{8, 9, 32, 21});
                    CHECK(draw->bitmap->width == 12);
                    CHECK(draw->bitmap->height == 6);
                    drawn = true;
                }
            CHECK(drawn);
            const auto matrix_field =
                f.linker.FindFieldRecursive(f.model.ObjectClass(image), "mMatrix", "Landroid/graphics/Matrix;");
            REQUIRE(matrix_field);
            const auto matrix = VmObjectRef{f.model.InstanceSlots(image)[f.linker.Field(*matrix_field).slot].bits};
            const auto values =
                f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 9);
            image_matrix = matrix;
            f.On(matrix, "getValues", "([F)V", {VmValue::Ref(values)});
            for (int i = 0; i < 9; ++i)
                CHECK(f.model.GetPrimitiveElement(values, i) == VmValue::Float(i % 4 == 0 ? 1.0F : 0.0F).cat1);
            CHECK(f.ledger.Unimplemented().empty());
            f.context->content_view = VmObjectRef{};
        }
        static_cast<void>(f.vm.CollectGarbage("image-owner-retired"));
        CHECK_FALSE(f.model.IsValidRef(image));
        // Finalizer sentinel allocations may reuse the retired View's raw ref.
        f.vm.RunFinalization();
        static_cast<void>(f.vm.CollectGarbage("image-finalized"));
        CHECK_FALSE(f.context->ui_view_images.contains(image.Value()));
        CHECK(f.context->ui_drawables.empty());
        CHECK_FALSE(f.model.IsValidRef(image_matrix));
        CHECK_FALSE(f.context->identity_matrices.contains(image_matrix.Value()));
        CHECK(f.ledger.Unimplemented().empty());
    }
}

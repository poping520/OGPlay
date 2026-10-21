#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/integration/host_image_decode.h"
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-118 Resources metrics share display facts and BootDex value semantics") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->surface_width = 960;
        f.context->surface_height = 540;
        f.context->ui_density = 1.5F;
        f.context->ui_scaled_density = 1.75F;
        const auto resources = f.vm.NewIntrinsicInstance("Landroid/content/res/Resources;");
        const auto root = f.vm.ProtectReferences(std::array{resources});
        const auto metrics = f.On(resources, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;").ref;
        const auto field = [&](VmObjectRef object, const char* name, const char* signature) {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, signature);
            REQUIRE(id.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*id).slot].bits;
        };
        CHECK(f.linker.Class(f.model.ObjectClass(metrics)).is_boot_dex);
        CHECK(field(metrics, "widthPixels", "I") == 960);
        CHECK(field(metrics, "heightPixels", "I") == 540);
        CHECK(field(metrics, "densityDpi", "I") == 240);
        CHECK(std::bit_cast<float>(field(metrics, "density", "F")) == 1.5F);
        CHECK(std::bit_cast<float>(field(metrics, "scaledDensity", "F")) == 1.75F);
        CHECK(field(metrics, "noncompatDensity", "F") == field(metrics, "density", "F"));
        static_cast<void>(f.vm.CollectGarbage("dvm118-resources-metrics"));
        CHECK(f.On(resources, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;").ref == metrics);
        const auto other = f.New("Landroid/util/DisplayMetrics;");
        const auto other_root = f.vm.ProtectReferences(std::array{other});
        CHECK(field(other, "widthPixels", "I") == 0);
        f.On(other, "setToDefaults", "()V");
        CHECK(field(other, "densityDpi", "I") == 240);
        CHECK(std::bit_cast<float>(field(other, "scaledDensity", "F")) == 1.5F);
        const auto display = f.vm.NewIntrinsicInstance("Landroid/view/Display;");
        f.On(display, "getMetrics", "(Landroid/util/DisplayMetrics;)V", {VmValue::Ref(other)});
        CHECK(f.On(metrics, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(other)}).AsInt() == 1);
        CHECK(f.On(metrics, "hashCode", "()I").AsInt() == f.On(other, "hashCode", "()I").AsInt());
        f.context->surface_width = 1280;
        CHECK(f.On(resources, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;").ref == metrics);
        CHECK(field(metrics, "widthPixels", "I") == 1280);
        CHECK(field(other, "widthPixels", "I") == 960);
        f.On(other, "setTo", "(Landroid/util/DisplayMetrics;)V", {VmValue::Ref(metrics)});
        CHECK(field(other, "widthPixels", "I") == 1280);
        const auto apply = [&](int unit, float value, VmObjectRef target) {
            return f.Static("Landroid/util/TypedValue;", "applyDimension",
                "(IFLandroid/util/DisplayMetrics;)F",
                {VmValue::Int(unit), VmValue::Float(value), VmValue::Ref(target)}).AsFloat();
        };
        CHECK(apply(0, 5.0F, VmObjectRef{}) == 5.0F);
        CHECK(apply(1, 5.0F, metrics) == 7.5F);
        CHECK(apply(2, 5.0F, metrics) == 8.75F);
        CHECK(apply(3, 72.0F, metrics) == doctest::Approx(240.0F));
        CHECK(apply(4, 1.0F, metrics) == 240.0F);
        CHECK(apply(5, 25.4F, metrics) == doctest::Approx(240.0F));
        CHECK(apply(99, 5.0F, metrics) == 0.0F);
        CHECK(f.Static("Landroid/util/TypedValue;", "complexToDimensionPixelSize",
                        "(ILandroid/util/DisplayMetrics;)I",
                        {VmValue::Int(0x501), VmValue::Ref(metrics)}).AsInt() == 8);
        CHECK(f.vm.StringUtf8(f.Static("Landroid/util/TypedValue;", "coerceToString",
                "(II)Ljava/lang/String;", {VmValue::Int(18), VmValue::Int(1)}).ref) == "true");
        const auto bad = f.OnOutcome(display, "getRealMetrics", "(Landroid/util/DisplayMetrics;)V",
                                     {VmValue::Ref(VmObjectRef{})});
        REQUIRE(bad.exception.IsValid());
        CHECK(f.linker.Class(bad.exception_class).descriptor == "Ljava/lang/NullPointerException;");
    }
}

TEST_CASE("DVM-137 Configuration uses API19 Java shape and managed device facts") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, nullptr, true);
        f.context->surface_width = 960;
        f.context->surface_height = 540;
        f.context->ui_density = 1.5F;
        const auto resources =
            f.vm.NewIntrinsicInstance("Landroid/content/res/Resources;");
        const auto resources_root = f.vm.ProtectReferences(std::array{resources});
        const auto field = [&](const VmObjectRef object, const char* name,
                               const char* descriptor = "I") {
            const auto id = f.linker.FindFieldRecursive(
                f.model.ObjectClass(object), name, descriptor);
            REQUIRE(id.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*id).slot].bits;
        };
        const auto configuration = f.On(
            resources, "getConfiguration",
            "()Landroid/content/res/Configuration;").ref;
        const auto configuration_root =
            f.vm.ProtectReferences(std::array{configuration});
        const auto owner = f.model.ObjectClass(configuration);
        CHECK(f.linker.Class(owner).is_boot_dex);
        CHECK(field(configuration, "touchscreen") == 3);
        CHECK(field(configuration, "keyboard") == 2);
        CHECK(field(configuration, "keyboardHidden") == 1);
        CHECK(field(configuration, "hardKeyboardHidden") == 1);
        CHECK(field(configuration, "navigation") == 1);
        CHECK(field(configuration, "navigationHidden") == 2);
        CHECK(field(configuration, "orientation") == 2);
        CHECK(field(configuration, "screenWidthDp") == 640);
        CHECK(field(configuration, "screenHeightDp") == 360);
        CHECK(field(configuration, "smallestScreenWidthDp") == 360);
        CHECK(field(configuration, "densityDpi") == 240);
        CHECK(field(configuration, "screenLayout") == 0x10000062);
        const auto default_locale = f.Static(
            "Ljava/util/Locale;", "getDefault", "()Ljava/util/Locale;").ref;
        REQUIRE(default_locale.IsValid());
        CHECK(f.Static("Landroid/text/TextUtils;",
                       "getLayoutDirectionFromLocale",
                       "(Ljava/util/Locale;)I",
                       {VmValue::Ref(default_locale)}).AsInt() == 0);
        const auto unsupported_locale = f.New(
            "Ljava/util/Locale;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("fr"))});
        const auto unsupported_direction = f.StaticOutcome(
            "Landroid/text/TextUtils;", "getLayoutDirectionFromLocale",
            "(Ljava/util/Locale;)I",
            {VmValue::Ref(unsupported_locale)});
        REQUIRE(unsupported_direction.exception.IsValid());
        CHECK(f.linker.Class(unsupported_direction.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        REQUIRE_FALSE(f.ledger.Unimplemented().empty());
        CHECK(f.ledger.Unimplemented().back().id ==
              "dexvm.locale_layout_direction");
        CHECK_MESSAGE(VmObjectRef(field(configuration, "locale",
                                       "Ljava/util/Locale;")) == default_locale,
                      "configuration locale=",
                      field(configuration, "locale", "Ljava/util/Locale;"),
                      " default locale=", default_locale.Value());
        const auto rendered = f.vm.StringUtf8(
            f.On(configuration, "toString", "()Ljava/lang/String;").ref);
        CHECK(f.vm.StringUtf8(f.On(default_locale, "toString",
                                  "()Ljava/lang/String;").ref) == "en_US");
        CHECK(rendered.find("ldltr") != std::string::npos);

        const auto defaults = f.New("Landroid/content/res/Configuration;");
        const auto defaults_root = f.vm.ProtectReferences(std::array{defaults});
        CHECK(std::bit_cast<float>(field(defaults, "fontScale", "F")) == 1.0F);
        CHECK(field(defaults, "keyboard") == 0);
        CHECK(field(defaults, "orientation") == 0);
        const auto copy = f.New(
            "Landroid/content/res/Configuration;",
            "(Landroid/content/res/Configuration;)V",
            {VmValue::Ref(configuration)});
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        CHECK(f.On(copy, "equals", "(Landroid/content/res/Configuration;)Z",
                   {VmValue::Ref(configuration)}).AsInt() == 1);
        CHECK(f.vm.StringUtf8(
                  f.On(copy, "toString", "()Ljava/lang/String;").ref)
                  .find("land") != std::string::npos);

        const auto parcel = f.Static(
            "Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto parcel_root = f.vm.ProtectReferences(std::array{parcel});
        f.On(configuration, "writeToParcel", "(Landroid/os/Parcel;I)V",
             {VmValue::Ref(parcel), VmValue::Int(0)});
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto creator_id = f.linker.FindFieldRecursive(
            owner, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(creator_id.has_value());
        const auto creator = VmObjectRef(
            f.linker.Class(owner)
                .static_storage[f.linker.Field(*creator_id).slot]);
        REQUIRE(creator.IsValid());
        const auto restored = f.On(
            creator, "createFromParcel",
            "(Landroid/os/Parcel;)Ljava/lang/Object;",
            {VmValue::Ref(parcel)}).ref;
        CHECK(f.On(restored, "equals",
                   "(Landroid/content/res/Configuration;)Z",
                   {VmValue::Ref(configuration)}).AsInt() == 1);

        static_cast<void>(f.vm.CollectGarbage("dvm137-configuration"));
        CHECK(f.On(resources, "getConfiguration",
                   "()Landroid/content/res/Configuration;").ref == configuration);
        CHECK(VmObjectRef(field(configuration, "locale",
                                "Ljava/util/Locale;")) == default_locale);
        f.context->surface_width = 540;
        f.context->surface_height = 960;
        CHECK(f.On(resources, "getConfiguration",
                   "()Landroid/content/res/Configuration;").ref == configuration);
        CHECK(field(configuration, "orientation") == 1);
        CHECK(field(configuration, "screenWidthDp") == 360);
        CHECK(field(configuration, "screenHeightDp") == 640);
    }
}

TEST_CASE("DVM-244 bitmap resource decoding shares ARSC selection pixels and density") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->ui_density = 2;
        f.context->apk_bytes = ogplay::test::BitmapDecodeApk();
        f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
        f.context->arsc.entries = {
            {.resource_id=0x7f020001, .type_name="drawable", .entry_name="fixture", .string_value="res/drawable/fixture.png", .value_type=3, .density=160},
            {.resource_id=0x7f020002, .type_name="drawable", .entry_name="alias", .value_type=1, .value_data=0x7f020001},
            {.resource_id=0x7f020003, .type_name="drawable", .entry_name="nodpi", .string_value="res/drawable/fixture.png", .value_type=3, .density=65535}};
        const auto res = f.vm.NewIntrinsicInstance("Landroid/content/res/Resources;");
        const auto roots = f.vm.ProtectReferences(std::array{res});
        const auto query = [&](int id, VmObjectRef opts = VmObjectRef{}) {
            return f.StaticOutcome("Landroid/graphics/BitmapFactory;", "decodeResource",
                "(Landroid/content/res/Resources;ILandroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;",
                {VmValue::Ref(res), VmValue::Int(id), VmValue::Ref(opts)});
        };
        const auto field = [&](VmObjectRef object, const char* name, const char* descriptor) -> Slot& {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, descriptor);
            REQUIRE(id.has_value());
            auto& slot = f.model.InstanceSlots(object)[f.linker.Field(*id).slot];
            // Direct fixture writes must carry the same scalar tag as iput.
            if (descriptor[0]=='I' || descriptor[0]=='Z') slot.tag=SlotTag::cat1;
            return slot;
        };
        const auto first = f.Static("Landroid/graphics/BitmapFactory;", "decodeResource",
            "(Landroid/content/res/Resources;I)Landroid/graphics/Bitmap;", {VmValue::Ref(res), VmValue::Int(0x7f020002)}).ref;
        const auto bitmap_root = f.vm.ProtectReferences(std::array{first});
        REQUIRE(first.IsValid()); CHECK(f.On(first,"getWidth","()I").AsInt()==4);
        CHECK(f.On(first,"getHeight","()I").AsInt()==2); CHECK(f.On(first,"getDensity","()I").AsInt()==320);
        CHECK_FALSE(f.On(first,"isMutable","()Z").AsInt());
        const auto opts = f.New("Landroid/graphics/BitmapFactory$Options;");
        const auto opts_root = f.vm.ProtectReferences(std::array{opts});
        field(opts,"inJustDecodeBounds","Z").bits=1;
        const auto bounds = query(0x7f020001, opts);
        REQUIRE_FALSE(bounds.exception.IsValid()); CHECK_FALSE(bounds.value.ref.IsValid());
        CHECK(field(opts,"outWidth","I").bits==2); CHECK(field(opts,"outHeight","I").bits==1);
        CHECK(field(opts,"inDensity","I").bits==160); CHECK(field(opts,"inTargetDensity","I").bits==320);
        CHECK(f.vm.StringUtf8(VmObjectRef{field(opts,"outMimeType","Ljava/lang/String;").bits})=="image/png");
        field(opts,"inJustDecodeBounds","Z").bits=0; field(opts,"inScaled","Z").bits=0;
        field(opts,"inMutable","Z").bits=1;
        const auto unscaled = query(0x7f020001, opts);
        REQUIRE_FALSE(unscaled.exception.IsValid()); REQUIRE(unscaled.value.ref.IsValid());
        const auto unscaled_root = f.vm.ProtectReferences(std::array{unscaled.value.ref});
        CHECK(f.On(unscaled.value.ref,"getWidth","()I").AsInt()==2);
        CHECK(f.On(unscaled.value.ref,"getDensity","()I").AsInt()==160);
        CHECK(f.On(unscaled.value.ref,"isMutable","()Z").AsInt()==1);
        const auto colors=f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"),JniPrimitiveKind::integer,2);
        const auto colors_root=f.vm.ProtectReferences(std::array{colors});
        f.model.SetPrimitiveElement(colors,0,0xff00ff00); f.model.SetPrimitiveElement(colors,1,0xffff0000);
        f.On(unscaled.value.ref,"setPixels","([IIIIIII)V",{VmValue::Ref(colors),VmValue::Int(0),VmValue::Int(2),VmValue::Int(0),VmValue::Int(0),VmValue::Int(2),VmValue::Int(1)});
        CHECK(f.context->bitmaps.at(unscaled.value.ref.Value()).pixels->Get(0)==0xff00ff00);
        CHECK(f.context->bitmaps.at(first.Value()).pixels->Get(0)==0xffff0000);
        const auto immutable=f.OnOutcome(first,"setPixels","([IIIIIII)V",{VmValue::Ref(colors),VmValue::Int(0),VmValue::Int(2),VmValue::Int(0),VmValue::Int(0),VmValue::Int(2),VmValue::Int(1)});
        REQUIRE(immutable.exception.IsValid()); CHECK(f.linker.Class(immutable.exception_class).descriptor=="Ljava/lang/IllegalStateException;");
        const auto nodpi = query(0x7f020003);
        REQUIRE_FALSE(nodpi.exception.IsValid()); REQUIRE(nodpi.value.ref.IsValid());
        const auto nodpi_root = f.vm.ProtectReferences(std::array{nodpi.value.ref});
        CHECK(f.On(nodpi.value.ref,"getWidth","()I").AsInt()==2);
        CHECK(f.On(nodpi.value.ref,"getDensity","()I").AsInt()==320);
        const auto again = query(0x7f020001);
        REQUIRE_FALSE(again.exception.IsValid()); CHECK(again.value.ref != first);
        CHECK(f.context->bitmaps.at(again.value.ref.Value()).pixels != f.context->bitmaps.at(first.Value()).pixels);
        const auto bad = query(0x7f020099); REQUIRE_FALSE(bad.exception.IsValid()); CHECK_FALSE(bad.value.ref.IsValid());
        field(opts,"inSampleSize","I").bits=2;
        const auto unsupported = query(0x7f020001,opts); REQUIRE(unsupported.exception.IsValid());
        CHECK(f.linker.Class(unsupported.exception_class).descriptor=="Ljava/lang/UnsupportedOperationException;");
        field(opts,"inSampleSize","I").bits=0;
        f.On(opts,"requestCancelDecode","()V");
        const auto cancelled = query(0x7f020001,opts); REQUIRE_FALSE(cancelled.exception.IsValid()); CHECK_FALSE(cancelled.value.ref.IsValid());
        CHECK(field(opts,"outWidth","I").bits==UINT32_MAX);
        static_cast<void>(f.vm.CollectGarbage());
        CHECK(f.On(first,"getWidth","()I").AsInt()==4);
    }
}

TEST_CASE("DVM-244 byte stream decode preserves null errors ownership and pixel budgets") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        int closed{}; bool sent{}; VmObjectRef resource_source;
        auto stream = IntrinsicClassBuilder::Class("Ltest/ImageStream;","Ljava/io/InputStream;");
        stream.Constructor("()V",[](IntrinsicContext&) {return VmValue::Void();});
        stream.OverrideMethod("read","([BII)I",[&](IntrinsicContext& call) {
            if(sent) return VmValue::Int(-1);
            sent=true; const auto bytes=ogplay::test::BitmapDecodePng();
            call.vm.Model().WriteByteRegion(call.arguments[0].ref,0,bytes);
            return VmValue::Int(static_cast<int>(bytes.size()));
        });
        stream.OverrideMethod("close","()V",[&](IntrinsicContext&) {++closed;return VmValue::Void();});
        auto resources = IntrinsicClassBuilder::Class("Ltest/ImageResources;", "Landroid/content/res/Resources;");
        resources.OverrideMethod("openRawResource", "(ILandroid/util/TypedValue;)Ljava/io/InputStream;",
            [&](IntrinsicContext&) { sent = false; return VmValue::Ref(resource_source); });
        AndroidValueVm f(backend,{std::move(stream).Build(), std::move(resources).Build()});
        const auto source=f.New("Ltest/ImageStream;");
        const auto source_root=f.vm.ProtectReferences(std::array{source});
        const auto bitmap=f.Static("Landroid/graphics/BitmapFactory;","decodeStream","(Ljava/io/InputStream;)Landroid/graphics/Bitmap;",{VmValue::Ref(source)}).ref;
        const auto bitmap_root=f.vm.ProtectReferences(std::array{bitmap});
        REQUIRE(bitmap.IsValid()); CHECK(closed==0); CHECK(f.On(bitmap,"getWidth","()I").AsInt()==2);
        resource_source = source;
        const auto res = f.vm.NewIntrinsicInstance("Ltest/ImageResources;");
        const auto res_root = f.vm.ProtectReferences(std::array{res});
        const auto resource_bitmap = f.Static("Landroid/graphics/BitmapFactory;", "decodeResource",
            "(Landroid/content/res/Resources;I)Landroid/graphics/Bitmap;", {VmValue::Ref(res), VmValue::Int(1)}).ref;
        REQUIRE(resource_bitmap.IsValid()); CHECK(closed==1);
        CHECK_FALSE(f.Static("Landroid/graphics/BitmapFactory;","decodeStream","(Ljava/io/InputStream;)Landroid/graphics/Bitmap;",{VmValue::Ref(VmObjectRef{})}).ref.IsValid());
        const auto bytes=ogplay::test::BitmapDecodePng();
        std::string payload; for(auto b:bytes) payload.push_back(static_cast<char>(std::to_integer<unsigned>(b)));
        const auto array=f.Bytes(payload); const auto array_root=f.vm.ProtectReferences(std::array{array});
        const auto decoded=f.Static("Landroid/graphics/BitmapFactory;","decodeByteArray","([BII)Landroid/graphics/Bitmap;",{VmValue::Ref(array),VmValue::Int(0),VmValue::Int(static_cast<int>(bytes.size()))}).ref;
        const auto decoded_root=f.vm.ProtectReferences(std::array{decoded});
        REQUIRE(decoded.IsValid()); CHECK(f.On(decoded,"getWidth","()I").AsInt()==2);
        const auto bad=f.StaticOutcome("Landroid/graphics/BitmapFactory;","decodeByteArray","([BII)Landroid/graphics/Bitmap;",{VmValue::Ref(array),VmValue::Int(-1),VmValue::Int(1)});
        REQUIRE(bad.exception.IsValid()); CHECK(f.linker.Class(bad.exception_class).descriptor=="Ljava/lang/ArrayIndexOutOfBoundsException;");
        const auto null=f.StaticOutcome("Landroid/graphics/BitmapFactory;","decodeByteArray","([BII)Landroid/graphics/Bitmap;",{VmValue::Ref(VmObjectRef{}),VmValue::Int(0),VmValue::Int(1)});
        REQUIRE(null.exception.IsValid()); CHECK(f.linker.Class(null.exception_class).descriptor=="Ljava/lang/NullPointerException;");
        CHECK_FALSE(f.Static("Landroid/graphics/BitmapFactory;","decodeByteArray","([BII)Landroid/graphics/Bitmap;",{VmValue::Ref(f.Bytes("bad")),VmValue::Int(0),VmValue::Int(3)}).ref.IsValid());
        payload[16]=0;payload[17]=0;payload[18]=0x20;payload[19]=1; // IHDR width 8193, rejected before pixel allocation
        const auto huge=f.StaticOutcome("Landroid/graphics/BitmapFactory;","decodeByteArray","([BII)Landroid/graphics/Bitmap;",{VmValue::Ref(f.Bytes(payload)),VmValue::Int(0),VmValue::Int(static_cast<int>(payload.size()))});
        REQUIRE(huge.exception.IsValid()); CHECK(f.linker.Class(huge.exception_class).descriptor=="Ljava/lang/OutOfMemoryError;");
        const auto info=ogplay::runtime::ProbeImage(bytes); REQUIRE(info.has_value()); CHECK(info->width==2);
        CHECK_FALSE(ogplay::runtime::ImageDimensionsSupported(8193,1)); CHECK_FALSE(ogplay::runtime::ImageDimensionsSupported(8192,8192));
        const auto image=ogplay::runtime::DecodeImageToArgb(bytes); REQUIRE(image.has_value());
        const auto scaled=ogplay::runtime::ResizeArgbImage(*image,4,1);
        CHECK(scaled.argb.front()==0xffff0000); CHECK(scaled.argb.back()==0xff0000ff);
        CHECK(scaled.argb[1]==0xffbf0040); CHECK(scaled.argb[2]==0xff4000bf);
    }
}

TEST_CASE("DVM-86 graphics value classes keep geometry and path state") {
    AndroidValueVm fixture;
    CHECK(fixture.Static("Landroid/graphics/Color;", "parseColor",
                         "(Ljava/lang/String;)I",
                         {VmValue::Ref(fixture.vm.NewStringUtf8("#112233"))})
              .AsInt() == static_cast<std::int32_t>(0xff112233U));
    const auto rect = fixture.New("Landroid/graphics/RectF;", "(FFFF)V",
        {VmValue::Float(1.0F), VmValue::Float(2.0F),
         VmValue::Float(6.0F), VmValue::Float(9.0F)});
    CHECK(fixture.On(rect, "width", "()F").AsFloat() == doctest::Approx(5.0F));
    CHECK(fixture.On(rect, "contains", "(FF)Z",
                     {VmValue::Float(3.0F), VmValue::Float(4.0F)}).AsInt() == 1);

    const auto path = fixture.New("Landroid/graphics/Path;");
    CHECK(fixture.On(path, "isEmpty", "()Z").AsInt() == 1);
    fixture.On(path, "moveTo", "(FF)V", {VmValue::Float(1), VmValue::Float(2)});
    fixture.On(path, "lineTo", "(FF)V", {VmValue::Float(3), VmValue::Float(4)});
    CHECK(fixture.context->paths.at(path.Value()).commands.size() == 2U);
    CHECK(fixture.On(path, "isEmpty", "()Z").AsInt() == 0);
}

TEST_CASE("Bitmap Config matches the API 19 enum and native mapping") {
    AndroidValueVm fixture;
    constexpr auto descriptor = "Landroid/graphics/Bitmap$Config;";
    const auto config_class = fixture.linker.ResolveDescriptor(descriptor);
    const auto enum_class = fixture.linker.ResolveDescriptor("Ljava/lang/Enum;");
    REQUIRE(fixture.linker.Class(config_class).super.has_value());
    CHECK(*fixture.linker.Class(config_class).super == enum_class);

    const auto initialized = fixture.vm.EnsureClassInitialized(config_class);
    REQUIRE_MESSAGE(!initialized.exception.IsValid(),
                    initialized.exception_message);
    const auto constant = [&](const char* name) {
        const auto field = fixture.linker.FindFieldRecursive(
            config_class, name, descriptor);
        REQUIRE(field.has_value());
        const auto& linked = fixture.linker.Field(*field);
        CHECK((linked.access_flags & 0x4019U) == 0x4019U);
        return VmObjectRef(
            fixture.linker.Class(linked.owner).static_storage[linked.slot]);
    };
    const std::array constants{constant("ALPHA_8"), constant("RGB_565"),
                               constant("ARGB_4444"),
                               constant("ARGB_8888")};
    for (const auto value : constants) CHECK(value.IsValid());

    const auto native_int = fixture.linker.FindFieldRecursive(
        config_class, "nativeInt", "I");
    const auto name_field = fixture.linker.FindFieldRecursive(
        config_class, "name", "Ljava/lang/String;");
    const auto ordinal = fixture.linker.FindFieldRecursive(
        config_class, "ordinal", "I");
    REQUIRE(native_int.has_value());
    REQUIRE(name_field.has_value());
    REQUIRE(ordinal.has_value());
    const std::array native_values{1U, 3U, 4U, 5U};
    const std::array names{"ALPHA_8", "RGB_565", "ARGB_4444", "ARGB_8888"};
    for (std::size_t index = 0; index < constants.size(); ++index) {
        const auto slots = fixture.model.InstanceSlots(constants[index]);
        CHECK(slots[fixture.linker.Field(*native_int).slot].bits ==
              native_values[index]);
        CHECK(slots[fixture.linker.Field(*ordinal).slot].bits == index);
        CHECK(fixture.vm.StringUtf8(VmObjectRef(
                  slots[fixture.linker.Field(*name_field).slot].bits)) ==
              names[index]);
    }

    const std::array<VmObjectRef, 6> native_mapping{
        VmObjectRef{}, constants[0], VmObjectRef{}, constants[1],
        constants[2], constants[3]};
    for (std::int32_t index = 0; index < 6; ++index) {
        CHECK(fixture.Static(descriptor, "nativeToConfig",
                             "(I)Landroid/graphics/Bitmap$Config;",
                             {VmValue::Int(index)})
                  .ref == native_mapping[static_cast<std::size_t>(index)]);
    }
    const auto by_name = fixture.Static(
        descriptor, "valueOf",
        "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("ARGB_8888"))});
    CHECK(by_name.ref == constants[3]);

    const auto first_values = fixture.Static(
        descriptor, "values", "()[Landroid/graphics/Bitmap$Config;").ref;
    const auto second_values = fixture.Static(
        descriptor, "values", "()[Landroid/graphics/Bitmap$Config;").ref;
    CHECK(first_values != second_values);
    CHECK(fixture.model.ArrayLength(first_values) == 4);
    for (JniSize index = 0; index < 4; ++index) {
        CHECK(fixture.model.GetObjectElement(first_values, index) ==
              constants[static_cast<std::size_t>(index)]);
    }
    CHECK(fixture.linker.FindFieldRecursive(
              config_class, "$VALUES",
              "[Landroid/graphics/Bitmap$Config;").has_value());
    CHECK(fixture.linker.FindFieldRecursive(
              config_class, "sConfigs",
              "[Landroid/graphics/Bitmap$Config;").has_value());
    CHECK(fixture.linker.FindDirectMethod(
              config_class, "<init>", "(Ljava/lang/String;II)V").has_value());

    const auto bitmap = fixture.Static(
        "Landroid/graphics/Bitmap;", "createBitmap",
        "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
        {VmValue::Int(2), VmValue::Int(2), VmValue::Ref(constants[1])}).ref;
    REQUIRE(bitmap.IsValid());
    REQUIRE(fixture.context->bitmaps.contains(bitmap.Value()));
    CHECK(fixture.context->bitmaps.at(bitmap.Value()).config == 3);
    CHECK(fixture.context->bitmaps.at(bitmap.Value()).pixels->Snapshot() ==
          std::vector<std::uint32_t>(4, 0U));
    const auto pixels = fixture.model.NewPrimitiveArray(
        fixture.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 4);
    const std::array<std::uint32_t, 4> colors{
        0xff112233U, 0xff445566U, 0xff778899U, 0xffaabbccU};
    for (JniSize index = 0; index < 4; ++index) {
        fixture.model.SetPrimitiveElement(pixels, index, colors[static_cast<std::size_t>(index)]);
    }
    fixture.On(bitmap, "setPixels", "([IIIIIII)V",
               {VmValue::Ref(pixels), VmValue::Int(0), VmValue::Int(2),
                VmValue::Int(0), VmValue::Int(0), VmValue::Int(2),
                VmValue::Int(2)});
    CHECK(fixture.context->bitmaps.at(bitmap.Value()).pixels->Snapshot() ==
          std::vector<std::uint32_t>(colors.begin(), colors.end()));

    const auto canvas = fixture.vm.NewIntrinsicInstance(
        "Landroid/graphics/Canvas;");
    fixture.context->canvases.emplace(
        canvas.Value(),
        DexVmAndroidContext::CanvasState{
            1U, 3U, 2U, std::vector<std::uint32_t>(6, 0xff000000U), true});
    fixture.On(canvas, "drawBitmap",
               "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V",
               {VmValue::Ref(bitmap), VmValue::Float(1.0F),
                VmValue::Float(0.0F), VmValue::Ref(VmObjectRef{0})});
    CHECK(fixture.context->canvases.at(canvas.Value()).argb ==
          std::vector<std::uint32_t>{
              0xff000000U, colors[0], colors[1],
              0xff000000U, colors[2], colors[3]});
}

TEST_CASE("DVM118 resource text coerces simple values and preserves reference selection errors") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->surface_width = 800;
        f.context->surface_height = 480;
        const auto add = [&](std::uint32_t id, std::uint8_t type, std::uint32_t data,
                             std::optional<std::string> text = {}) {
            ogplay::loader::ArscEntry entry{};
            entry.resource_id = id;
            entry.value_type = type;
            entry.value_data = data;
            entry.string_value = std::move(text);
            f.context->arsc.entries.push_back(std::move(entry));
        };
        struct Value { std::uint8_t type; std::uint32_t data; const char* expected; };
        const std::array values{
            Value{0x12, 0, "false"}, Value{0x12, 0xffffffffU, "true"},
            Value{0x10, 30, "30"}, Value{0x10, 0xfffffff9U, "-7"},
            Value{0x11, 0x80000000U, "0x80000000"}, Value{0x1c, 0xff010203U, "#ff010203"},
            Value{0x04, std::bit_cast<std::uint32_t>(3.25F), "3.25"},
            Value{0x05, 0x501, "5.0dip"}, Value{0x06, 0x40000030, "50.0%"},
            Value{0x03, 0, "hello"}};
        for (std::size_t i = 0; i < values.size(); ++i)
            add(0x7f010000U + static_cast<std::uint32_t>(i), values[i].type, values[i].data,
                values[i].type == 3 ? std::optional<std::string>{"hello"} : std::nullopt);
        add(0x7f010080, 1, 0x7f010081);
        add(0x7f010081, 1, 0x7f010002);
        for (const auto [orientation, text] : {std::pair{0, "default"}, {1, "portrait"}, {2, "landscape"}}) {
            add(0x7f010090, 3, 0, text);
            f.context->arsc.entries.back().orientation = static_cast<std::uint8_t>(orientation);
        }
        add(0x7f010091, 1, 0x7f010090);
        add(0x7f010100, 0, 0);
        add(0x7f010101, 0x0f, 0);
        add(0x7f010102, 3, 0);
        add(0x7f010103, 3, 0, "bag");
        f.context->arsc.entries.back().is_complex = true;
        add(0x7f010104, 1, 0);
        add(0x7f010105, 1, 0x7f010106);
        add(0x7f010106, 1, 0x7f010105);
        const auto context = f.vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto resources = f.On(context, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{context, resources});
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto id = VmValue::Int(static_cast<std::int32_t>(0x7f010000U + i));
            for (const auto [receiver, method, signature] : {
                std::tuple{resources, "getText", "(I)Ljava/lang/CharSequence;"},
                std::tuple{resources, "getString", "(I)Ljava/lang/String;"},
                std::tuple{context, "getString", "(I)Ljava/lang/String;"}}) {
                const auto text = f.On(receiver, method, signature, {id}).ref;
                CHECK(f.vm.StringUtf8(text) == values[i].expected);
            }
        }
        CHECK(f.vm.StringUtf8(f.On(resources, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0x7f010080)}).ref) == "30");
        CHECK(f.vm.StringUtf8(f.On(resources, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0x7f010091)}).ref) == "landscape");
        f.context->surface_width = 480;
        f.context->surface_height = 800;
        CHECK(f.vm.StringUtf8(f.On(resources, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0x7f010091)}).ref) == "portrait");
        const auto not_found = f.linker.ResolveDescriptor("Landroid/content/res/Resources$NotFoundException;");
        for (const auto id : {0, 0x7f01ffff, 0x7f010100, 0x7f010101, 0x7f010102, 0x7f010103, 0x7f010104, 0x7f010105}) {
            for (const auto [method, signature] : {
                 std::pair{"getText", "(I)Ljava/lang/CharSequence;"}, {"getString", "(I)Ljava/lang/String;"}}) {
                const auto result = f.OnOutcome(resources, method, signature, {VmValue::Int(id)});
                REQUIRE(result.exception.IsValid());
                CHECK(result.exception_class == not_found);
            }
        }
    }
}

TEST_CASE("DVM118 getString dispatches getText and CharSequence toString through Context") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto text = std::make_shared<VmObjectRef>();
        auto resources = std::make_shared<VmObjectRef>();
        auto custom = IntrinsicClassBuilder::Class("Ltest/TextResources;", "Landroid/content/res/Resources;");
        custom.OverrideMethod("getText", "(I)Ljava/lang/CharSequence;",
            [text](IntrinsicContext&) { return VmValue::Ref(*text); });
        auto owner = IntrinsicClassBuilder::Class("Ltest/TextContext;", "Landroid/content/Context;");
        owner.OverrideMethod("getResources", "()Landroid/content/res/Resources;",
            [resources](IntrinsicContext&) { return VmValue::Ref(*resources); });
        AndroidValueVm f(backend, {std::move(custom).Build(), std::move(owner).Build()});
        *text = f.New("Ljava/lang/StringBuilder;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("overridden"))});
        *resources = f.vm.NewIntrinsicInstance("Ltest/TextResources;");
        const auto context = f.vm.NewIntrinsicInstance("Ltest/TextContext;");
        const auto roots = f.vm.ProtectReferences(std::array{*text, *resources, context});
        CHECK(f.On(*resources, "getText", "(I)Ljava/lang/CharSequence;", {VmValue::Int(0)}).ref == *text);
        CHECK(f.vm.StringUtf8(f.On(*resources, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref) == "overridden");
        CHECK(f.vm.StringUtf8(f.On(context, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref) == "overridden");
        *text = VmObjectRef{};
        const auto failure = f.OnOutcome(*resources, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0)});
        REQUIRE(failure.exception.IsValid());
        CHECK(f.linker.Class(failure.exception_class).descriptor == "Landroid/content/res/Resources$NotFoundException;");
    }
}

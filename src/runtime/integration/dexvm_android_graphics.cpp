// Bitmap/Canvas and widget handlers. Bitmaps hold real ARGB pixels
// (host decode for compressed payloads); widgets hold real state but
// never draw, since the GL surface is the only visual output.

#include "ogplay/runtime/integration/host_image_decode.h"

#include "dexvm_android_internal.h"

namespace ogplay::runtime::android_intrinsics {

[[nodiscard]] DexVmAndroidContext::BitmapState& BitmapOf(
    dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->bitmaps.find(call.receiver.Value());
    if (found == context->bitmaps.end() || found->second.recycled) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "bitmap is recycled or was never created"};
    }
    return found->second;
}

// Builds a Bitmap intrinsic instance from pixels copied out of a guest int
// array (offset/stride window per the createBitmap contract).
[[nodiscard]] dx::VmValue MakeBitmapFromArray(
    dx::IntrinsicContext& call, const Context& context,
    const dx::VmObjectRef array, const std::int32_t offset,
    const std::int32_t stride, const std::int32_t width,
    const std::int32_t height) {
    auto& model = call.vm.Model();
    if (!array.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "createBitmap colors array is null"};
    }
    if (width <= 0 || height <= 0 || stride < width) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalArgumentException;",
            "createBitmap dimensions are invalid: " + std::to_string(width) +
                "x" + std::to_string(height) + " stride " +
                std::to_string(stride)};
    }
    const auto length = static_cast<std::int64_t>(model.ArrayLength(array));
    const auto last = static_cast<std::int64_t>(offset) +
                      static_cast<std::int64_t>(stride) * (height - 1) +
                      width;
    if (offset < 0 || last > length) {
        throw dx::VmJavaThrow{
            "Ljava/lang/ArrayIndexOutOfBoundsException;",
            "createBitmap window exceeds the colors array"};
    }
    DexVmAndroidContext::BitmapState state;
    state.width = width;
    state.height = height;
    state.argb.resize(static_cast<std::size_t>(width) *
                      static_cast<std::size_t>(height));
    for (std::int32_t row = 0; row < height; ++row) {
        for (std::int32_t column = 0; column < width; ++column) {
            state.argb[static_cast<std::size_t>(row) *
                           static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(column)] =
                static_cast<std::uint32_t>(model.GetPrimitiveElement(
                    array, offset + row * stride + column));
        }
    }
    const auto instance =
        call.vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
    context->bitmaps[instance.Value()] = std::move(state);
    return dx::VmValue::Ref(instance);
}

void RegisterGraphicsBitmaps(dx::IntrinsicRegistry& registry,
                             const Context& context) {
    registry.Register("android.bitmap_config.clinit",
                      [](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        for (const char* name : {"ARGB_4444", "ARGB_8888"}) {
            vm.SetIntrinsicStaticRef(
                "Landroid/graphics/Bitmap$Config;", name,
                "Landroid/graphics/Bitmap$Config;",
                vm.NewIntrinsicInstance("Landroid/graphics/Bitmap$Config;"));
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.region_op.clinit",
                      [](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        vm.SetIntrinsicStaticRef(
            "Landroid/graphics/Region$Op;", "REPLACE",
            "Landroid/graphics/Region$Op;",
            vm.NewIntrinsicInstance("Landroid/graphics/Region$Op;"));
        return dx::VmValue::Void();
    });
    registry.Register("android.bitmap_factory.decode_byte_array",
                      [context](dx::IntrinsicContext& call) {
        auto& model = call.vm.Model();
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto length = call.arguments[2].AsInt();
        if (!array.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "decodeByteArray data is null"};
        }
        const auto array_length =
            static_cast<std::int64_t>(model.ArrayLength(array));
        if (offset < 0 || length < 0 ||
            static_cast<std::int64_t>(offset) + length > array_length) {
            throw dx::VmJavaThrow{
                "Ljava/lang/ArrayIndexOutOfBoundsException;",
                "decodeByteArray range exceeds the data array"};
        }
        const auto bytes = model.ReadByteRegion(array, offset, length);
        const auto decoded = DecodeImageToArgb(bytes);
        if (!decoded.has_value()) {
            // Documented decode-failure result is null, not a throw.
            GuestLog(call, core::LogLevel::warn,
                     "BitmapFactory.decodeByteArray: undecodable image (" +
                         std::to_string(length) + " bytes)");
            return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        DexVmAndroidContext::BitmapState state;
        state.width = decoded->width;
        state.height = decoded->height;
        state.argb = std::move(decoded->argb);
        const auto instance =
            call.vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
        context->bitmaps[instance.Value()] = std::move(state);
        return dx::VmValue::Ref(instance);
    });
    registry.Register("android.bitmap.create",
                      [context](dx::IntrinsicContext& call) {
        const auto array = call.arguments[0].ref;
        const auto width = call.arguments[1].AsInt();
        const auto height = call.arguments[2].AsInt();
        return MakeBitmapFromArray(call, context, array, 0, width, width,
                                   height);
    });
    registry.Register("android.bitmap.create_offset",
                      [context](dx::IntrinsicContext& call) {
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto stride = call.arguments[2].AsInt();
        const auto width = call.arguments[3].AsInt();
        const auto height = call.arguments[4].AsInt();
        return MakeBitmapFromArray(call, context, array, offset, stride,
                                   width, height);
    });
    registry.Register("android.bitmap.get_width",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(BitmapOf(call, context).width);
    });
    registry.Register("android.bitmap.get_height",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(BitmapOf(call, context).height);
    });
    registry.Register("android.bitmap.get_pixels",
                      [context](dx::IntrinsicContext& call) {
        auto& model = call.vm.Model();
        const auto& state = BitmapOf(call, context);
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto stride = call.arguments[2].AsInt();
        const auto x = call.arguments[3].AsInt();
        const auto y = call.arguments[4].AsInt();
        const auto width = call.arguments[5].AsInt();
        const auto height = call.arguments[6].AsInt();
        if (!array.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "getPixels target array is null"};
        }
        if (x < 0 || y < 0 || width < 0 || height < 0 ||
            x + width > state.width || y + height > state.height) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalArgumentException;",
                "getPixels region exceeds the bitmap"};
        }
        if (width == 0 || height == 0) return dx::VmValue::Void();
        const auto length =
            static_cast<std::int64_t>(model.ArrayLength(array));
        const auto last = static_cast<std::int64_t>(offset) +
                          static_cast<std::int64_t>(stride) * (height - 1) +
                          width;
        if (offset < 0 || stride < width || last > length) {
            throw dx::VmJavaThrow{
                "Ljava/lang/ArrayIndexOutOfBoundsException;",
                "getPixels window exceeds the target array"};
        }
        for (std::int32_t row = 0; row < height; ++row) {
            for (std::int32_t column = 0; column < width; ++column) {
                const auto pixel =
                    state.argb[static_cast<std::size_t>(y + row) *
                                   static_cast<std::size_t>(state.width) +
                               static_cast<std::size_t>(x + column)];
                model.SetPrimitiveElement(array,
                                          offset + row * stride + column,
                                          pixel);
            }
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.bitmap.recycle",
                      [context](dx::IntrinsicContext& call) {
        const auto found = context->bitmaps.find(call.receiver.Value());
        if (found != context->bitmaps.end()) {
            found->second.recycled = true;
            found->second.argb.clear();
            found->second.argb.shrink_to_fit();
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.canvas.save", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    registry.Register("android.canvas.clip_rect",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    registry.Register("android.canvas.get_clip_bounds",
                      [context](dx::IntrinsicContext& call) {
        const auto rect =
            call.vm.NewIntrinsicInstance("Landroid/graphics/Rect;");
        const auto slots = call.vm.Model().InstanceSlots(rect);
        slots[0] = {0, dx::SlotTag::cat1};
        slots[1] = {0, dx::SlotTag::cat1};
        slots[2] = {context->surface_width, dx::SlotTag::cat1};
        slots[3] = {context->surface_height, dx::SlotTag::cat1};
        return dx::VmValue::Ref(rect);
    });
}

void RegisterWidgets(dx::IntrinsicRegistry& registry,
                     const Context& context) {
    registry.Register("android.widget.noop", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.widget.zero", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    // Wide/float neutral answers, so a presentation-only getter never needs a
    // bespoke handler (tools/dexvm_stub_gen.py emits these).
    registry.Register("android.widget.zero_long", [](dx::IntrinsicContext&) {
        return dx::VmValue::Long(0);
    });
    registry.Register("android.widget.zero_float", [](dx::IntrinsicContext&) {
        return dx::VmValue::Float(0.0F);
    });
    registry.Register("android.widget.zero_double",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Double(0.0);
    });
    registry.Register("android.widget.null", [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.widget.self", [](dx::IntrinsicContext& call) {
        return Self(call);
    });
    // TextView text is real state in the interpreter's builder buffer so
    // interpreted logic round-trips what it stored.
    registry.Register("android.textview.set_text",
                      [](dx::IntrinsicContext& call) {
        auto& buffer = call.vm.BuilderBuffer(call.receiver);
        const auto value = call.arguments[0].ref;
        buffer = value.IsValid()
                     ? call.vm.Model().StringValue(value)
                     : std::u16string();
        return dx::VmValue::Void();
    });
    registry.Register("android.textview.get_text",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.Model().NewString(call.vm.BuilderBuffer(call.receiver)));
    });
    registry.Register("android.textview.get_paint",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "text_paint",
                      "Landroid/text/TextPaint;"));
    });
    registry.Register("android.edittext.get_editable",
                      [context](dx::IntrinsicContext& call) {
        const auto key =
            "editable:" + std::to_string(call.receiver.Value());
        const auto editable = Singleton(call, context, key,
                                        "Landroid/text/EditableImpl;");
        context->editable_owner[editable.Value()] = call.receiver.Value();
        return dx::VmValue::Ref(editable);
    });
    const auto owner_buffer =
        [context](dx::IntrinsicContext& call) -> std::u16string& {
        const auto found =
            context->editable_owner.find(call.receiver.Value());
        if (found == context->editable_owner.end()) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "Editable has no owning text widget"};
        }
        return call.vm.BuilderBuffer(dx::VmObjectRef(found->second));
    };
    registry.Register("android.editable.clear",
                      [owner_buffer](dx::IntrinsicContext& call) {
        owner_buffer(call).clear();
        return dx::VmValue::Void();
    });
    registry.Register("android.editable.length",
                      [owner_buffer](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(
            static_cast<std::int32_t>(owner_buffer(call).size()));
    });
    registry.Register("android.editable.replace",
                      [owner_buffer](dx::IntrinsicContext& call) {
        auto& buffer = owner_buffer(call);
        const auto start = call.arguments[0].AsInt();
        const auto end = call.arguments[1].AsInt();
        if (start < 0 || start > end ||
            static_cast<std::size_t>(end) > buffer.size()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IndexOutOfBoundsException;",
                "Editable.replace range is invalid"};
        }
        const auto value = call.arguments[2].ref;
        buffer.replace(static_cast<std::size_t>(start),
                       static_cast<std::size_t>(end - start),
                       value.IsValid()
                           ? call.vm.Model().StringValue(value)
                           : std::u16string());
        return Self(call);
    });
    registry.Register("android.paint.get_text_bounds",
                      [](dx::IntrinsicContext& call) {
        // No font engine exists; the v1 metric is a deterministic
        // monospace estimate (8x16 px per glyph) so layout math stays
        // finite and consistent.
        const auto start = call.arguments[1].AsInt();
        const auto end = call.arguments[2].AsInt();
        const auto rect = call.arguments[3].ref;
        if (!rect.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "getTextBounds bounds rect is null"};
        }
        const auto count = end > start ? end - start : 0;
        const auto slots = call.vm.Model().InstanceSlots(rect);
        slots[0] = {0, dx::SlotTag::cat1};
        slots[1] = {static_cast<std::uint32_t>(-16), dx::SlotTag::cat1};
        slots[2] = {static_cast<std::uint32_t>(count * 8),
                    dx::SlotTag::cat1};
        slots[3] = {0, dx::SlotTag::cat1};
        return dx::VmValue::Void();
    });
    registry.Register("android.dialog.create",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/app/AlertDialog;"));
    });
    registry.Register("android.videoview.unsupported",
                      [](dx::IntrinsicContext& call) {
        // Recorded gap: video playback is not provided; position and
        // duration answer zero so skip paths trigger immediately.
        GuestLog(call, core::LogLevel::warn,
                 "VideoView playback is not provided on this platform");
        return dx::VmValue::Void();
    });
    registry.Register("android.videoview.set_completion",
                      [context](dx::IntrinsicContext& call) {
        context->video_completion[call.receiver.Value()] =
            call.arguments[0].ref;
        return dx::VmValue::Void();
    });
    // Playback is not provided, so start() reports completion right away
    // through the registered listener; splash-video activities then advance
    // exactly as they would after a real playback finished.
    registry.Register("android.videoview.start",
                      [context](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::warn,
                 "VideoView playback is not provided; reporting immediate "
                 "completion");
        const auto found =
            context->video_completion.find(call.receiver.Value());
        if (found == context->video_completion.end() ||
            !found->second.IsValid()) {
            return dx::VmValue::Void();
        }
        auto& vm = call.vm;
        auto& linker = vm.Linker();
        const auto listener_class = vm.Model().ObjectClass(found->second);
        const auto index = linker.FindVtableIndex(
            listener_class, "onCompletion",
            "(Landroid/media/MediaPlayer;)V");
        if (!index.has_value()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalStateException;",
                "completion listener has no onCompletion method"};
        }
        const auto player =
            vm.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
        const auto outcome = vm.Call(
            linker.Class(listener_class).vtable[*index],
            std::vector<dx::VmValue>{dx::VmValue::Ref(found->second),
                                     dx::VmValue::Ref(player)});
        if (outcome.exception.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                                  "onCompletion raised: " +
                                      outcome.exception_message};
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.webview.load_url",
                      [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::warn,
                 "WebView.loadUrl dropped (web content is a non-goal): " +
                     call.vm.StringUtf8(call.arguments[0].ref));
        return dx::VmValue::Void();
    });
    registry.Register("android.webview.get_settings",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "web_settings",
                      "Landroid/webkit/WebSettings;"));
    });
    // System settings table shares the session-lifetime preference store.
    registry.Register("android.settings.get_int",
                      [context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[1].ref);
        auto& store = context->preferences["__android.settings.system"];
        const auto found = store.find(key);
        if (found != store.end()) {
            if (const auto* value = std::get_if<std::int32_t>(
                    &found->second)) {
                return dx::VmValue::Int(*value);
            }
        }
        return dx::VmValue::Int(call.arguments[2].AsInt());
    });
    registry.Register("android.settings.put_int",
                      [context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[1].ref);
        context->preferences["__android.settings.system"][key] =
            call.arguments[2].AsInt();
        return dx::VmValue::Int(1);
    });
    registry.Register("android.ssl.context_instance",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Ljavax/net/ssl/SSLContext;"));
    });
    registry.Register("android.ssl.socket_factory",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "ssl_socket_factory",
                      "Ljavax/net/ssl/SSLSocketFactory;"));
    });
    registry.Register("android.scale_type.clinit",
                      [](dx::IntrinsicContext& call) {
        call.vm.SetIntrinsicStaticRef(
            "Landroid/widget/ImageView$ScaleType;", "CENTER",
            "Landroid/widget/ImageView$ScaleType;",
            call.vm.NewIntrinsicInstance(
                "Landroid/widget/ImageView$ScaleType;"));
        return dx::VmValue::Void();
    });
    registry.Register("android.network_state.clinit",
                      [](dx::IntrinsicContext& call) {
        call.vm.SetIntrinsicStaticRef(
            "Landroid/net/NetworkInfo$State;", "CONNECTED",
            "Landroid/net/NetworkInfo$State;",
            call.vm.NewIntrinsicInstance(
                "Landroid/net/NetworkInfo$State;"));
        return dx::VmValue::Void();
    });
}

}  // namespace ogplay::runtime::android_intrinsics

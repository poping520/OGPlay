#include "android_skia_paint.h"

#include "include/core/SkFontMetrics.h"
#include "include/core/SkScalar.h"

#include <algorithm>
#include <limits>

namespace ogplay::runtime::android_intrinsics {
namespace skia {

std::u16string ReadText(dx::Interpreter& vm, dx::VmObjectRef text, bool array) {
    if (!text.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "text"};
    if (!array) return std::u16string(vm.Model().StringValue(text));
    std::u16string result(static_cast<std::size_t>(vm.Model().ArrayLength(text)), u'\0');
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<char16_t>(vm.Model().GetPrimitiveElement(text, static_cast<int>(i)));
    return result;
}

std::u16string_view TextSlice(std::u16string_view text, int start, int limit, TextRange range) {
    const auto count = range == TextRange::count ? limit : static_cast<std::int64_t>(limit) - start;
    if (start < 0 || count < 0 || static_cast<std::uint64_t>(start + count) > text.size())
        throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "text range"};
    return text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(count));
}

std::u16string_view TextRunSlice(std::u16string_view text, int start, int limit, int context_start, int context_limit,
                                 TextRange range) {
    const auto context = TextSlice(text, context_start, context_limit, range);
    const auto run = TextSlice(text, start, limit, range);
    if (start < context_start || static_cast<std::uint64_t>(start - context_start) + run.size() > context.size())
        throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "text run context"};
    return run;
}

raster::TextRun LayoutText(dx::Interpreter& vm, const DexVmAndroidContext& context, const raster::Paint& paint,
                           std::u16string_view text, bool rtl) {
    auto run = raster::MapText(paint, context.system_font_source, text);
    if (rtl || run.needs_layout) RecordUiDegraded(vm, "graphics.textLayout");
    return run;
}
}  // namespace skia

namespace {
using namespace skia;
using V = dx::VmValue;
constexpr auto kPublicNative = dx::kAccPublic | dx::kAccNative;
constexpr auto kPrivateNative = dx::kAccPrivate | dx::kAccNative;

bool ExplicitRtl(int bidi) {
    return bidi == 1 || bidi == 5;
}

int BreakText(const raster::TextRun& run, bool forward, float max_width, float& width) {
    int units = 0;
    for (std::size_t i = 0; i < run.glyphs.size(); ++i) {
        const auto& glyph = run.glyphs[forward ? i : run.glyphs.size() - i - 1];
        const auto next = width + glyph.advance;
        if (!(next <= max_width)) break;
        width = next;
        units += static_cast<int>(glyph.units);
    }
    return units;
}

int Cursor(std::u16string_view context, int start, int offset, int option) {
    if (offset < start || static_cast<std::size_t>(offset - start) > context.size() || option < 0 || option > 4)
        throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "text cursor"};
    auto index = static_cast<std::size_t>(offset - start);
    if (option == 0 && index < context.size()) ++index;
    if (option == 2 && index > 0) --index;
    if (index > 0 && index < context.size() && context[index - 1] >= 0xd800 && context[index - 1] <= 0xdbff &&
        context[index] >= 0xdc00 && context[index] <= 0xdfff) {
        if (option == 4) return -1;
        return start + static_cast<int>(index) + (option < 2 ? 1 : -1);
    }
    return start + static_cast<int>(index);
}

void WriteWidths(dx::Interpreter& vm, dx::VmObjectRef output, int offset, int units, const raster::TextRun& run) {
    CheckRange(vm, output, offset, units);
    for (int i = 0; i < units; ++i)
        WriteFloat(vm, output, offset + i, 0);
    for (const auto& glyph : run.glyphs)
        WriteFloat(vm, output, offset + static_cast<int>(glyph.unit), glyph.advance);
}

}  // namespace

void skia::AddPaintTextMethods(dx::IntrinsicClassBuilder& b, const Context& context) {
    const auto peer = [context](dx::IntrinsicContext& c) -> raster::Paint& {
        return PaintPeer(*context, IntField(c.vm, c.receiver, "mNativePaint"));
    };
    const auto token_peer = [context](dx::IntrinsicContext& c) -> raster::Paint& {
        return PaintPeer(*context, c.arguments[0].AsInt());
    };
    for (const auto* name : {"ascent", "descent"}) {
        const bool ascent = std::string_view(name) == "ascent";
        b.VirtualMethod(
            name, "()F",
            [peer, ascent](dx::IntrinsicContext& c) {
                SkFontMetrics m;
                peer(c).font.getMetrics(&m);
                return V::Float(ascent ? m.fAscent : m.fDescent);
            },
            kPublicNative);
    }
    b.VirtualMethod(
        "getFontMetrics", "(Landroid/graphics/Paint$FontMetrics;)F",
        [peer](dx::IntrinsicContext& c) {
            SkFontMetrics m;
            const auto spacing = peer(c).font.getMetrics(&m);
            if (const auto out = c.arguments[0].ref; out.IsValid()) {
                for (const auto& [name, value] : {std::pair{"top", m.fTop},
                                                  {"ascent", m.fAscent},
                                                  {"descent", m.fDescent},
                                                  {"bottom", m.fBottom},
                                                  {"leading", m.fLeading}})
                    Field(c.vm, out, name, "F") = {std::bit_cast<std::uint32_t>(value), dx::SlotTag::cat1};
            }
            return V::Float(spacing);
        },
        kPublicNative);
    b.VirtualMethod(
        "getFontMetricsInt", "(Landroid/graphics/Paint$FontMetricsInt;)I",
        [peer](dx::IntrinsicContext& c) {
            SkFontMetrics m;
            peer(c).font.getMetrics(&m);
            const auto ascent = SkScalarRoundToInt(m.fAscent);
            const auto descent = SkScalarRoundToInt(m.fDescent);
            const auto leading = SkScalarRoundToInt(m.fLeading);
            if (const auto out = c.arguments[0].ref; out.IsValid()) {
                for (const auto& [name, value] : {std::pair{"top", SkScalarFloorToInt(m.fTop)},
                                                  {"ascent", ascent},
                                                  {"descent", descent},
                                                  {"bottom", SkScalarCeilToInt(m.fBottom)},
                                                  {"leading", leading}})
                    Field(c.vm, out, name, "I") = {std::bit_cast<std::uint32_t>(value), dx::SlotTag::cat1};
            }
            return V::Int(std::bit_cast<int>(static_cast<std::uint32_t>(descent) - static_cast<std::uint32_t>(ascent) +
                                             static_cast<std::uint32_t>(leading)));
        },
        kPublicNative);

    b.DirectMethod(
        "native_measureText", "(Ljava/lang/String;I)F",
        [context, peer](dx::IntrinsicContext& c) {
            const auto text = ReadText(c.vm, c.arguments[0].ref, false);
            return V::Float(LayoutText(c.vm, *context, peer(c), text, ExplicitRtl(c.arguments[1].AsInt())).width);
        },
        kPrivateNative);

    for (const bool array : {false, true}) {
        const auto range = array ? TextRange::count : TextRange::end;
        b.DirectMethod(
            "native_measureText", array ? "([CIII)F" : "(Ljava/lang/String;III)F",
            [context, peer, array, range](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                const auto text = ReadText(c.vm, a.Ref(0), array);
                return V::Float(LayoutText(c.vm, *context, peer(c), TextSlice(text, a.Int(1), a.Int(2), range),
                                           ExplicitRtl(a.Int(3)))
                                    .width);
            },
            kPrivateNative);

        b.DirectMethod(
            "native_breakText", array ? "([CIIFI[F)I" : "(Ljava/lang/String;ZFI[F)I",
            [context, peer, array](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                const auto text = ReadText(c.vm, a.Ref(0), array);
                const auto count = array ? a.Int(2) : static_cast<int>(text.size());
                if (count == std::numeric_limits<int>::min())
                    throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "breakText count"};
                const auto selection =
                    array ? TextSlice(text, a.Int(1), std::abs(count), TextRange::count) : std::u16string_view(text);
                const auto run = LayoutText(c.vm, *context, peer(c), selection, ExplicitRtl(a.Int(array ? 4 : 3)));
                float width = 0;
                const int units = BreakText(run, array ? count >= 0 : a.Int(1) != 0, a.Float(array ? 3 : 2), width);
                const auto out = a.Ref(array ? 5 : 4);
                if (out.IsValid() && c.vm.Model().ArrayLength(out)) WriteFloat(c.vm, out, 0, width);
                return V::Int(units);
            },
            kPrivateNative);

        b.StaticMethod(
            "native_getTextWidths", array ? "(I[CIII[F)I" : "(ILjava/lang/String;III[F)I",
            [context, token_peer, array, range](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                const auto text = ReadText(c.vm, a.Ref(1), array);
                const auto selection = TextSlice(text, a.Int(2), a.Int(3), range);
                const auto run = LayoutText(c.vm, *context, token_peer(c), selection, ExplicitRtl(a.Int(4)));
                const int units = static_cast<int>(selection.size());
                WriteWidths(c.vm, a.Ref(5), 0, units, run);
                return V::Int(units);
            },
            kStaticNative);

        b.StaticMethod(
            "native_getTextRunAdvances", array ? "(I[CIIIII[FI)F" : "(ILjava/lang/String;IIIII[FI)F",
            [context, token_peer, array, range](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                const auto text = ReadText(c.vm, a.Ref(1), array);
                const auto selection = TextRunSlice(text, a.Int(2), a.Int(3), a.Int(4), a.Int(5), range);
                const auto run = LayoutText(c.vm, *context, token_peer(c), selection, a.Int(6) != 0);
                if (a.Ref(7).IsValid()) WriteWidths(c.vm, a.Ref(7), a.Int(8), static_cast<int>(selection.size()), run);
                return V::Float(run.width);
            },
            kStaticNative);

        b.DirectMethod(
            "native_getTextRunCursor", array ? "(I[CIIIII)I" : "(ILjava/lang/String;IIIII)I",
            [array, range](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                const auto text = ReadText(c.vm, a.Ref(1), array);
                const auto selection = TextSlice(text, a.Int(2), a.Int(3), range);
                return V::Int(Cursor(selection, a.Int(2), a.Int(5), a.Int(6)));
            },
            kPrivateNative);

        b.StaticMethod(
            "native_getTextPath", array ? "(II[CIIFFI)V" : "(IILjava/lang/String;IIFFI)V",
            [context, token_peer, array, range](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                const auto text = ReadText(c.vm, a.Ref(2), array);
                const auto& paint = token_peer(c);
                const auto run = LayoutText(c.vm, *context, paint, TextSlice(text, a.Int(3), a.Int(4), range),
                                            ExplicitRtl(a.Int(1)));
                raster::TextPath(paint, run, a.Float(5), a.Float(6), Require(context->skia_objects->paths, a.Int(7)));
                return V::Void();
            },
            kStaticNative);

        b.StaticMethod(
            array ? "nativeGetCharArrayBounds" : "nativeGetStringBounds",
            array ? "(I[CIIILandroid/graphics/Rect;)V" : "(ILjava/lang/String;IIILandroid/graphics/Rect;)V",
            [context, token_peer, array, range](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                const auto text = ReadText(c.vm, a.Ref(1), array);
                const auto& paint = token_peer(c);
                const auto run = LayoutText(c.vm, *context, paint, TextSlice(text, a.Int(2), a.Int(3), range),
                                            ExplicitRtl(a.Int(4)));
                WriteRect(c.vm, a.Ref(5), raster::TextBounds(paint, run).roundOut());
                return V::Void();
            },
            kStaticNative);
    }

    b.StaticMethod(
        "native_getTextGlyphs", "(ILjava/lang/String;IIIII[C)I",
        [context, token_peer](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto text = ReadText(c.vm, a.Ref(1), false);
            const auto selection = TextRunSlice(text, a.Int(2), a.Int(3), a.Int(4), a.Int(5), TextRange::end);
            const auto run = LayoutText(c.vm, *context, token_peer(c), selection, a.Int(6) != 0);
            CheckRange(c.vm, a.Ref(7), 0, static_cast<int>(run.glyphs.size()));
            for (std::size_t i = 0; i < run.glyphs.size(); ++i)
                c.vm.Model().SetPrimitiveElement(a.Ref(7), static_cast<int>(i), run.glyphs[i].id);
            return V::Int(static_cast<int>(run.glyphs.size()));
        },
        kStaticNative);
}

Decl Declare_android_emoji_EmojiFactory() {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/emoji/EmojiFactory;");
    builder.MergesBootClass();
    // This device provides Unicode fonts and no vendor PUA emoji factory.
    const auto absent = [](dx::IntrinsicContext&) { return dx::VmValue::Ref(dx::VmObjectRef{}); };
    builder.StaticMethod("newAvailableInstance", "()Landroid/emoji/EmojiFactory;", absent,
                         dx::kAccPublic | dx::kAccStatic | dx::kAccNative);
    builder.StaticMethod("newInstance", "(Ljava/lang/String;)Landroid/emoji/EmojiFactory;", absent,
                         dx::kAccPublic | dx::kAccStatic | dx::kAccNative);
    return std::move(builder).Build();
}

Decl Declare_android_text_AndroidBidi(const Context&) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/text/AndroidBidi;");
    b.MergesBootClass();
    b.StaticMethod(
        "runBidi", "(I[C[BIZ)I",
        [](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto chars = ReadText(c.vm, a.Ref(1), true);
            const auto text = TextSlice(chars, 0, a.Int(3), TextRange::count);
            if (raster::ContainsStrongRtl(text)) RecordUiDegraded(c.vm, "AndroidBidi.runBidi");
            CheckRange(c.vm, a.Ref(2), 0, a.Int(3));
            for (int i = 0; i < a.Int(3); ++i)
                c.vm.Model().SetPrimitiveElement(a.Ref(2), i, 0);
            return V::Int(0);
        },
        kStaticNative);
    return std::move(b).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

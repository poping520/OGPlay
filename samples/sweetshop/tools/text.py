"""Bilingual labels, the game title and ribbon banners for the sweetshop atlas.

Text is rasterized at twice its logical size with the Apache-2.0 Android fonts that
OGPlay already ships, so the game needs no font engine at runtime.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

OUTLINE = (58, 36, 24, 255)
SCALE = 2  # text sprites are drawn at half their pixel size
LANGUAGES = ("zh", "en")

# key: (chinese, english, logical height in px, outlined)
STRINGS: dict[str, tuple[str, str, int, bool]] = {
    "subtitle": ("消除糖果 · 收集星星", "Match candies · Collect stars", 30, True),
    "start": ("开始游戏", "Play", 34, True),
    "select_level": ("选择关卡", "Select Level", 40, True),
    "level": ("关卡", "Level", 24, False),
    "moves": ("步数", "Moves", 24, False),
    "score": ("得分", "Score", 24, False),
    "target": ("目标", "Target", 24, False),
    "best": ("最高分", "Best", 24, False),
    "win": ("过关啦！", "Level Clear!", 40, True),
    "lose": ("差一点点！", "So Close!", 40, True),
    "next": ("下一关", "Next", 24, False),
    "retry": ("重玩", "Retry", 24, False),
    "home": ("主页", "Home", 24, False),
    "sound": ("音效", "Sound", 24, False),
    "tip_swap": ("交换相邻的糖果，三个连成一线就能消除", "Swap two candies to line up three of a kind", 26, True),
    "tip_line": ("一次连成四个，得到条纹糖", "Line up four to make a striped candy", 26, True),
    "tip_bomb": ("拼出 L 或 T 形，得到炸弹糖", "Make an L or T shape for a bomb candy", 26, True),
    "tip_rainbow": ("一次连成五个，得到彩虹糖", "Line up five to make a rainbow candy", 26, True),
    "tip_grape": ("葡萄软糖来了，颜色更多啦", "Grape gummies join in: one more color!", 26, True),
    "shuffle": ("没有可走的了，重新洗牌！", "No moves left, shuffling!", 28, True),
    "language": ("EN", "中", 30, True),  # names the language the button switches to
    "demo": ("示例游戏", "Demo Game", 24, False),
    "splash_line": ("示例游戏", "Demo Game", 26, False),
}
TITLES = {"zh": "糖果铺", "en": "Sweet Shop"}
RIBBON_DENSITY = 1.5
RIBBONS = {"pink": (0xF4, 0x8F, 0xB1), "gold": (0xF7, 0xC2, 0x4A), "lavender": (0xB9, 0x9A, 0xE0)}


INK_LEVELS = 86  # translucent steps per ink in the text palette


def ink_palette(image: Image.Image) -> Image.Image:
    """Near-lossless 256-color palette for two-ink sprites (white fill, brown rim).

    Translucent pixels are either ink at some alpha; opaque ones blend between the
    inks. Brown and white get 86 alpha steps each and the opaque blend the remaining
    84, so an RGBA PNG of text shrinks by half with sub-pixel-level error.
    """
    brown, white = OUTLINE[:3], (255, 255, 255)
    mixes = 256 - 2 * INK_LEVELS
    entries = [ink + (round(step * 255 / (INK_LEVELS - 1)),) for ink in (brown, white)
               for step in range(INK_LEVELS)]
    entries += [tuple(round(b + (w - b) * step / (mixes - 1)) for b, w in zip(brown, white)) + (255,)
                for step in range(mixes)]
    span = sum(w - b for b, w in zip(brown, white))
    lookup: dict[tuple[int, int, int, int], int] = {}
    indices = []
    for pixel in image.getdata():
        index = lookup.get(pixel)
        if index is None:
            t = min(1.0, max(0.0, sum(c - b for c, b in zip(pixel[:3], brown)) / span))
            if pixel[3] >= 250:
                index = 2 * INK_LEVELS + round(t * (mixes - 1))
            else:
                index = (INK_LEVELS if t >= 0.5 else 0) + round(pixel[3] * (INK_LEVELS - 1) / 255)
            lookup[pixel] = index
        indices.append(index)
    result = Image.new("P", image.size)
    result.putpalette([channel for entry in entries for channel in entry[:3]])
    result.info["transparency"] = bytes(entry[3] for entry in entries)
    result.putdata(indices)
    return result


def _font(fonts: Path, text: str, size: int) -> ImageFont.FreeTypeFont:
    latin = all(ord(character) < 0x2000 for character in text)
    name = "Roboto-Regular.ttf" if latin else "DroidSansFallback.ttf"
    return ImageFont.truetype(str(fonts / name), size)


def _mask(text: str, font: ImageFont.FreeTypeFont, stroke: int, pad: int) -> Image.Image:
    left, top, right, bottom = font.getbbox(text, stroke_width=stroke)
    mask = Image.new("L", (right - left + pad * 2, bottom - top + pad * 2), 0)
    ImageDraw.Draw(mask).text((pad - left, pad - top), text, font=font, fill=255, stroke_width=stroke,
                              stroke_fill=255)
    return mask


def _layer(mask: Image.Image, color: tuple[int, int, int, int]) -> Image.Image:
    layer = Image.new("RGBA", mask.size, color[:3] + (0,))
    layer.putalpha(ImageChops.multiply(mask, Image.new("L", mask.size, color[3])))
    return layer


def render_label(fonts: Path, text: str, height: int, outlined: bool) -> Image.Image:
    """White text (tintable); outlined labels get the art's dark-brown rim."""
    size = round(height * SCALE * 0.82)
    font = _font(fonts, text, size)
    bold = max(1, size // 22)
    rim = max(2, size // 9) if outlined else 0
    pad = rim + bold + 2
    body = _mask(text, font, bold, pad)
    image = Image.new("RGBA", body.size, (0, 0, 0, 0))
    if outlined:
        image.alpha_composite(_layer(_mask(text, font, bold + rim, pad), OUTLINE))
    image.alpha_composite(_layer(body, (255, 255, 255, 255)))
    return image.crop(image.getbbox())


def render_title(fonts: Path, text: str) -> Image.Image:
    """Fallback logo: pink candy gradient, brown rim, white sticker edge and soft shadow."""
    size = 200
    font = _font(fonts, text, size)
    latin = all(ord(character) < 0x2000 for character in text)
    bold, rim, edge = (size // 16 if latin else size // 34), size // 11, size // 8
    pad = bold + rim + edge + 24
    body = _mask(text, font, bold, pad)
    outline = _mask(text, font, bold + rim, pad)
    sticker = _mask(text, font, bold + rim + edge, pad)
    width, height = body.size
    image = Image.new("RGBA", (width, height + 16), (0, 0, 0, 0))
    shadow = _layer(sticker, (90, 40, 30, 110)).filter(ImageFilter.GaussianBlur(8))
    image.alpha_composite(shadow, (0, 14))
    image.alpha_composite(_layer(sticker, (255, 250, 244, 255)))
    image.alpha_composite(_layer(outline, OUTLINE))
    gradient = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    top, bottom = (255, 168, 196), (236, 82, 132)
    box = body.getbbox() or (0, 0, width, height)
    for y in range(height):
        t = min(1.0, max(0.0, (y - box[1]) / max(1, box[3] - box[1])))
        color = tuple(round(a + (b - a) * t) for a, b in zip(top, bottom))
        ImageDraw.Draw(gradient).line((0, y, width, y), fill=color + (255,))
    gradient.putalpha(body)
    image.alpha_composite(gradient)
    shine = Image.new("L", (width, height), 0)
    ImageDraw.Draw(shine).rectangle((0, box[1], width, box[1] + (box[3] - box[1]) * 0.38), fill=120)
    image.alpha_composite(_layer(ImageChops.multiply(shine, body.filter(ImageFilter.MinFilter(9))), (255, 255, 255, 255)))
    return image.crop(image.getbbox())


def render_ribbon(color: tuple[int, int, int]) -> Image.Image:
    """Banner with folded tails, sized for a short heading (520x110 logical)."""
    width, height = 520 * SCALE, 110 * SCALE
    oversample = 2
    canvas = Image.new("RGBA", (width * oversample, height * oversample), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    o = oversample
    dark = tuple(round(channel * 0.78) for channel in color)
    line = 7 * o
    body_top, body_bottom = 18 * o, 150 * o
    tail_top, tail_bottom = 70 * o, 205 * o
    for side in (1, -1):
        x0 = 0 if side == 1 else width * o
        inner = x0 + side * 170 * o
        tail = [(x0, tail_top), (inner, tail_top), (inner, tail_bottom), (x0, tail_bottom),
                (x0 + side * 55 * o, (tail_top + tail_bottom) / 2)]
        draw.polygon(tail, fill=dark + (255,), outline=OUTLINE, width=line)
        fold = [(inner - side * 0, body_bottom - 4 * o), (inner, tail_bottom),
                (inner - side * 55 * o, body_bottom - 4 * o)]
        draw.polygon(fold, fill=tuple(round(channel * 0.6) for channel in color) + (255,), outline=OUTLINE,
                     width=line)
    draw.rounded_rectangle((115 * o, body_top, (width - 115) * o, body_bottom), radius=26 * o,
                           fill=color + (255,), outline=OUTLINE, width=line)
    draw.rounded_rectangle((135 * o, body_top + 14 * o, (width - 135) * o, body_top + 42 * o), radius=14 * o,
                           fill=(255, 255, 255, 70))
    return canvas.resize((width, height), Image.Resampling.LANCZOS)


def _clean(image: Image.Image) -> Image.Image:
    """Drop the faint speckle AI art leaves around its sticker edge, then trim."""
    image = image.convert("RGBA")
    image.putalpha(image.getchannel("A").point(lambda value: 0 if value < 48 else value))
    return image.crop(image.getbbox())


def _art(source: Path, stem: str) -> Path | None:
    for suffix in (".webp", ".png", ".jpg"):
        if (source / f"{stem}{suffix}").exists():
            return source / f"{stem}{suffix}"
    return None


def titles(source: Path, fonts: Path) -> dict[str, Image.Image]:
    """Game name per language; AI lettering when present, else the font-rendered fallback."""
    result = {}
    for language, text in TITLES.items():
        art = _art(source, f"title_{language}")
        title = render_title(fonts, text) if art is None else _clean(Image.open(art))
        # Shown at most 560 logical px wide, so 880 px stays sharp on a 1.5x window.
        scale = min(1.0, 880 / title.width, 330 / title.height)
        result[language] = title.resize((round(title.width * scale), round(title.height * scale)),
                                        Image.Resampling.LANCZOS)
    return result


def sprites(source: Path, fonts: Path, brand: Path, split_sheet, fit) -> dict[str, Image.Image]:
    result: dict[str, Image.Image] = {
        "og_logo": fit(Image.open(brand).convert("RGBA"), 192, 1.0),
        # Pre-rendered by tools/wordmark.py in the desktop app's own font and colors.
        "og_wordmark": Image.open(source / "og_wordmark.png").convert("RGBA"),
    }
    for key, (chinese, english, height, outlined) in STRINGS.items():
        for language, text in zip(LANGUAGES, (chinese, english)):
            result[f"text_{key}_{language}"] = render_label(fonts, text, height, outlined)
    ribbon_sheet = _art(source, "sheet_ribbons")
    if ribbon_sheet is None:
        for name, color in RIBBONS.items():
            result[f"ribbon_{name}"] = render_ribbon(color)
    else:
        for name, ribbon in zip(RIBBONS, split_sheet(ribbon_sheet, 1, 3, 3)):
            result[f"ribbon_{name}"] = _clean(ribbon)
    # Ribbons span at most ~480 logical px, so 1.5x density is plenty.
    for name in RIBBONS:
        ribbon = result[f"ribbon_{name}"]
        width = round(520 * RIBBON_DENSITY)
        result[f"ribbon_{name}"] = ribbon.resize((width, round(ribbon.height * width / ribbon.width)),
                                                 Image.Resampling.LANCZOS)
    return result

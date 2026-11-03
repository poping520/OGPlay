"""Turn the sweetshop source art into the runtime atlas, textures and launcher icons."""

from __future__ import annotations

import math
from collections import deque
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

import text

OUTLINE = (58, 36, 24, 255)
ATLAS_SIZE = 2048
PADDING = 6
CANDY_SIZE = 128
ICON_SIZE = 128
DIGIT_HEIGHT = 96

CANDY_NAMES = ["candy_red", "candy_orange", "candy_yellow", "candy_green",
               "candy_blue", "candy_purple", "candy_rainbow"]
ICON_NAMES = ["icon_play", "icon_retry", "icon_home", "icon_sound_on", "icon_sound_off",
              "icon_star", "icon_moves", "icon_target", "icon_lock"]
LAUNCHER_SIZES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def largest_component(cell: Image.Image) -> Image.Image:
    """Crop a sheet cell to its largest opaque blob, dropping stray specks."""
    alpha = cell.getchannel("A")
    scale = 4
    small = alpha.resize((max(1, cell.width // scale), max(1, cell.height // scale)))
    width, height = small.size
    mask = [value > 40 for value in small.getdata()]
    seen = [False] * len(mask)
    best: list[int] = []
    for start in range(len(mask)):
        if not mask[start] or seen[start]:
            continue
        component = []
        queue = deque([start])
        seen[start] = True
        while queue:
            index = queue.popleft()
            component.append(index)
            x, y = index % width, index // width
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if 0 <= nx < width and 0 <= ny < height:
                    neighbor = ny * width + nx
                    if mask[neighbor] and not seen[neighbor]:
                        seen[neighbor] = True
                        queue.append(neighbor)
        if len(component) > len(best):
            best = component
    if not best:
        raise RuntimeError("sprite sheet cell is empty")
    xs = [index % width for index in best]
    ys = [index // width for index in best]
    box = (max(0, min(xs) * scale - scale), max(0, min(ys) * scale - scale),
           min(cell.width, (max(xs) + 2) * scale), min(cell.height, (max(ys) + 2) * scale))
    cropped = cell.crop(box)
    # Clear anything outside the blob's dilated footprint (neighbor specks inside the box).
    footprint = Image.new("L", small.size, 0)
    footprint.putdata([255 if value else 0 for value in _membership(best, len(mask))])
    footprint = footprint.filter(ImageFilter.MaxFilter(5)).resize(cell.size).crop(box)
    cropped.putalpha(Image.composite(cropped.getchannel("A"), Image.new("L", cropped.size, 0), footprint))
    return cropped


def _membership(indices: list[int], length: int) -> list[bool]:
    member = [False] * length
    for index in indices:
        member[index] = True
    return member


def split_sheet(path: Path, columns: int, rows: int, count: int) -> list[Image.Image]:
    sheet = Image.open(path).convert("RGBA")
    cell_width, cell_height = sheet.width / columns, sheet.height / rows
    sprites = []
    for index in range(count):
        column, row = index % columns, index // columns
        box = (round(column * cell_width), round(row * cell_height),
               round((column + 1) * cell_width), round((row + 1) * cell_height))
        sprites.append(largest_component(sheet.crop(box)))
    return sprites


def fit_square(image: Image.Image, size: int, fill: float = 0.92) -> Image.Image:
    scale = size * fill / max(image.width, image.height)
    resized = image.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))),
                           Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.alpha_composite(resized, ((size - resized.width) // 2, (size - resized.height) // 2))
    return canvas


def arc(cx: float, cy: float, rx: float, ry: float, start: float, end: float,
        steps: int = 28) -> list[tuple[float, float]]:
    return [(cx + rx * math.cos(math.radians(start + (end - start) * i / steps)),
             cy + ry * math.sin(math.radians(start + (end - start) * i / steps)))
            for i in range(steps + 1)]


def glyph_strokes() -> dict[str, list[list[tuple[float, float]]]]:
    six = [arc(0.3, 0.69, 0.29, 0.31, 0, 360), arc(0.62, 0.68, 0.6, 0.68, 263, 180)]
    return {
        "0": [arc(0.3, 0.5, 0.3, 0.5, 0, 360)],
        "1": [[(0.1, 0.22), (0.34, 0.0), (0.34, 1.0)]],
        "2": [arc(0.3, 0.28, 0.28, 0.28, 190, 385) + [(0.02, 1.0), (0.6, 1.0)]],
        "3": [arc(0.3, 0.26, 0.26, 0.24, 200, 450) + arc(0.3, 0.74, 0.28, 0.26, 270, 520)],
        "4": [[(0.46, 1.0), (0.46, 0.0), (0.0, 0.68), (0.62, 0.68)]],
        "5": [[(0.56, 0.0), (0.1, 0.0), (0.06, 0.46)] + arc(0.3, 0.68, 0.28, 0.32, 215, 500)],
        "6": six,
        "7": [[(0.02, 0.0), (0.6, 0.0), (0.24, 1.0)]],
        "8": [arc(0.3, 0.26, 0.24, 0.24, 0, 360), arc(0.3, 0.73, 0.29, 0.27, 0, 360)],
        "9": [[(0.6 - x, 1.0 - y) for x, y in stroke] for stroke in six],
        "x": [[(0.08, 0.38), (0.52, 0.92)], [(0.52, 0.38), (0.08, 0.92)]],
        "+": [[(0.3, 0.3), (0.3, 0.95)], [(0.02, 0.62), (0.58, 0.62)]],
        "/": [[(0.55, 0.0), (0.05, 1.0)]],
    }


def render_glyph(strokes: list[list[tuple[float, float]]], height: int) -> Image.Image:
    """Bubbly white digits with the art's dark-brown outline; white takes the tint."""
    oversample = 4
    body = 0.17 * height * oversample
    outline = 0.075 * height * oversample
    glyph_height = height * oversample * 0.74
    margin = outline * 2 + body
    width = int(0.62 * glyph_height + margin * 2)
    image = Image.new("RGBA", (width, int(height * oversample)), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    top = (height * oversample - glyph_height) / 2

    def points(stroke):
        return [(margin + x * glyph_height, top + y * glyph_height) for x, y in stroke]

    for color, thickness in ((OUTLINE, body + outline * 2), ((255, 255, 255, 255), body)):
        for stroke in strokes:
            path = points(stroke)
            draw.line(path, fill=color, width=int(thickness), joint="curve")
            radius = thickness / 2
            for x, y in (path[0], path[-1]):
                draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=color)
    return image.resize((max(1, width // oversample), height), Image.Resampling.LANCZOS)


def rounded(size: tuple[int, int], radius: int, fill, outline=None, outline_width: int = 0) -> Image.Image:
    oversample = 4
    image = Image.new("RGBA", (size[0] * oversample, size[1] * oversample), (0, 0, 0, 0))
    ImageDraw.Draw(image).rounded_rectangle(
        (0, 0, image.width - 1, image.height - 1), radius * oversample, fill=fill,
        outline=outline, width=outline_width * oversample)
    return image.resize(size, Image.Resampling.LANCZOS)


def shapes() -> dict[str, Image.Image]:
    result = {
        "panel": rounded((96, 96), 30, (255, 248, 238, 255), OUTLINE, 5),
        "tile": rounded((64, 64), 18, (255, 255, 255, 255)),
    }
    oversample = 4
    size = 128 * oversample
    circle = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(circle).ellipse((4, 4, size - 4, size - 4), fill=(255, 255, 255, 255))
    result["circle"] = circle.resize((128, 128), Image.Resampling.LANCZOS)
    ring = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(ring).ellipse((8, 8, size - 8, size - 8), outline=(255, 255, 255, 255),
                                 width=16 * oversample)
    result["ring"] = ring.resize((128, 128), Image.Resampling.LANCZOS)
    glow = Image.new("RGBA", (128, 128), (255, 255, 255, 0))
    glow.putalpha(Image.frombytes("L", (128, 128), bytes(
        int(255 * max(0.0, 1.0 - math.hypot(x - 63.5, y - 63.5) / 64.0) ** 2)
        for y in range(128) for x in range(128))))
    result["glow"] = glow
    sparkle = Image.new("RGBA", (64 * oversample, 64 * oversample), (0, 0, 0, 0))
    center, reach, waist = 32 * oversample, 31 * oversample, 7 * oversample
    ImageDraw.Draw(sparkle).polygon(
        [(center, center - reach), (center + waist, center - waist), (center + reach, center),
         (center + waist, center + waist), (center, center + reach), (center - waist, center + waist),
         (center - reach, center), (center - waist, center - waist)], fill=(255, 255, 255, 255))
    result["sparkle"] = sparkle.resize((64, 64), Image.Resampling.LANCZOS)
    return result


def stripes(candy: Image.Image) -> Image.Image:
    """White bands masked by the candy silhouette, drawn over a line-clearing candy."""
    bands = Image.new("L", candy.size, 0)
    draw = ImageDraw.Draw(bands)
    step = candy.height / 7
    for index in (1.5, 3.0, 4.5):
        top = index * step
        draw.rounded_rectangle((0, top, candy.width, top + step * 0.8), radius=int(step * 0.4), fill=205)
    interior = candy.getchannel("A").filter(ImageFilter.MinFilter(11))
    mask = Image.composite(bands, Image.new("L", candy.size, 0), interior)
    overlay = Image.new("RGBA", candy.size, (255, 255, 255, 0))
    overlay.putalpha(mask)
    return overlay


def density(name: str) -> float:
    if name.startswith("text_") or name == "og_wordmark":
        return text.SCALE
    return text.RIBBON_DENSITY if name.startswith("ribbon_") else 1.0


def pack(sprites: dict[str, Image.Image], sheet: int) -> tuple[Image.Image, list[str]]:
    """Shelf-pack into a 2048-wide sheet, then trim to the smallest power-of-two height."""
    atlas = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (0, 0, 0, 0))
    x = y = shelf = PADDING
    lines = []
    for name, sprite in sorted(sprites.items(), key=lambda item: -item[1].height):
        if x + sprite.width + PADDING > ATLAS_SIZE:
            x, y, shelf = PADDING, y + shelf + PADDING, 0
        if y + sprite.height + PADDING > ATLAS_SIZE:
            raise RuntimeError("sprite atlas is full")
        atlas.alpha_composite(sprite, (x, y))
        lines.append(f"{name} {sheet} {x} {y} {sprite.width} {sprite.height} {density(name):g}")
        x += sprite.width + PADDING
        shelf = max(shelf, sprite.height)
    height = 1
    while height < y + shelf + PADDING:
        height *= 2
    return atlas.crop((0, 0, ATLAS_SIZE, height)), lines


def save_split(image: Image.Image, folder: Path, stem: str, fill: tuple[int, int, int]) -> None:
    """JPEG color plus a grayscale alpha mask: about a tenth of an RGBA PNG at the same look.

    Color stays straight (not matted) so translucent areas keep their tone; fully
    transparent pixels take `fill`, the color the silhouette edge is drawn in, so JPEG
    ringing at the boundary stays invisible.
    """
    alpha = image.getchannel("A")
    color = Image.new("RGB", image.size, fill)
    color.paste(image.convert("RGB"), mask=alpha.point(lambda value: 255 if value else 0))
    color.save(folder / f"{stem}.jpg", quality=85, optimize=True)
    alpha.save(folder / f"{stem}_alpha.png", optimize=True)


def build(source: Path, assets: Path, resources: Path, fonts: Path, brand: Path) -> None:
    assets.mkdir(parents=True, exist_ok=True)
    sprites: dict[str, Image.Image] = {}
    candies = [fit_square(sprite, CANDY_SIZE) for sprite in split_sheet(source / "sheet_candies.webp", 4, 2, 7)]
    for name, candy in zip(CANDY_NAMES, candies):
        sprites[name] = candy
        if name != "candy_rainbow":
            sprites[name.replace("candy_", "stripes_")] = stripes(candy)
    for name, icon in zip(ICON_NAMES, split_sheet(source / "sheet_icons.webp", 3, 3, 9)):
        sprites[name] = fit_square(icon, ICON_SIZE, 0.96)
    for name, strokes in glyph_strokes().items():
        sprites[f"glyph_{name}"] = render_glyph(strokes, DIGIT_HEIGHT)
    sprites.update(shapes())
    sprites.update(text.sprites(source, fonts, brand, split_sheet, fit_square))
    # Two-ink text and digits pack apart so they can use a near-lossless palette.
    inked = {name: sprite for name, sprite in sprites.items() if name.startswith(("text_", "glyph_"))}
    art, art_lines = pack({name: sprite for name, sprite in sprites.items() if name not in inked}, 0)
    art.save(assets / "atlas.png", optimize=True)
    lettering, text_lines = pack(inked, 1)
    palette = text.ink_palette(lettering)
    palette.save(assets / "text.png", optimize=True, transparency=palette.info["transparency"])
    (assets / "atlas.txt").write_text("\n".join(art_lines + text_lines) + "\n", encoding="ascii")

    background = Image.open(source / "bg_shop.jpg").convert("RGB")
    target_ratio = 16 / 9
    if background.width / background.height > target_ratio:
        width = round(background.height * target_ratio)
        background = background.crop(((background.width - width) // 2, 0,
                                      (background.width + width) // 2, background.height))
    else:
        height = round(background.width / target_ratio)
        background = background.crop((0, (background.height - height) // 2,
                                      background.width, (background.height + height) // 2))
    background.resize((1280, 720), Image.Resampling.LANCZOS).save(
        assets / "background.jpg", quality=88, optimize=True, progressive=False)

    # Titles get textures of their own instead of atlas space; their rim is the white sticker edge.
    for language, title in text.titles(source, fonts).items():
        save_split(title, assets, f"title_{language}", (255, 255, 255))

    logo = Image.open(source / "logo_jar.webp").convert("RGBA")
    logo = logo.crop(logo.getchannel("A").point(lambda value: 255 if value > 24 else 0).getbbox())
    scale = 560 / max(logo.width, logo.height)
    logo = logo.resize((round(logo.width * scale), round(logo.height * scale)), Image.Resampling.LANCZOS)
    save_split(logo, assets, "logo", OUTLINE[:3])

    # The OGPlay cartridge in the corner marks the icon as the bundled demo.
    icon = Image.open(source / "icon_app.jpg").convert("RGBA").resize((1024, 1024), Image.Resampling.LANCZOS)
    icon.putalpha(rounded((1024, 1024), 205, (255, 255, 255, 255)).getchannel("A"))
    badge = fit_square(Image.open(brand).convert("RGBA"), 330, 1.0)
    shadow = Image.new("RGBA", badge.size, (60, 30, 20, 0))
    shadow.putalpha(badge.getchannel("A").point(lambda value: value * 0.45).filter(ImageFilter.GaussianBlur(10)))
    icon.alpha_composite(shadow, (1024 - 330 - 30, 1024 - 330 - 22))
    icon.alpha_composite(badge, (1024 - 330 - 36, 1024 - 330 - 36))
    for density, size in LAUNCHER_SIZES.items():
        folder = resources / f"mipmap-{density}"
        folder.mkdir(parents=True, exist_ok=True)
        icon.resize((size, size), Image.Resampling.LANCZOS).save(folder / "ic_launcher.png", optimize=True)

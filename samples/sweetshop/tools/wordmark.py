#!/usr/bin/env python3
"""Render art/source/og_wordmark.png: the OGPlay wordmark exactly as the desktop app sets it.

The app draws "OGPlay" in SF Pro Rounded at weight 900 with "OG" in its yellow and
"Play" in its ink color (webui/packages/ui-kit/theme.css). Only the rendered image is
kept in the repository; run this on macOS when the wordmark style changes.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONT = Path("/System/Library/Fonts/SFNSRounded.ttf")
YELLOW = (0xFF, 0xC2, 0x1A, 255)
INK = (0xF2, 0xF4, 0xFA, 255)
SIZE = 96  # twice the logical height the game draws it at
TRACKING = -0.01  # letter-spacing in em, as the app's splash uses


def render() -> Image.Image:
    font = ImageFont.truetype(str(FONT), SIZE)
    font.set_variation_by_axes([400, 900])  # GRAD default, Weight 900
    canvas = Image.new("RGBA", (SIZE * 6, SIZE * 2), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    x, baseline = SIZE * 0.25, SIZE * 1.3
    for word, color in (("OG", YELLOW), ("Play", INK)):
        for index, character in enumerate(word):
            draw.text((x, baseline), character, font=font, fill=color, anchor="ls")
            following = word[index + 1] if index + 1 < len(word) else ""
            # Pair advance keeps the font's kerning; tracking matches the CSS letter-spacing.
            x += font.getlength(character + following) - font.getlength(following) + TRACKING * SIZE
    left, top, right, bottom = canvas.getbbox()
    # Pad the top by the descender depth of "y" so the image center is the letters'
    # center: centered placement then lines up with labels next to it.
    descent = bottom - round(baseline)
    trimmed = canvas.crop((left, top, right, bottom))
    result = Image.new("RGBA", (trimmed.width, trimmed.height + descent), (0, 0, 0, 0))
    result.alpha_composite(trimmed, (0, descent))
    return result


if __name__ == "__main__":
    target = Path(__file__).resolve().parents[1] / "art" / "source" / "og_wordmark.png"
    render().save(target, optimize=True)
    print(target)

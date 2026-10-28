# API19 UI font chain

Copied unchanged from AOSP android-4.4.4_r2.0.1,
platform/frameworks/base/data/fonts/:

1. Roboto-Regular.ttf: sans-serif Regular in system_fonts.xml.
2. DroidSansFallback.ttf: fallback in fallback_fonts.xml.

The ordered, pinned subset selects the first covering glyph. ASCII retains
OGPlay's 5x7 font and metrics; bold/italic remain synthetic. Only independent
BMP glyphs are supported; shaping, combining, bidi and supplementary scalars
fail explicitly. No host or application fonts are read.

Apache-2.0 notice: ../notices/fonts.txt (AOSP data/fonts/NOTICE with a source header).
Hashes and sizes are recorded in ../manifest.json.

# API19 fonts

Copied unchanged from AOSP android-4.4.4_r2.0.1,
platform/frameworks/base/data/fonts/.

- ROM Paint and Typeface use the seven families, styles and aliases in
  `system_fonts.xml`. DroidSansFallback.ttf is the sole fallback.
- The UI projection uses only Roboto-Regular.ttf and DroidSansFallback.ttf,
  in that order. It keeps the existing ASCII 5x7 raster and fixed metrics.

The font data is supplied by the runtime payload. The ROM path shares
immutable Skia typefaces per process and opens custom fonts through APK
assets or the guest VFS. The UI projection keeps its own font chain.

The [Apache-2.0 notice](../notices/fonts.txt) contains the original AOSP
notice. Sizes and SHA-256 hashes are recorded in [manifest.json](../manifest.json).

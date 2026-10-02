# AMD ATC block decoder

Source: [GPUOpen-Tools/compressonator](https://github.com/GPUOpen-Tools/compressonator), commit
`f4b53d79ec5abbb50924f58aebb7bf2793200b94`.

`compressonatori_tc.c` and `compressonator_tc.h` are byte-for-byte upstream
`cmp_compressonatorlib/ati/` files. LICENSE.txt is upstream license/license.txt.
Only the RGB block decoder is called; no SDK, codec framework or GPU tool dependency.
The RGBA alpha adapter follows the same commit's atc/codec_atc.cpp
DecompressExplicitAlphaBlock and GetCompressedAlphaRamp/DecompressInterpolatedAlphaBlock.
All endian, bounds, image layout and allocation handling belongs in src/gles/atc.cpp.

CMake checks the three upstream file SHA256 values before compilation.

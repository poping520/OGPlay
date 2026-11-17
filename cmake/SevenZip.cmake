# 7-Zip 26.06. Source groups follow Bundles/Format7zExtractR/makefile and
# Bundles/Format7zF/Arc{,_gcc}.mak. No bundle exports, UI, encoder targets or ASM.
set(_7z_root "${PROJECT_SOURCE_DIR}/third_party/7zip")
if(NOT EXISTS "${_7z_root}/C/7zVersion.h")
    message(FATAL_ERROR "7-Zip sources missing. Run: git submodule update --init third_party/7zip")
endif()
file(STRINGS "${_7z_root}/C/7zVersion.h" _7z_version REGEX "^#define MY_VERSION_NUMBERS ")
if(NOT _7z_version STREQUAL "#define MY_VERSION_NUMBERS \"26.06\"")
    message(FATAL_ERROR "The 7-Zip filename patches require 7-Zip 26.06. Check the third_party/7zip gitlink.")
endif()
find_package(Git REQUIRED)
find_package(Threads REQUIRED)

set(_7z_patch_dir "${PROJECT_SOURCE_DIR}/src/archive7z/patches")
set(_7z_patched "${CMAKE_CURRENT_BINARY_DIR}/generated/7zip")
foreach(_name IN ITEMS Zip Rar)
    if(_name STREQUAL "Zip")
        set(_patch "${_7z_patch_dir}/zip-names.patch")
        set(_files ZipHandler.cpp ZipHandler.h ZipIn.cpp ZipIn.h ZipItem.cpp ZipItem.h)
    else()
        set(_patch "${_7z_patch_dir}/rar4-names.patch")
        set(_files RarHandler.cpp RarHandler.h RarItem.h)
    endif()
    set(_folder "CPP/7zip/Archive/${_name}")
    set(_scratch "${_7z_patched}/input")
    foreach(_file IN LISTS _files)
        # Always start with pristine input, even if the last configure left a patched copy.
        configure_file("${_7z_root}/${_folder}/${_file}" "${_scratch}/${_folder}/${_file}" COPYONLY)
        file(COPY_FILE "${_7z_root}/${_folder}/${_file}" "${_scratch}/${_folder}/${_file}")
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_patch}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_7z_patched}"
            "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${_patch}"
        WORKING_DIRECTORY "${_scratch}"
        RESULT_VARIABLE _patch_status ERROR_VARIABLE _patch_error)
    if(NOT _patch_status STREQUAL "0")
        message(FATAL_ERROR "Cannot apply the 7-Zip 26.06 filename patch ${_patch}: ${_patch_error}")
    endif()
    foreach(_file IN LISTS _files)
        set(_copy "${_7z_patched}/${_folder}/${_file}")
        configure_file("${_scratch}/${_folder}/${_file}" "${_copy}" COPYONLY)
        if(_file MATCHES "[.]cpp$")
            list(APPEND _7z_patched_sources "${_copy}")
            set_source_files_properties("${_copy}" PROPERTIES INCLUDE_DIRECTORIES "${_7z_root}/${_folder}")
        endif()
    endforeach()
endforeach()

set(_7z_c
    7zCrc 7zCrcOpt 7zStream Aes AesOpt Alloc Bcj2 Blake2s Bra Bra86 BraIA64 CpuArch Delta
    Lzma2Dec Lzma2DecMt LzmaDec MtDec Ppmd7 Ppmd7Dec Ppmd7aDec Ppmd8 Ppmd8Dec Ppmd8Enc
    Sha1 Sha1Opt Sha256 Sha256Opt SwapBytes Threads Xxh64 Xz XzCrc64 XzCrc64Opt XzDec ZstdDec)
set(_7z_sources)
foreach(_unit IN LISTS _7z_c)
    list(APPEND _7z_sources "${_7z_root}/C/${_unit}.c")
endforeach()
set(_7z_cpp
    Common/CRC Common/CrcReg Common/IntToString Common/MyString Common/MyVector Common/NewHandler
    Common/Sha1Prepare Common/Sha256Prepare Common/StringConvert Common/StringToInt Common/UTFConvert
    Common/Wildcard Common/XzCrc64Init
    Windows/PropVariant Windows/PropVariantUtils Windows/Synchronization Windows/System Windows/TimeUtils
    7zip/Common/CreateCoder 7zip/Common/CWrappers 7zip/Common/FilterCoder 7zip/Common/InBuffer
    7zip/Common/LimitedStreams 7zip/Common/MethodId 7zip/Common/MethodProps 7zip/Common/OutBuffer
    7zip/Common/ProgressUtils 7zip/Common/PropId 7zip/Common/StreamBinder 7zip/Common/StreamObjects
    7zip/Common/StreamUtils 7zip/Common/VirtThread
    7zip/Archive/Common/CoderMixer2 7zip/Archive/Common/FindSignature 7zip/Archive/Common/HandlerOut
    7zip/Archive/Common/ItemNameUtils 7zip/Archive/Common/OutStreamWithCRC 7zip/Archive/Common/ParseProperties
    7zip/Archive/7z/7zCompressionMode 7zip/Archive/7z/7zDecode 7zip/Archive/7z/7zExtract
    7zip/Archive/7z/7zHandler 7zip/Archive/7z/7zHeader 7zip/Archive/7z/7zIn 7zip/Archive/7z/7zProperties
    7zip/Archive/Rar/Rar5Handler
    7zip/Compress/Bcj2Coder 7zip/Compress/Bcj2Register 7zip/Compress/BcjCoder 7zip/Compress/BcjRegister
    7zip/Compress/BitlDecoder 7zip/Compress/BranchMisc 7zip/Compress/BranchRegister 7zip/Compress/ByteSwap
    7zip/Compress/BZip2Crc 7zip/Compress/BZip2Decoder 7zip/Compress/BZip2Register
    7zip/Compress/CodecExports 7zip/Compress/CopyCoder 7zip/Compress/CopyRegister
    7zip/Compress/Deflate64Register 7zip/Compress/DeflateDecoder 7zip/Compress/DeflateRegister
    7zip/Compress/DeltaFilter 7zip/Compress/ImplodeDecoder 7zip/Compress/LzfseDecoder
    7zip/Compress/Lzma2Decoder 7zip/Compress/Lzma2Register 7zip/Compress/LzmaDecoder 7zip/Compress/LzmaRegister
    7zip/Compress/LzOutWindow 7zip/Compress/PpmdDecoder 7zip/Compress/PpmdRegister 7zip/Compress/PpmdZip
    7zip/Compress/Rar1Decoder 7zip/Compress/Rar2Decoder 7zip/Compress/Rar3Decoder 7zip/Compress/Rar3Vm
    7zip/Compress/Rar5Decoder 7zip/Compress/RarCodecsRegister 7zip/Compress/ShrinkDecoder
    7zip/Compress/XzDecoder 7zip/Compress/ZstdDecoder
    # Referenced by the selected readers. The wrapper refuses all password requests.
    7zip/Crypto/7zAes 7zip/Crypto/7zAesRegister 7zip/Crypto/HmacSha1 7zip/Crypto/HmacSha256
    7zip/Crypto/MyAes 7zip/Crypto/Pbkdf2HmacSha1 7zip/Crypto/RandGen
    7zip/Crypto/Rar20Crypto 7zip/Crypto/Rar5Aes 7zip/Crypto/RarAes
    7zip/Crypto/WzAes 7zip/Crypto/ZipCrypto 7zip/Crypto/ZipStrong)
if(NOT WIN32)
    list(APPEND _7z_cpp Common/MyWindows)
endif()
foreach(_unit IN LISTS _7z_cpp)
    list(APPEND _7z_sources "${_7z_root}/CPP/${_unit}.cpp")
endforeach()

# ZIP has no upstream extract-only class layout. Its reader needs the shared
# method options and a vtable with writer stubs, but no ZIP writer sources.
set_source_files_properties(
    "${_7z_patched}/CPP/7zip/Archive/Zip/ZipHandler.cpp"
    "${_7z_patched}/CPP/7zip/Archive/Zip/ZipIn.cpp"
    "${_7z_patched}/CPP/7zip/Archive/Zip/ZipItem.cpp"
    "${_7z_root}/CPP/7zip/Archive/Common/HandlerOut.cpp"
    src/archive7z/zip_read_only.cpp
    PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/UZ7_EXTRACT_ONLY,-UZ7_EXTRACT_ONLY>")

# ZIP AES decoding uses CAesCoder::SetCoderProperties and CAesCtrCoder::SetFunctions.
# Those implementations share MyAes.cpp with writer code, so compile this unit with
# the upstream extract-only guard disabled; the unused writer COMDATs are discarded.
set_source_files_properties("${_7z_root}/CPP/7zip/Crypto/MyAes.cpp"
    PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/UZ7_EXTRACT_ONLY,-UZ7_EXTRACT_ONLY>")

add_library(ogplay_7z SHARED src/archive7z/archive7z.cpp src/archive7z/zip_read_only.cpp
    ${_7z_patched_sources} ${_7z_sources})
target_include_directories(ogplay_7z PUBLIC "${PROJECT_SOURCE_DIR}/src/archive7z")
target_include_directories(ogplay_7z SYSTEM PRIVATE
    "${_7z_patched}" "${_7z_root}"
    "${_7z_root}/CPP/7zip/Archive/Rar" "${_7z_root}/CPP/7zip/Archive/Zip")
target_compile_definitions(ogplay_7z PRIVATE Z7_EXTRACT_ONLY Z7_COM_USE_ATOMIC)
target_link_libraries(ogplay_7z PRIVATE Threads::Threads)
set_target_properties(ogplay_7z PROPERTIES
    OUTPUT_NAME 7z
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
    WINDOWS_EXPORT_ALL_SYMBOLS OFF)
# PpmdZip.cpp contains writer methods alongside the decoder. MSVC retains their
# references in the ZIP handler vtable, so include the shared Ppmd8 encoder support
# unit; no encoder is registered or exported by this library.
if(WIN32)
    target_compile_options(ogplay_7z PRIVATE /Gy /Gw)
    target_link_options(ogplay_7z PRIVATE /OPT:REF)
    target_compile_definitions(ogplay_7z PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(ogplay_7z PRIVATE oleaut32 ole32 uuid)
elseif(APPLE)
    set_target_properties(ogplay_7z PROPERTIES INSTALL_NAME_DIR "@rpath")
    target_link_options(ogplay_7z PRIVATE "LINKER:-exported_symbol,_ogplay_7z_*" "LINKER:-dead_strip")
else()
    set(_7z_exports "${_7z_patched}/exports.map")
    file(CONFIGURE OUTPUT "${_7z_exports}" CONTENT "{ global: ogplay_7z_*; local: *; };\n" @ONLY)
    target_compile_options(ogplay_7z PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(ogplay_7z PRIVATE "LINKER:--version-script,${_7z_exports}"
        "LINKER:--no-undefined" "LINKER:--gc-sections")
    set_property(TARGET ogplay_7z APPEND PROPERTY LINK_DEPENDS "${_7z_exports}")
endif()

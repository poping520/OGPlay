# The API19 libcore native boundary must never resolve the host ICU.
include(FetchContent)
set(_icu_archive "https://android.googlesource.com/platform/external/icu4c/+archive/18668f3b015a110275f5cc9a8722b2f65f3333bf.tar.gz")
# An offline cache is accepted only through the same archive digest.
if(EXISTS "${PROJECT_SOURCE_DIR}/.local/dvm102-audit/icu4c-18668f3.tar.gz")
    set(_icu_archive "${PROJECT_SOURCE_DIR}/.local/dvm102-audit/icu4c-18668f3.tar.gz")
endif()
FetchContent_Declare(ogplay_icu51
    URL "${_icu_archive}"
    URL_HASH SHA256=8c2a2a2305bbecf17da14fa42fc6222882814d40645253b2183d213e8ac560ae
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_GetProperties(ogplay_icu51)
if(NOT ogplay_icu51_POPULATED)
    FetchContent_Populate(ogplay_icu51)
endif()

# Use the exact upstream object lists, including their platform-guarded ports.
function(ogplay_icu_sources family output)
    file(READ "${ogplay_icu51_SOURCE_DIR}/${family}/Makefile.in" contents)
    string(REPLACE "\\\n" " " contents "${contents}")
    string(REGEX MATCH "\nOBJECTS = ([^\n]+)" objects "${contents}")
    string(REGEX MATCHALL "[a-zA-Z0-9_]+\\.o" objects "${CMAKE_MATCH_1}")
    set(sources)
    foreach(object IN LISTS objects)
        string(REGEX REPLACE "\\.o$" "" stem "${object}")
        if(EXISTS "${ogplay_icu51_SOURCE_DIR}/${family}/${stem}.cpp")
            list(APPEND sources "${ogplay_icu51_SOURCE_DIR}/${family}/${stem}.cpp")
        elseif(EXISTS "${ogplay_icu51_SOURCE_DIR}/${family}/${stem}.c")
            list(APPEND sources "${ogplay_icu51_SOURCE_DIR}/${family}/${stem}.c")
        else()
            message(FATAL_ERROR "ICU51 source missing: ${family}/${stem}")
        endif()
    endforeach()
    set(${output} "${sources}" PARENT_SCOPE)
endfunction()
ogplay_icu_sources(common _icu_common)
ogplay_icu_sources(i18n _icu_i18n)
add_library(ogplay_icu_common STATIC ${_icu_common}
    "${ogplay_icu51_SOURCE_DIR}/stubdata/stubdata.c")
add_library(ogplay_icu_i18n STATIC ${_icu_i18n})
foreach(target IN ITEMS ogplay_icu_common ogplay_icu_i18n)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 11 C_STANDARD 99
        POSITION_INDEPENDENT_CODE ON)
    target_include_directories(${target} SYSTEM PUBLIC
        "${ogplay_icu51_SOURCE_DIR}/common"
        "${ogplay_icu51_SOURCE_DIR}/i18n")
    target_compile_definitions(${target} PUBLIC U_STATIC_IMPLEMENTATION
        U_HAVE_STDINT_H=1 PRIVATE UCONFIG_NO_FILE_IO=1 U_ENABLE_DYLOAD=0)
    if(MSVC)
        target_compile_options(${target} PRIVATE /w /utf-8)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endforeach()
target_compile_definitions(ogplay_icu_common PRIVATE U_COMMON_IMPLEMENTATION)
target_compile_definitions(ogplay_icu_i18n PRIVATE U_I18N_IMPLEMENTATION)
target_link_libraries(ogplay_icu_i18n PUBLIC ogplay_icu_common)
if(WIN32)
    target_link_libraries(ogplay_icu_common PRIVATE advapi32)
endif()

set(_icu_data_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/icu51_data.cpp")
add_custom_command(OUTPUT "${_icu_data_cpp}"
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/bootdex/embed_icu_data.py"
        "${PROJECT_SOURCE_DIR}/data/android/19/icu/icudt51l.dat" "${_icu_data_cpp}"
    DEPENDS tools/bootdex/embed_icu_data.py data/android/19/icu/icudt51l.dat
    VERBATIM)
add_library(ogplay_icu_data STATIC "${_icu_data_cpp}")
set_target_properties(ogplay_icu_data PROPERTIES POSITION_INDEPENDENT_CODE ON)

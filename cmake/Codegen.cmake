# Included from the project root; source/binary paths use that directory scope.
# GLES API catalogs generated from data/gles/<name>.json; OGPLAY_<NAME>_CATALOG
# names each header for the catalog-current tests.
set(_ogplay_gles_catalog_headers)
foreach(_catalog IN ITEMS gles1 gles1_extensions gles2 gles2_extensions gles3)
    string(TOUPPER "${_catalog}" _catalog_variable)
    set(OGPLAY_${_catalog_variable}_CATALOG
        "${OGPLAY_GENERATED_DIR}/ogplay/gles/generated/${_catalog}_catalog.h")
    add_custom_command(
        OUTPUT "${OGPLAY_${_catalog_variable}_CATALOG}"
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py"
            --idl "${CMAKE_CURRENT_SOURCE_DIR}/data/gles/${_catalog}.json"
            --output "${OGPLAY_${_catalog_variable}_CATALOG}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/gles/${_catalog}.json"
        VERBATIM
    )
    list(APPEND _ogplay_gles_catalog_headers "${OGPLAY_${_catalog_variable}_CATALOG}")
endforeach()
add_custom_target(ogplay_gles_codegen DEPENDS ${_ogplay_gles_catalog_headers})
set(OGPLAY_DEXVM_OPCODE_TABLE
    "${OGPLAY_GENERATED_DIR}/ogplay/runtime/dexvm/generated/opcode_table.h")
add_custom_command(
    OUTPUT "${OGPLAY_DEXVM_OPCODE_TABLE}"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dexvm_opcodes.py"
        --catalog "${CMAKE_CURRENT_SOURCE_DIR}/data/dexvm/dalvik_opcodes.json"
        --output "${OGPLAY_DEXVM_OPCODE_TABLE}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dexvm_opcodes.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/dexvm/dalvik_opcodes.json"
    VERBATIM
)
add_custom_target(ogplay_dexvm_codegen DEPENDS "${OGPLAY_DEXVM_OPCODE_TABLE}")
set(OGPLAY_DEXVM_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/dexvm_fixtures")
file(GLOB OGPLAY_DEXVM_FIXTURE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/dexvm/fixtures/*.dexasm")
add_custom_command(
    OUTPUT "${OGPLAY_DEXVM_FIXTURE_DIR}/.stamp"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/dexasm.py"
        --catalog "${CMAKE_CURRENT_SOURCE_DIR}/data/dexvm/dalvik_opcodes.json"
        --batch-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/dexvm/fixtures"
        --batch-output "${OGPLAY_DEXVM_FIXTURE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${OGPLAY_DEXVM_FIXTURE_DIR}/.stamp"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/dexasm.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/dex_survey_lib.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/dexvm/dalvik_opcodes.json"
        ${OGPLAY_DEXVM_FIXTURE_SOURCES}
    VERBATIM
)
add_custom_target(ogplay_dexvm_fixtures
    DEPENDS "${OGPLAY_DEXVM_FIXTURE_DIR}/.stamp")

# ADR-0158: controlled test ROM plus existing exception closure, no production recipe change.
add_custom_command(
    OUTPUT "${OGPLAY_DEXVM_FIXTURE_DIR}/member-sources.json"
           "${OGPLAY_DEXVM_FIXTURE_DIR}/member-sources.dex"
           "${OGPLAY_DEXVM_FIXTURE_DIR}/member-sources.jar"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/member_sources.py"
        --fixture "${OGPLAY_DEXVM_FIXTURE_DIR}/member-sources.json"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/member_sources.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/build_bootdex.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/dexvm/fixtures/member_composition.dexasm"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/dexvm/fixtures/member_composition.surface.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/android/19/framework/bootdex.jar"
    VERBATIM
)
add_custom_target(ogplay_member_sources_fixture
    DEPENDS "${OGPLAY_DEXVM_FIXTURE_DIR}/member-sources.json"
            "${OGPLAY_DEXVM_FIXTURE_DIR}/member-sources.dex"
            "${OGPLAY_DEXVM_FIXTURE_DIR}/member-sources.jar")
add_dependencies(ogplay_dexvm_fixtures ogplay_member_sources_fixture)

# Fixed sealed JavaTraversal counterpart. The production default remains Legacy.
add_custom_command(
    OUTPUT "${OGPLAY_DEXVM_FIXTURE_DIR}/view-measure.jar"
           "${OGPLAY_DEXVM_FIXTURE_DIR}/view-measure.manifest.json"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/compose_bootdex.py" build
        --view-measure-test-output "${OGPLAY_DEXVM_FIXTURE_DIR}/view-measure.jar"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/compose_bootdex.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/member_sources.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/member-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-measure-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-owner.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-button-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-edittext-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-edittext.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-scroll-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-scroll.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-video-adapter.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-surface-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-surface.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-button.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-image-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-image.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-paint-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-paint.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-text.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-group-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-frame-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-linear-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-relative-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-xml-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-xml.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-inflater-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-inflater.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/experimental-java/android/view/LocalLayoutInflater.java"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-context-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-context.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-theme-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-theme.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/theme_projection.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/theme_inputs.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/theme-projection-inputs.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-system-sources.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/view-measure.surface.json.gz"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/api19.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/android/19/framework/bootdex.jar"
    VERBATIM
)
add_custom_target(ogplay_view_measure_fixture
    DEPENDS "${OGPLAY_DEXVM_FIXTURE_DIR}/view-measure.jar"
            "${OGPLAY_DEXVM_FIXTURE_DIR}/view-measure.manifest.json")
add_dependencies(ogplay_dexvm_fixtures ogplay_view_measure_fixture)

# API19 ICU property fallbacks follow the bundled payload metadata, not host ICU.
set(_ogplay_platform_manifest "${CMAKE_CURRENT_SOURCE_DIR}/data/android/19/manifest.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ogplay_platform_manifest}")
file(READ "${_ogplay_platform_manifest}" _ogplay_platform_metadata)
foreach(_version IN ITEMS version unicode_version cldr_version)
    string(JSON _ogplay_icu_${_version} GET "${_ogplay_platform_metadata}" icu ${_version})
    if(NOT _ogplay_icu_${_version} MATCHES "^[0-9]+(\\.[0-9]+)*$")
        message(FATAL_ERROR "Invalid bundled ICU ${_version}")
    endif()
endforeach()
file(WRITE "${OGPLAY_GENERATED_DIR}/ogplay/runtime/dexvm/generated/icu_metadata.h"
    "#pragma once\n#include <string_view>\nnamespace ogplay::runtime::dexvm {\ninline constexpr std::string_view kBundledIcuVersion = \"${_ogplay_icu_version}\";\ninline constexpr std::string_view kBundledUnicodeVersion = \"${_ogplay_icu_unicode_version}\";\ninline constexpr std::string_view kBundledCldrVersion = \"${_ogplay_icu_cldr_version}\";\n}\n")

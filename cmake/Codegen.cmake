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

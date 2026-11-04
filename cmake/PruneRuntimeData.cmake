# Removes files the runtime never reads from a staged or bundled data directory.
if(NOT DEFINED DATA_ROOT)
    message(FATAL_ERROR "DATA_ROOT is required")
endif()
file(GLOB_RECURSE _excluded
    "${DATA_ROOT}/.DS_Store"
    "${DATA_ROOT}/README.md"
    "${DATA_ROOT}/*.schema.json"
    "${DATA_ROOT}/source-manifest.xml")
if(_excluded)
    file(REMOVE ${_excluded})
endif()

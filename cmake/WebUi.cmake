option(OGPLAY_BUILD_WEBUI "Build static Web UI with pinned npm dependencies" OFF)
set(OGPLAY_WEBVIEW2_SDK "${PROJECT_SOURCE_DIR}/.local/gui-v2/webview2" CACHE PATH
    "Extracted Microsoft.Web.WebView2 1.0.1150.38 SDK (see webui/README.md)")

if(OGPLAY_BUILD_WEBUI)
    find_program(OGPLAY_NPM NAMES npm.cmd npm REQUIRED)
    add_custom_target(webui
        COMMAND "${OGPLAY_NPM}" ci --no-audit --no-fund
        COMMAND "${OGPLAY_NPM}" run build
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/webui"
        VERBATIM)
endif()

# Runtime developers with GUI disabled do not need Node or WebView2.
if(OGPLAY_ENABLE_SDL3 AND WIN32)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/webview/core/include/webview/webview.h")
        message(FATAL_ERROR "Initialize the third_party/webview submodule")
    endif()
    if(NOT EXISTS "${OGPLAY_WEBVIEW2_SDK}/build/native/include/WebView2.h")
        message(FATAL_ERROR "WebView2 SDK missing; run webui/prepare-sdk.ps1")
    endif()
    add_library(ogplay_webview STATIC src/hal/windows/webview_backend.cpp)
    target_compile_definitions(ogplay_webview PUBLIC WEBVIEW_STATIC)
    target_include_directories(ogplay_webview SYSTEM PUBLIC
        "${PROJECT_SOURCE_DIR}/third_party/webview/core/include"
        "${OGPLAY_WEBVIEW2_SDK}/build/native/include")
    target_link_libraries(ogplay_webview PUBLIC advapi32 ole32 shell32 shlwapi user32 version)
    if(MSVC)
        target_compile_options(ogplay_webview PRIVATE /w)
    endif()
    target_sources(ogplay_hal PRIVATE src/hal/windows/webview_host.cpp)
    target_link_libraries(ogplay_hal PRIVATE ogplay_webview)
    add_custom_target(ogplay_check_webui
        COMMAND "${CMAKE_COMMAND}" -DWEBUI_ROOT=${PROJECT_SOURCE_DIR}/data/webui/gui
            -P "${PROJECT_SOURCE_DIR}/cmake/CheckWebUi.cmake"
        COMMAND "${CMAKE_COMMAND}" -DWEBUI_ROOT=${PROJECT_SOURCE_DIR}/data/webui/dashboard
            -P "${PROJECT_SOURCE_DIR}/cmake/CheckWebUi.cmake"
        VERBATIM)
    if(TARGET webui)
        add_dependencies(ogplay_check_webui webui)
    endif()
endif()

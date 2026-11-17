option(OGPLAY_BUILD_WEBUI "Build static Web UI with pinned npm dependencies" OFF)
set(OGPLAY_WEBVIEW2_SDK "${PROJECT_SOURCE_DIR}/.local/gui-v2/webview2" CACHE PATH
    "Extracted Microsoft.Web.WebView2 1.0.1150.38 SDK (see webui/README.md)")

if(OGPLAY_BUILD_WEBUI)
    find_program(OGPLAY_NPM NAMES npm.cmd npm REQUIRED)
    add_custom_target(webui
        COMMAND "${OGPLAY_NPM}" ci --no-audit --no-fund
        COMMAND "${CMAKE_COMMAND}" -E env "OGPLAY_WEBUI_PLATFORM=${CMAKE_SYSTEM_NAME}"
            "${OGPLAY_NPM}" run $<IF:$<PLATFORM_ID:Android>,build:android,build>
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/webui"
        VERBATIM)
endif()

# Runtime developers with GUI disabled do not need Node or a WebView SDK.
if(OGPLAY_BUILD_GUI)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/webview/core/include/webview/webview.h")
        message(FATAL_ERROR "Initialize the third_party/webview submodule")
    endif()
    if(WIN32)
        if(NOT EXISTS "${OGPLAY_WEBVIEW2_SDK}/build/native/include/WebView2.h")
            message(FATAL_ERROR "WebView2 SDK missing; run webui/prepare-sdk.ps1")
        endif()
        # Add an explicit data-directory constructor to a build-tree copy; keep the submodule pristine.
        find_package(Git REQUIRED)
        set(_webview_patch "${PROJECT_SOURCE_DIR}/src/hal/windows/webview-data-directory.patch")
        set(_webview_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/webview")
        configure_file("${PROJECT_SOURCE_DIR}/third_party/webview/core/include/webview/webview.h"
            "${_webview_generated}/input/webview/webview.h" COPYONLY)
        file(COPY_FILE "${PROJECT_SOURCE_DIR}/third_party/webview/core/include/webview/webview.h"
            "${_webview_generated}/input/webview/webview.h")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_webview_patch}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_webview_generated}"
                "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${_webview_patch}"
            WORKING_DIRECTORY "${_webview_generated}/input"
            RESULT_VARIABLE _webview_patch_status ERROR_VARIABLE _webview_patch_error)
        if(NOT _webview_patch_status STREQUAL "0")
            message(FATAL_ERROR "Cannot apply WebView2 data directory patch: ${_webview_patch_error}")
        endif()
        configure_file("${_webview_generated}/input/webview/webview.h"
            "${_webview_generated}/webview/webview.h" COPYONLY)
        add_library(ogplay_webview STATIC src/hal/windows/webview_backend.cpp)
        target_include_directories(ogplay_webview SYSTEM PUBLIC
            "${_webview_generated}"
            "${OGPLAY_WEBVIEW2_SDK}/build/native/include")
        target_link_libraries(ogplay_webview PUBLIC advapi32 ole32 shell32 shlwapi user32 version)
        if(MSVC)
            target_compile_options(ogplay_webview PRIVATE /w)
        endif()
        add_library(ogplay_desktop_webview STATIC src/hal/windows/webview_host.cpp)
        target_link_libraries(ogplay_desktop_webview PRIVATE dwmapi)
    else()
        # macOS uses the system WKWebView through the same pinned webview release.
        add_library(ogplay_webview STATIC src/hal/macos/webview_backend.cpp)
        target_include_directories(ogplay_webview SYSTEM PUBLIC
            "${PROJECT_SOURCE_DIR}/third_party/webview/core/include")
        target_compile_options(ogplay_webview PRIVATE -w)
        target_link_libraries(ogplay_webview PUBLIC "-framework Cocoa" "-framework WebKit")
        add_library(ogplay_desktop_webview STATIC src/hal/macos/webview_host.mm)
        target_compile_options(ogplay_desktop_webview PRIVATE -fobjc-arc)
        target_link_libraries(ogplay_desktop_webview PRIVATE "-framework UniformTypeIdentifiers")
    endif()
    target_compile_definitions(ogplay_webview PUBLIC WEBVIEW_STATIC)
    target_link_libraries(ogplay_desktop_webview PUBLIC ogplaycore PRIVATE ogplay_webview)
    target_include_directories(ogplay_desktop_webview PRIVATE "${PROJECT_SOURCE_DIR}/src")
    ogplay_set_project_warnings(ogplay_desktop_webview)
    add_custom_target(ogplay_check_webui
        COMMAND "${CMAKE_COMMAND}" -DWEBUI_ROOT=${PROJECT_SOURCE_DIR}/data/webui/gui
            -DWEBUI_PLATFORM=${CMAKE_SYSTEM_NAME}
            -P "${PROJECT_SOURCE_DIR}/cmake/CheckWebUi.cmake"
        COMMAND "${CMAKE_COMMAND}" -DWEBUI_ROOT=${PROJECT_SOURCE_DIR}/data/webui/dashboard
            -P "${PROJECT_SOURCE_DIR}/cmake/CheckWebUi.cmake"
        COMMAND "${CMAKE_COMMAND}" -DWEBUI_ROOT=${PROJECT_SOURCE_DIR}/data/webui/controls
            -P "${PROJECT_SOURCE_DIR}/cmake/CheckWebUi.cmake"
        VERBATIM)
    if(TARGET webui)
        add_dependencies(ogplay_check_webui webui)
    endif()
endif()

if(NOT TARGET ogplay_desktop_webview)
    add_library(ogplay_desktop_webview INTERFACE)
endif()

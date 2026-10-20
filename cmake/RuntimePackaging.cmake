# Included from the project root; source/binary paths use that directory scope.
# Data directories are replaced wholesale so files removed from the source tree
# do not linger in the staged or installed runtime.
set(OGPLAY_DATA_DIRECTORIES profiles android dexvm)
set(_ogplay_stage_data_commands)
foreach(_data_directory IN LISTS OGPLAY_DATA_DIRECTORIES)
    list(APPEND _ogplay_stage_data_commands
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:ogplaycore>/data/${_data_directory}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data/${_data_directory}"
            "$<TARGET_FILE_DIR:ogplaycore>/data/${_data_directory}")
endforeach()
add_custom_target(ogplay_stage_data ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ogplaycore>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/data/quirks.toml"
        "$<TARGET_FILE_DIR:ogplaycore>/data/quirks.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/capabilities.toml"
        "$<TARGET_FILE_DIR:ogplaycore>/data/capabilities.toml"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:ogplaycore>/data/input"
    ${_ogplay_stage_data_commands}
    COMMENT "Staging OGPlay runtime data"
    VERBATIM)

add_dependencies(ogplay-cli ogplay_stage_data)
if(TARGET ogplay)
    add_dependencies(ogplay ogplay_stage_data)
endif()

# CLI MCP/controls pages remain available when the desktop host is disabled.
if(OGPLAY_BUILD_GUI OR
   (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/webui/dashboard/manifest.json" AND
    EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/webui/controls/manifest.json"))
    set(_ogplay_webui_parts dashboard controls)
    if(OGPLAY_BUILD_GUI)
        list(APPEND _ogplay_webui_parts gui)
    endif()
    set(_ogplay_webui_commands)
    foreach(_part IN LISTS _ogplay_webui_parts)
        list(APPEND _ogplay_webui_commands
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/data/webui/${_part}"
                "$<TARGET_FILE_DIR:ogplaycore>/data/webui/${_part}")
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/webui/${_part}"
            DESTINATION ${CMAKE_INSTALL_BINDIR}/data/webui COMPONENT Runtime)
    endforeach()
    add_custom_target(ogplay_stage_webui ${_ogplay_webui_commands} VERBATIM)
    add_dependencies(ogplay-cli ogplay_stage_webui)
    if(OGPLAY_BUILD_GUI)
        add_dependencies(ogplay_stage_webui ogplay_check_webui)
        add_dependencies(ogplay ogplay_stage_webui)
        if(WIN32)
            install(FILES "${OGPLAY_WEBVIEW2_SDK}/LICENSE.txt"
                DESTINATION ${CMAKE_INSTALL_BINDIR}/data/webui/gui RENAME WEBVIEW2-SDK-LICENSE.txt
                COMPONENT Runtime)
        endif()
    endif()
endif()
if(TARGET ogplay AND APPLE)
    # Complete and ad-hoc sign OGPlay.app on every build so the bundle never lags
    # ogplaycore or the staged payload; it is self-contained and runs from any directory.
    set(_ogplay_app "$<TARGET_BUNDLE_DIR:ogplay>")
    set(_ogplay_frameworks
        "$<TARGET_FILE:ogplaycore>" "$<TARGET_SONAME_FILE:SDL3::SDL3>"
        "${OGPLAY_ANGLE_RUNTIME_DIR}/libEGL.dylib" "${OGPLAY_ANGLE_RUNTIME_DIR}/libGLESv2.dylib")
    if(OGPLAY_FFMPEG_FOUND)
        list(APPEND _ogplay_frameworks "${OGPLAY_FFMPEG_RUNTIME}")
    endif()
    add_custom_target(ogplay_bundle ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_ogplay_app}/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_ogplay_frameworks} "${_ogplay_app}/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${_ogplay_app}/Contents/Resources/data"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:ogplaycore>/data" "${_ogplay_app}/Contents/Resources/data"
        COMMAND codesign --force --sign - --timestamp=none "${_ogplay_app}"
        COMMAND codesign --verify --strict --deep "${_ogplay_app}"
        COMMENT "Assembling and ad-hoc signing OGPlay.app"
        VERBATIM)
    add_dependencies(ogplay_bundle ogplay ogplaycore ogplay_stage_data ogplay_stage_webui)
    install(TARGETS ogplay BUNDLE DESTINATION . COMPONENT Runtime)
elseif(TARGET ogplay)
    install(TARGETS ogplay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
endif()
install(TARGETS ogplaycore ogplay-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Development)
install(FILES "${OGPLAY_GENERATED_DIR}/ogplay/app/export.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ogplay/app COMPONENT Development)
if(OGPLAY_ENABLE_ANGLE)
    if(WIN32)
        install(DIRECTORY "${_angle_sdk}/bin/" DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT Runtime FILES_MATCHING PATTERN "*.dll")
    elseif(APPLE)
        install(DIRECTORY "${OGPLAY_ANGLE_RUNTIME_DIR}/" DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT Runtime FILES_MATCHING PATTERN "*.dylib")
    else()
        install(DIRECTORY "${_angle_sdk}/lib/" DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT Runtime FILES_MATCHING PATTERN "*.so*")
    endif()
endif()
if(OGPLAY_FFMPEG_FOUND)
    if(WIN32)
        set(_ogplay_ffmpeg_install_dir ${CMAKE_INSTALL_BINDIR})
    else()
        set(_ogplay_ffmpeg_install_dir ${CMAKE_INSTALL_LIBDIR})
    endif()
    install(FILES "${OGPLAY_FFMPEG_RUNTIME}" DESTINATION ${_ogplay_ffmpeg_install_dir} COMPONENT Runtime)
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES data/quirks.toml capabilities.toml
    DESTINATION ${CMAKE_INSTALL_BINDIR}/data COMPONENT Runtime)
foreach(_data_directory IN LISTS OGPLAY_DATA_DIRECTORIES)
    install(DIRECTORY data/${_data_directory}
        DESTINATION ${CMAKE_INSTALL_BINDIR}/data COMPONENT Runtime)
endforeach()

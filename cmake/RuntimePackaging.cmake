# Included from the project root; source/binary paths use that directory scope.
function(ogplay_stage_angle_runtime target)
    if(NOT OGPLAY_ENABLE_ANGLE)
        return()
    endif()
    # Windows keeps EGL/GLESv2 DLLs under bin/; macOS stages the relocated
    # @rpath copies beside the binary, which finds them through @loader_path.
    set(_ogplay_angle_stage_commands)
    if(EXISTS "${_angle_sdk}/bin")
        list(APPEND _ogplay_angle_stage_commands
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_angle_sdk}/bin" $<TARGET_FILE_DIR:${target}>
        )
    endif()
    if(APPLE)
        foreach(_ogplay_angle_dylib IN ITEMS libEGL.dylib libGLESv2.dylib)
            if(EXISTS "${OGPLAY_ANGLE_RUNTIME_DIR}/${_ogplay_angle_dylib}")
                list(APPEND _ogplay_angle_stage_commands
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${OGPLAY_ANGLE_RUNTIME_DIR}/${_ogplay_angle_dylib}"
                        "$<TARGET_FILE_DIR:${target}>/${_ogplay_angle_dylib}"
                )
            endif()
        endforeach()
    endif()
    if(ANDROID)
        list(APPEND _ogplay_angle_stage_commands
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_angle_egl_library}" "${_angle_glesv2_library}" $<TARGET_FILE_DIR:${target}>
        )
    endif()
    if(_ogplay_angle_stage_commands)
        add_custom_command(TARGET ${target} POST_BUILD
            ${_ogplay_angle_stage_commands}
        )
    endif()
endfunction()

# Places the ogplay_ffmpeg runtime beside an executable; macOS resolves its
# @rpath install name through @loader_path.
function(ogplay_stage_ffmpeg_runtime target)
    if(NOT OGPLAY_FFMPEG_FOUND)
        return()
    endif()
    get_filename_component(_ogplay_ffmpeg_name "${OGPLAY_FFMPEG_RUNTIME}" NAME)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OGPLAY_FFMPEG_RUNTIME}"
            "$<TARGET_FILE_DIR:${target}>/${_ogplay_ffmpeg_name}"
    )
    if(APPLE)
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "@loader_path")
    endif()
endfunction()

ogplay_stage_angle_runtime(ogplaycore)
ogplay_stage_ffmpeg_runtime(ogplaycore)

# Data directories are replaced wholesale so files removed from the source tree
# do not linger in the staged or installed runtime.
set(OGPLAY_DATA_DIRECTORIES profiles android branding)
# OGPlay's own demo games, committed prebuilt by their samples and staged as data/demos; the launcher
# seeds them into the library while the show_demo_games setting is on.
set(OGPLAY_DEMO_APKS "${CMAKE_CURRENT_SOURCE_DIR}/samples/sweetshop/out/ogplay-sweetshop.apk")
# Android stages data for Gradle's assets instead of beside the packaged libraries.
set(OGPLAY_RUNTIME_DATA_DIR "" CACHE PATH "Staged runtime data directory; empty places it beside ogplaycore")
if(OGPLAY_RUNTIME_DATA_DIR)
    set(_ogplay_data_root "${OGPLAY_RUNTIME_DATA_DIR}")
else()
    set(_ogplay_data_root "$<TARGET_FILE_DIR:ogplaycore>/data")
endif()
set(_ogplay_stage_data_commands)
foreach(_data_directory IN LISTS OGPLAY_DATA_DIRECTORIES)
    list(APPEND _ogplay_stage_data_commands
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${_ogplay_data_root}/${_data_directory}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data/${_data_directory}"
            "${_ogplay_data_root}/${_data_directory}")
endforeach()
add_custom_target(ogplay_stage_data ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${_ogplay_data_root}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/data/credits.json"
        "${_ogplay_data_root}/credits.json"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_ogplay_data_root}/demos"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_ogplay_data_root}/demos"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OGPLAY_DEMO_APKS} "${_ogplay_data_root}/demos"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_ogplay_data_root}/input"
    ${_ogplay_stage_data_commands}
    COMMAND ${CMAKE_COMMAND} "-DDATA_ROOT=${_ogplay_data_root}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PruneRuntimeData.cmake"
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
        COMMAND ${CMAKE_COMMAND} "-DDATA_ROOT=${_ogplay_app}/Contents/Resources/data"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PruneRuntimeData.cmake"
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
install(FILES data/credits.json
    DESTINATION ${CMAKE_INSTALL_BINDIR}/data COMPONENT Runtime)
install(FILES ${OGPLAY_DEMO_APKS} DESTINATION ${CMAKE_INSTALL_BINDIR}/data/demos COMPONENT Runtime)
foreach(_data_directory IN LISTS OGPLAY_DATA_DIRECTORIES)
    install(DIRECTORY data/${_data_directory}
        DESTINATION ${CMAKE_INSTALL_BINDIR}/data COMPONENT Runtime
        PATTERN ".DS_Store" EXCLUDE
        PATTERN "README.md" EXCLUDE
        PATTERN "*.schema.json" EXCLUDE
        PATTERN "source-manifest.xml" EXCLUDE)
endforeach()

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

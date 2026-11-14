# Included from the project root on Android. SDLActivity loads ogplay_android and enters
# SDL_main; Gradle packages every shared library beside it in the library output directory.
add_library(ogplay_android SHARED src/frontend/android/main.cpp)
target_link_libraries(ogplay_android PRIVATE ogplaycore log)
target_link_options(ogplay_android PRIVATE "LINKER:--no-undefined")
ogplay_set_project_warnings(ogplay_android)
foreach(_ogplay_slim_option IN LISTS _ogplay_slim_link_options)
    target_link_options(ogplay_android PRIVATE "$<$<CONFIG:Release>:${_ogplay_slim_option}>")
endforeach()

# The launcher process (LauncherActivity) loads ogplay_launcher for the Web UI's rpc bridge.
add_library(ogplay_launcher SHARED src/frontend/android/launcher.cpp)
target_link_libraries(ogplay_launcher PRIVATE ogplay_launcher_rpc ogplaycore log)
target_include_directories(ogplay_launcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_options(ogplay_launcher PRIVATE "LINKER:--no-undefined")
ogplay_set_project_warnings(ogplay_launcher)
foreach(_ogplay_slim_option IN LISTS _ogplay_slim_link_options)
    target_link_options(ogplay_launcher PRIVATE "$<$<CONFIG:Release>:${_ogplay_slim_option}>")
endforeach()

# The launcher page: the Android Web UI build (npm run build:android), checked against its manifest and
# staged as data/webui/gui beside the other runtime data. A missing or stale build fails here.
set(_ogplay_android_webui "${CMAKE_CURRENT_SOURCE_DIR}/data/webui/android/gui")
add_custom_target(ogplay_stage_android_webui
    COMMAND ${CMAKE_COMMAND} -DWEBUI_ROOT=${_ogplay_android_webui} -DWEBUI_PLATFORM=Android
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckWebUi.cmake"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_ogplay_data_root}/webui"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_ogplay_android_webui}" "${_ogplay_data_root}/webui/gui"
    COMMENT "Staging the Android launcher Web UI"
    VERBATIM)
if(TARGET webui)
    add_dependencies(ogplay_stage_android_webui webui)
endif()
add_dependencies(ogplay_stage_android_webui ogplay_stage_data)
add_dependencies(ogplay_launcher ogplay_stage_android_webui)

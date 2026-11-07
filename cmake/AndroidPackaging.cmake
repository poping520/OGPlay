# Included from the project root on Android. SDLActivity loads ogplay_android and enters
# SDL_main; Gradle packages every shared library beside it in the library output directory.
add_library(ogplay_android SHARED src/frontend/android/main.cpp)
target_link_libraries(ogplay_android PRIVATE ogplaycore log)
target_link_options(ogplay_android PRIVATE "LINKER:--no-undefined")
ogplay_set_project_warnings(ogplay_android)
foreach(_ogplay_slim_option IN LISTS _ogplay_slim_link_options)
    target_link_options(ogplay_android PRIVATE "$<$<CONFIG:Release>:${_ogplay_slim_option}>")
endforeach()

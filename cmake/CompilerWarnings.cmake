function(ogplay_set_project_warnings target)
    if(MSVC)
        # Visual Studio parallelizes projects with /m, but each large OGPlay
        # target still compiles its translation units serially unless cl.exe
        # receives /MP.  Keep this on our targets rather than applying it
        # globally to bundled third-party projects.
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /MP)
        if(OGPLAY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        )
        if(OGPLAY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

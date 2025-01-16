if (USE_ARGS OR USE_M3U OR USE_WAV)
    add_library(common)
    target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    if (USE_ARGS)
        target_sources(common PRIVATE args/args.c)
    endif()

    if (USE_M3U)
        target_sources(common PRIVATE m3u/m3u.c)
    endif()

    if (USE_WAV)
        target_sources(common PRIVATE wav_writer/wav_writer.c)
    endif()
endif()

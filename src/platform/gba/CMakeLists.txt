cmake_minimum_required(VERSION 3.13)

project(TotalGBS
    VERSION ${LIBGBS_VERSION}
    LANGUAGES C
)

set(USE_M3U ON)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/platform/common binary_dir)

add_executable(TotalGBS main.c)

set_target_properties(TotalGBS PROPERTIES
    C_STANDARD 99
    C_EXTENSIONS ON
)

# add compiler flags
target_compile_options(TotalGBS PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

target_link_libraries(TotalGBS PRIVATE gbs common)

gba_create_rom(TotalGBS
    TITLE "TotalGBS"
    GAMECODE "TGBS"
    MAKERCODE "TJ"
    VERSION ${LIBGBS_VERSION}
)

# place uncompressed roms in this directory
# otherwise the output will be a base rom with no rom embeded
# to embed a rom, use rom-convert.py
SET(ROMS_DIRECTORY ${CMAKE_SOURCE_DIR}/roms)

if (EXISTS ${ROMS_DIRECTORY} AND IS_DIRECTORY ${ROMS_DIRECTORY})
    find_package(Python COMPONENTS Interpreter)

    add_custom_command(OUTPUT rom.gba
        COMMAND ${Python_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/rom-converter.py --rom ${ROMS_DIRECTORY} --z64 TotalGBS.gba --ext .gbs .zip --offset 0x20000 --out_ext .gba --out ${CMAKE_BINARY_DIR}/out
        DEPENDS TotalGBS
        VERBATIM
    )
    add_custom_target(embed_rom_script ALL DEPENDS rom.gba)
endif()

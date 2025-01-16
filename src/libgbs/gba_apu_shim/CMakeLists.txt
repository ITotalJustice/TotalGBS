cmake_minimum_required(VERSION 3.13)


project(gb_apu
    VERSION 1.2.1
    LANGUAGES C
)

add_library(gb_apu gb_apu.c)
target_include_directories(gb_apu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(gb_apu PROPERTIES C_STANDARD 99)

# enable warnings
target_compile_options(gb_apu PRIVATE
    $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>,$<C_COMPILER_ID:GNU>>:
        -Wall
        -Wextra
    >
    $<$<C_COMPILER_ID:MSVC>:
        /W4
    >
)

cmake_minimum_required(VERSION 3.13)

project(TotalGBS
    VERSION ${LIBGBS_VERSION}
    LANGUAGES C
)

add_library(gbs gbs.c)

target_include_directories(gbs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(gbs PROPERTIES C_STANDARD 99)

# add compiler flags
target_compile_options(gbs PRIVATE
    $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>,$<C_COMPILER_ID:GNU>>:
        -Wall
        -Wextra
    >
    $<$<C_COMPILER_ID:MSVC>:
        /W4
    >
)

target_compile_definitions(gbs PUBLIC
    LIBGBS_VERSION_MAJOR=${LIBGBS_VERSION_MAJOR}
    LIBGBS_VERSION_MINOR=${LIBGBS_VERSION_MINOR}
    LIBGBS_VERSION_PATCH=${LIBGBS_VERSION_PATCH}
    LIBGBS_VERSION_STR="${LIBGBS_VERSION}"
)

# fetch libraries
include(FetchContent)

FetchContent_Declare(gb_apu
    GIT_REPOSITORY https://github.com/ITotalJustice/gb_apu.git
    GIT_TAG        v1.4.0
    GIT_PROGRESS   TRUE
)

FetchContent_Declare(scheduler
    GIT_REPOSITORY https://github.com/ITotalJustice/scheduler.git
    GIT_TAG        v1.1.0
    GIT_PROGRESS   TRUE
)

FetchContent_MakeAvailable(scheduler)

target_include_directories(gbs PRIVATE LR35902)

if (GBA)
    add_subdirectory(gba_apu_shim)
else()
    FetchContent_MakeAvailable(gb_apu)
endif()

target_compile_definitions(gbs PUBLIC
    GBS_ENABLE_LRU=$<BOOL:${GBS_ENABLE_LRU}>
    GBS_ENABLE_GBS2GB=$<BOOL:${GBS_ENABLE_GBS2GB}>
)

target_link_libraries(gbs PRIVATE gb_apu)
target_link_libraries(gbs PRIVATE scheduler)

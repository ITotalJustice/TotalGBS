cmake_minimum_required(VERSION 3.13)

set(LIBGBS_VERSION_MAJOR 1)
set(LIBGBS_VERSION_MINOR 0)
set(LIBGBS_VERSION_PATCH 0)
set(LIBGBS_VERSION ${LIBGBS_VERSION_MAJOR}.${LIBGBS_VERSION_MINOR}.${LIBGBS_VERSION_PATCH})

project(TotalGBS
    VERSION ${LIBGBS_VERSION}
    LANGUAGES C
)

if (NOT DEFINED GB_APU_CXX)
    set(GB_APU_CXX ON)
endif()

if (NOT DEFINED GBS_ENABLE_LRU)
    set(GBS_ENABLE_LRU OFF)
endif()

if (NOT DEFINED GBS_ENABLE_GBS2GB)
    set(GBS_ENABLE_GBS2GB OFF)
endif()

if (NOT DEFINED ENABLE_LTO)
    set(ENABLE_LTO ON)
endif()

if (GB_APU_CXX)
    message(STATUS "using cxx")
    enable_language(CXX)
else()
    message(STATUS "not using cxx")
endif()

# only enable the below options if its not included by another project
if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    # export compile commands
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    # enable LTO (only in release builds)
    if (ENABLE_LTO AND (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel"))
        include(CheckIPOSupported)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
        if (ipo_supported)
            message(STATUS "IPO / LTO enabled for ALL targets")
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(STATUS "IPO / LTO not supported: <${ipo_error}>")
        endif()
    else()
        message(STATUS "IPO / LTO not enabled in debug build")
    endif()
endif()

# helpers
function(target_add_compiler_flag is_c target scope)
    foreach(flag IN LISTS ARGN)
        string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" check_flag ${flag})
        if (is_c)
            include(CheckCCompilerFlag)
            check_c_compiler_flag(${flag} CHECK_FLAG_${check_flag})
        else()
            include(CheckCXXCompilerFlag)
            check_cxx_compiler_flag(${flag} CHECK_FLAG_${check_flag})
        endif()
        if (CHECK_FLAG_${check_flag})
            target_compile_options(${target} ${scope} ${flag})
        endif()
    endforeach()
endfunction()

function(target_add_linker_flag is_c target scope)
    include(CheckLinkerFlag)
    foreach(flag IN LISTS ARGN)
        string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" check_flag ${flag})
        if (is_c)
            check_linker_flag("C" ${flag} CHECK_FLAG_${check_flag})
        else()
            check_linker_flag("CXX" ${flag} CHECK_FLAG_${check_flag})
        endif()
        if (CHECK_FLAG_${check_flag})
            target_link_options(${target} ${scope} ${flag})
        endif()
    endforeach()
endfunction()

function(target_add_c_flag target scope)
    target_add_compiler_flag(true ${target} ${scope} ${ARGN})
endfunction()

function(target_add_cxx_flag target scope)
    target_add_compiler_flag(false ${target} ${scope} ${ARGN})
endfunction()

function(target_add_c_linker_flag target scope)
    target_add_linker_flag(true ${target} ${scope} ${ARGN})
endfunction()

function(target_add_cxx_linker_flag target scope)
    target_add_linker_flag(false ${target} ${scope} ${ARGN})
endfunction()

if (GBA OR NDS)
    add_compile_options(-Ofast -fomit-frame-pointer)
endif()

# add_compile_options(
#     -fsanitize=address
#     -fsanitize=leak
#     -fsanitize=undefined
#     -fsanitize=pointer-compare
#     -fsanitize=pointer-subtract
#     -fstack-protector-all
# )

# add_link_options(
#     -fsanitize=address
#     -fsanitize=leak
#     -fsanitize=undefined
#     -fsanitize=pointer-compare
#     -fsanitize=pointer-subtract
#     -fstack-protector-all
# )

add_subdirectory(src)
